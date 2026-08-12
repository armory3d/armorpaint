
#include "vulkan_gpu.h"
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <malloc.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

extern uint32_t constant_buffer_index;

static VkSemaphore                      framebuffer_available_semaphore;
static VkSemaphore                      rendering_finished_semaphores[GPU_FRAMEBUFFER_COUNT];
static VkFence                          fence;
static VkViewport                       current_viewport;
static VkRect2D                         current_scissor;
static gpu_buffer_t                    *current_vb;
static gpu_buffer_t                    *current_ib;
static VkDescriptorSetLayout            descriptor_layout;
static VkDescriptorSet                  descriptor_sets[GPU_CONSTANT_BUFFER_MULTIPLE];
static VkRenderingInfo                  current_rendering_info;
static VkRenderingAttachmentInfo        current_color_attachment_infos[8];
static VkRenderingAttachmentInfo        current_depth_attachment_info;
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkSampler                        linear_sampler;
static VkSampler                        point_sampler;
static bool                             linear_sampling = true;
static VkCommandBuffer                  command_buffer;
static VkBuffer                         buffers_to_destroy[512];
static VkDeviceMemory                   buffer_memories_to_destroy[512];
static int                              buffers_to_destroy_count = 0;
static char                             device_name[256];

static VkInstance       instance;
static VkPhysicalDevice gpu;
static VkDevice         device;
static VkCommandPool    cmd_pool;
static VkQueue          queue;
#ifndef NDEBUG
static bool                     validation_found;
static VkDebugUtilsMessengerEXT debug_messenger;
#endif

static bool               surface_destroyed;
static int                window_depth_bits;
static bool               window_vsync;
static VkSurfaceKHR       surface;
static VkSurfaceFormatKHR surface_format;
static VkSwapchainKHR     swapchain;
static VkImage            window_images[GPU_FRAMEBUFFER_COUNT];
static bool               framebuffer_acquired = false;
static bool               framebuffer_undefined[GPU_FRAMEBUFFER_COUNT];
static bool               framebuffer_wait_pending = false;
static VkBuffer           readback_buffer;
static int                readback_buffer_size = 0;
static VkDeviceMemory     readback_mem;
static VkBuffer           upload_buffer;
static uint32_t           upload_buffer_size = 0;
static VkDeviceMemory     upload_mem;
static bool               is_amd = false;
#ifdef IRON_ANDROID
static bool unified_memory = true;
#else
static bool unified_memory = false;
#endif

void     iron_vulkan_get_instance_extensions(const char **extensions, int *index);
VkBool32 iron_vulkan_get_physical_device_presentation_support(VkPhysicalDevice physical_device, uint32_t queue_family_index);
VkResult iron_vulkan_create_surface(VkInstance instance, VkSurfaceKHR *surface);

static VkFormat convert_image_format(gpu_texture_format_t format) {
	switch (format) {
	case GPU_TEXTURE_FORMAT_RGBA128:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case GPU_TEXTURE_FORMAT_RGBA64:
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	case GPU_TEXTURE_FORMAT_R8:
		return VK_FORMAT_R8_UNORM;
	case GPU_TEXTURE_FORMAT_R16:
		return VK_FORMAT_R16_SFLOAT;
	case GPU_TEXTURE_FORMAT_R32:
		return VK_FORMAT_R32_SFLOAT;
	case GPU_TEXTURE_FORMAT_D32:
		return VK_FORMAT_D32_SFLOAT;
	default:
#ifdef IRON_ANDROID
		return VK_FORMAT_R8G8B8A8_UNORM;
#else
		return VK_FORMAT_B8G8R8A8_UNORM;
#endif
	}
}

static VkCullModeFlagBits convert_cull_mode(gpu_cull_mode_t cull_mode) {
	switch (cull_mode) {
	case GPU_CULL_MODE_CLOCKWISE:
		return VK_CULL_MODE_BACK_BIT;
	case GPU_CULL_MODE_COUNTER_CLOCKWISE:
		return VK_CULL_MODE_FRONT_BIT;
	default:
		return VK_CULL_MODE_NONE;
	}
}

static VkCompareOp convert_compare_mode(gpu_compare_mode_t compare) {
	switch (compare) {
	default:
	case GPU_COMPARE_MODE_ALWAYS:
		return VK_COMPARE_OP_ALWAYS;
	case GPU_COMPARE_MODE_NEVER:
		return VK_COMPARE_OP_NEVER;
	case GPU_COMPARE_MODE_EQUAL:
		return VK_COMPARE_OP_EQUAL;
	case GPU_COMPARE_MODE_LESS:
		return VK_COMPARE_OP_LESS;
	}
}

static VkBlendFactor convert_blend_factor(gpu_blend_t factor) {
	switch (factor) {
	case GPU_BLEND_ONE:
		return VK_BLEND_FACTOR_ONE;
	case GPU_BLEND_ZERO:
		return VK_BLEND_FACTOR_ZERO;
	case GPU_BLEND_SOURCE_ALPHA:
		return VK_BLEND_FACTOR_SRC_ALPHA;
	case GPU_BLEND_DEST_ALPHA:
		return VK_BLEND_FACTOR_DST_ALPHA;
	case GPU_BLEND_INV_SOURCE_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GPU_BLEND_INV_DEST_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	}
}

static VkImageLayout convert_texture_state(gpu_texture_state_t state) {
	switch (state) {
	case GPU_TEXTURE_STATE_SHADER_RESOURCE:
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	case GPU_TEXTURE_STATE_RENDER_TARGET:
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	case GPU_TEXTURE_STATE_RENDER_TARGET_DEPTH:
		return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	case GPU_TEXTURE_STATE_PRESENT:
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}
}

static VkBool32 vk_debug_utils_messenger_callback_ext(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_types,
                                                      const VkDebugUtilsMessengerCallbackDataEXT *pcallback_data, void *puser_data) {
	if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		iron_error("Vulkan ERROR: Code %d : %s\n", pcallback_data->messageIdNumber, pcallback_data->pMessage);
	}
	else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		iron_log("Vulkan WARNING: Code %d : %s\n", pcallback_data->messageIdNumber, pcallback_data->pMessage);
	}
	return VK_FALSE;
}

static bool check_extensions(const char **wanted_extensions, int wanted_extension_count, VkExtensionProperties *extensions, int extension_count) {
	bool *found_extensions = calloc(wanted_extension_count, 1);
	for (int i = 0; i < extension_count; i++) {
		for (int i2 = 0; i2 < wanted_extension_count; i2++) {
			if (strcmp(wanted_extensions[i2], extensions[i].extensionName) == 0) {
				found_extensions[i2] = true;
			}
		}
	}

	bool missing_extensions = false;
	for (int i = 0; i < wanted_extension_count; i++) {
		if (!found_extensions[i]) {
			iron_error("Failed to find extension %s", wanted_extensions[i]);
			missing_extensions = true;
		}
	}
	free(found_extensions);
	return missing_extensions;
}

static bool find_layer(VkLayerProperties *layers, int layer_count, const char *wanted_layer) {
	for (int i = 0; i < layer_count; i++) {
		if (strcmp(wanted_layer, layers[i].layerName) == 0) {
			return true;
		}
	}
	return false;
}

static uint32_t memory_type_from_properties(uint32_t type_bits, VkFlags requirements_mask) {
	uint32_t     best_index = 0;
	VkDeviceSize best_size  = 0;
	for (uint32_t i = 0; i < 32; i++) {
		if ((type_bits & 1) == 1) {
			if (is_amd && memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) {
				continue;
			}
			if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
				uint32_t     heap_index = memory_properties.memoryTypes[i].heapIndex;
				VkDeviceSize heap_size  = memory_properties.memoryHeaps[heap_index].size;
				if (heap_size > best_size) {
					best_size  = heap_size;
					best_index = i;
				}
			}
		}
		type_bits >>= 1;
	}
	return best_index;
}

static VkAccessFlags access_mask(VkImageLayout layout) {
	if (layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		return VK_ACCESS_TRANSFER_READ_BIT;
	}
	if (layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		return VK_ACCESS_MEMORY_READ_BIT;
	}
	return 0;
}

void gpu_barrier(gpu_texture_t *render_target, gpu_texture_state_t state_after) {
	if (render_target->state == state_after) {
		return;
	}

	VkImageLayout old_layout = convert_texture_state(render_target->state);
	for (int i = 0; i < GPU_FRAMEBUFFER_COUNT; ++i) {
		if (framebuffer_undefined[i] && render_target == &framebuffers[i]) {
			old_layout               = VK_IMAGE_LAYOUT_UNDEFINED;
			framebuffer_undefined[i] = false;
			break;
		}
	}

	VkImageMemoryBarrier barrier = {
	    .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = access_mask(old_layout),
	    .dstAccessMask = access_mask(convert_texture_state(state_after)),
	    .oldLayout     = old_layout,
	    .newLayout     = convert_texture_state(state_after),
	    .image         = render_target->impl.image,
	    .subresourceRange =
	        {
	            .aspectMask     = render_target->format == GPU_TEXTURE_FORMAT_D32 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
	            .baseMipLevel   = 0,
	            .levelCount     = 1,
	            .baseArrayLayer = 0,
	            .layerCount     = 1,
	        },
	};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

	render_target->state = state_after;
}

static void set_image_layout(VkImage image, VkImageAspectFlags aspect_mask, VkImageLayout old_layout, VkImageLayout new_layout) {
	if (gpu_in_use) {
		vkCmdEndRendering(command_buffer);
	}

	VkImageMemoryBarrier barrier = {
	    .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask                   = 0,
	    .dstAccessMask                   = 0,
	    .oldLayout                       = old_layout,
	    .newLayout                       = new_layout,
	    .image                           = image,
	    .subresourceRange.aspectMask     = aspect_mask,
	    .subresourceRange.baseMipLevel   = 0,
	    .subresourceRange.levelCount     = 1,
	    .subresourceRange.baseArrayLayer = 0,
	    .subresourceRange.layerCount     = 1,
	};

	if (new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}
	if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}
	if (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
		barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

	if (gpu_in_use) {
		vkCmdBeginRendering(command_buffer, &current_rendering_info);
	}
}

static void create_descriptors(void) {
	VkDescriptorSetLayoutBinding bindings[18];
	memset(bindings, 0, sizeof(bindings));

	bindings[0].binding         = 0;
	bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding         = 1;
	bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	for (int i = 2; i < 2 + GPU_MAX_TEXTURES; ++i) {
		bindings[i].binding         = i;
		bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo layout_create_info = {
	    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	    .bindingCount = 2 + GPU_MAX_TEXTURES,
	    .pBindings    = bindings,
	};

	vkCreateDescriptorSetLayout(device, &layout_create_info, NULL, &descriptor_layout);

	VkDescriptorPoolSize type_counts[3];
	memset(type_counts, 0, sizeof(type_counts));

	type_counts[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	type_counts[0].descriptorCount = GPU_CONSTANT_BUFFER_MULTIPLE;
	type_counts[1].type            = VK_DESCRIPTOR_TYPE_SAMPLER;
	type_counts[1].descriptorCount = GPU_CONSTANT_BUFFER_MULTIPLE;
	type_counts[2].type            = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	type_counts[2].descriptorCount = GPU_CONSTANT_BUFFER_MULTIPLE * GPU_MAX_TEXTURES;

	VkDescriptorPoolCreateInfo pool_info = {
	    .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .maxSets       = GPU_CONSTANT_BUFFER_MULTIPLE,
	    .poolSizeCount = 3,
	    .pPoolSizes    = type_counts,
	};

	VkDescriptorPool descriptor_pool;
	vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool);

	VkDescriptorSetLayout layouts[GPU_CONSTANT_BUFFER_MULTIPLE];
	for (int i = 0; i < GPU_CONSTANT_BUFFER_MULTIPLE; ++i) {
		layouts[i] = descriptor_layout;
	}

	VkDescriptorSetAllocateInfo alloc_info = {
	    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	    .descriptorPool     = descriptor_pool,
	    .descriptorSetCount = GPU_CONSTANT_BUFFER_MULTIPLE,
	    .pSetLayouts        = layouts,
	};
	vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets);

	VkSamplerCreateInfo sampler_info = {
	    .sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	    .magFilter     = VK_FILTER_LINEAR,
	    .minFilter     = VK_FILTER_LINEAR,
	    .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	    .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	    .addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	    .maxAnisotropy = 1.0f,
	    .borderColor   = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
	    .compareOp     = VK_COMPARE_OP_ALWAYS,
	    .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
	};
	vkCreateSampler(device, &sampler_info, NULL, &linear_sampler);
	sampler_info.magFilter  = VK_FILTER_NEAREST;
	sampler_info.minFilter  = VK_FILTER_NEAREST;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	vkCreateSampler(device, &sampler_info, NULL, &point_sampler);
}

VkSwapchainKHR cleanup_swapchain() {
	// for (int i = 0; i < GPU_FRAMEBUFFER_COUNT; ++i) {
	// 	gpu_texture_destroy_internal(&framebuffers[i]);
	// }
	VkSwapchainKHR chain = swapchain;
	swapchain            = VK_NULL_HANDLE;
	return chain;
}

static void gpu_cleanup_internal() {
	while (buffers_to_destroy_count > 0) {
		buffers_to_destroy_count--;
		vkFreeMemory(device, buffer_memories_to_destroy[buffers_to_destroy_count], NULL);
		vkDestroyBuffer(device, buffers_to_destroy[buffers_to_destroy_count], NULL);
	}
}

void gpu_render_target_init2(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format, int framebuffer_index) {
	target->width     = width;
	target->height    = height;
	target->format    = format;
	target->state     = (framebuffer_index >= 0) ? GPU_TEXTURE_STATE_PRESENT : GPU_TEXTURE_STATE_SHADER_RESOURCE;
	target->buffer    = NULL;
	target->gpu_write = false;

	if (framebuffer_index >= 0) {
		return;
	}

	VkFormatProperties format_properties;
	vkGetPhysicalDeviceFormatProperties(gpu, convert_image_format(target->format), &format_properties);

	VkImageCreateInfo image = {
	    .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType     = VK_IMAGE_TYPE_2D,
	    .format        = convert_image_format(target->format),
	    .extent.width  = width,
	    .extent.height = height,
	    .extent.depth  = 1,
	    .mipLevels     = 1,
	    .arrayLayers   = 1,
	    .samples       = VK_SAMPLE_COUNT_1_BIT,
	    .tiling        = VK_IMAGE_TILING_OPTIMAL,
	};

	if (format == GPU_TEXTURE_FORMAT_D32) {
		image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	else {
		image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	VkImageViewCreateInfo color_image_view = {
	    .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .viewType                        = VK_IMAGE_VIEW_TYPE_2D,
	    .format                          = convert_image_format(target->format),
	    .subresourceRange.aspectMask     = format == GPU_TEXTURE_FORMAT_D32 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
	    .subresourceRange.baseMipLevel   = 0,
	    .subresourceRange.levelCount     = 1,
	    .subresourceRange.baseArrayLayer = 0,
	    .subresourceRange.layerCount     = 1,
	};

	vkCreateImage(device, &image, NULL, &target->impl.image);
	VkMemoryRequirements memory_reqs;
	vkGetImageMemoryRequirements(device, target->impl.image, &memory_reqs);

	VkMemoryAllocateInfo allocation_nfo = {
	    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = memory_reqs.size,
	};
	allocation_nfo.memoryTypeIndex = memory_type_from_properties(memory_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkResult result                = vkAllocateMemory(device, &allocation_nfo, NULL, &target->impl.mem);

	if (result != VK_SUCCESS && gpu_cleanup_pending()) {
		gpu_execute_and_wait();
		gpu_cleanup_internal();
		gpu_cleanup();
		gpu_render_target_init2(target, width, height, format, framebuffer_index);
		return;
	}

	vkBindImageMemory(device, target->impl.image, target->impl.mem, 0);
	set_image_layout(target->impl.image, format == GPU_TEXTURE_FORMAT_D32 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
	                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	color_image_view.image = target->impl.image;
	vkCreateImageView(device, &color_image_view, NULL, &target->impl.view);
}

static uint32_t umin(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

static uint32_t umax(uint32_t a, uint32_t b) {
	return a > b ? a : b;
}

static void create_swapchain() {
	VkSwapchainKHR old_swapchain = cleanup_swapchain();
	if (surface_destroyed) {
		vkDestroySwapchainKHR(device, old_swapchain, NULL);
		old_swapchain = VK_NULL_HANDLE;
		vkDestroySurfaceKHR(instance, surface, NULL);
		iron_vulkan_create_surface(instance, &surface);
		surface_destroyed = false;
	}

	VkSurfaceCapabilitiesKHR caps = {0};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);

	VkPresentModeKHR present_modes[256];
	uint32_t         present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &present_mode_count, NULL);
	present_mode_count = present_mode_count > 256 ? 256 : present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &present_mode_count, present_modes);

	uint32_t image_count = GPU_FRAMEBUFFER_COUNT;
	if (image_count < caps.minImageCount) {
		image_count = caps.minImageCount;
	}
	else if (image_count > caps.maxImageCount && caps.maxImageCount > 0) {
		image_count = caps.maxImageCount;
	}

	VkSurfaceTransformFlagBitsKHR pre_transform = {0};
	if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
		pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else {
		pre_transform = caps.currentTransform;
	}

	// Fetch newest window size
	iron_internal_handle_messages();

	VkExtent2D image_extent = caps.currentExtent;
	if (caps.currentExtent.width == UINT32_MAX) {
		image_extent.width  = umax(caps.minImageExtent.width, umin(iron_window_width(), caps.maxImageExtent.width));
		image_extent.height = umax(caps.minImageExtent.height, umin(iron_window_height(), caps.maxImageExtent.height));
	}

	VkSwapchainCreateInfoKHR swapchain_info = {
	    .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .surface         = surface,
	    .minImageCount   = image_count,
	    .imageFormat     = surface_format.format,
	    .imageColorSpace = surface_format.colorSpace,
	    .imageExtent     = image_extent,
	    .imageUsage      = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	    .preTransform    = pre_transform,
	};

	if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
		swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	}
	else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
		swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	}
	else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
		swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
	}
	else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
		swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
	}

	swapchain_info.imageArrayLayers      = 1;
	swapchain_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_info.queueFamilyIndexCount = 0;
	swapchain_info.pQueueFamilyIndices   = NULL;
	swapchain_info.presentMode           = window_vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
	swapchain_info.oldSwapchain          = old_swapchain;
	swapchain_info.clipped               = true;

	vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain);

	if (old_swapchain != VK_NULL_HANDLE) {
		gpu_execute_and_wait();
		vkQueueWaitIdle(queue);
		vkDestroySwapchainKHR(device, old_swapchain, NULL);
	}

	uint32_t framebuffer_count = GPU_FRAMEBUFFER_COUNT;
	// vkGetSwapchainImagesKHR(device, swapchain, &framebuffer_count, NULL);
	vkGetSwapchainImagesKHR(device, swapchain, &framebuffer_count, window_images);

	for (uint32_t i = 0; i < framebuffer_count; i++) {
		framebuffers[i].impl.image = window_images[i];
		framebuffers[i].state      = GPU_TEXTURE_STATE_PRESENT;
		framebuffer_undefined[i]   = true;

		VkImageViewCreateInfo color_attachment_view = {
		    .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		    .format                          = surface_format.format,
		    .components.r                    = VK_COMPONENT_SWIZZLE_R,
		    .components.g                    = VK_COMPONENT_SWIZZLE_G,
		    .components.b                    = VK_COMPONENT_SWIZZLE_B,
		    .components.a                    = VK_COMPONENT_SWIZZLE_A,
		    .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		    .subresourceRange.baseMipLevel   = 0,
		    .subresourceRange.levelCount     = 1,
		    .subresourceRange.baseArrayLayer = 0,
		    .subresourceRange.layerCount     = 1,
		    .viewType                        = VK_IMAGE_VIEW_TYPE_2D,
		    .flags                           = 0,
		    .image                           = window_images[i],
		};
		vkCreateImageView(device, &color_attachment_view, NULL, &framebuffers[i].impl.view);
		// gpu_texture_destroy_internal(&framebuffers[i]);
		// gpu_render_target_init2(&framebuffers[i], iron_window_width(), iron_window_height(), GPU_TEXTURE_FORMAT_RGBA32, i);
		framebuffers[i].width  = image_extent.width;
		framebuffers[i].height = image_extent.height;
	}

	framebuffer_index = 0;

	if (window_depth_bits > 0) {
		VkImageCreateInfo image = {
		    .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		    .imageType     = VK_IMAGE_TYPE_2D,
		    .format        = VK_FORMAT_D32_SFLOAT,
		    .extent.width  = image_extent.width,
		    .extent.height = image_extent.height,
		    .extent.depth  = 1,
		    .mipLevels     = 1,
		    .arrayLayers   = 1,
		    .samples       = VK_SAMPLE_COUNT_1_BIT,
		    .tiling        = VK_IMAGE_TILING_OPTIMAL,
		    .usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		    .flags         = 0,
		};

		VkMemoryAllocateInfo mem_alloc = {
		    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		};

		VkMemoryRequirements mem_reqs = {0};
		vkCreateImage(device, &image, NULL, &framebuffer_depth.impl.image);
		vkGetImageMemoryRequirements(device, framebuffer_depth.impl.image, &mem_reqs);
		mem_alloc.allocationSize  = mem_reqs.size;
		mem_alloc.memoryTypeIndex = memory_type_from_properties(mem_reqs.memoryTypeBits, 0);
		vkAllocateMemory(device, &mem_alloc, NULL, &framebuffer_depth.impl.mem);
		vkBindImageMemory(device, framebuffer_depth.impl.image, framebuffer_depth.impl.mem, 0);
		set_image_layout(framebuffer_depth.impl.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkImageViewCreateInfo view = {
		    .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		    .image                           = framebuffer_depth.impl.image,
		    .format                          = VK_FORMAT_D32_SFLOAT,
		    .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
		    .subresourceRange.baseMipLevel   = 0,
		    .subresourceRange.levelCount     = 1,
		    .subresourceRange.baseArrayLayer = 0,
		    .subresourceRange.layerCount     = 1,
		    .viewType                        = VK_IMAGE_VIEW_TYPE_2D,
		};
		vkCreateImageView(device, &view, NULL, &framebuffer_depth.impl.view);
	}
}

static void acquire_next_image() {
	VkResult err = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, framebuffer_available_semaphore, VK_NULL_HANDLE, (uint32_t *)&framebuffer_index);
	if (err == VK_ERROR_SURFACE_LOST_KHR || err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR || surface_destroyed) {
		surface_destroyed        = surface_destroyed || (err == VK_ERROR_SURFACE_LOST_KHR);
		framebuffer_wait_pending = false;
		gpu_in_use               = false;
		create_swapchain();
		gpu_in_use = true;
		acquire_next_image();
		return;
	}
	framebuffer_wait_pending = true;
}

void gpu_resize_internal(int width, int height) {
	// Newest window size is fetched in create_swapchain
}

void gpu_init_internal(int depth_buffer_bits, bool vsync) {
	uint32_t instance_layer_count = 0;

	static const char *wanted_instance_layers[64];
	int                wanted_instance_layer_count = 0;

	vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);

	if (instance_layer_count > 0) {
		VkLayerProperties *instance_layers = (VkLayerProperties *)malloc(sizeof(VkLayerProperties) * instance_layer_count);
		vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers);

#ifndef NDEBUG
		validation_found = find_layer(instance_layers, instance_layer_count, "VK_LAYER_KHRONOS_validation");
		if (validation_found) {
			iron_log("Running with Vulkan validation layers enabled.");
			wanted_instance_layers[wanted_instance_layer_count++] = "VK_LAYER_KHRONOS_validation";
		}
#endif

		free(instance_layers);
	}

	static const char *wanted_instance_extensions[64];
	int                wanted_instance_extension_count            = 0;
	uint32_t           instance_extension_count                   = 0;
	wanted_instance_extensions[wanted_instance_extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
	wanted_instance_extensions[wanted_instance_extension_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
	iron_vulkan_get_instance_extensions(wanted_instance_extensions, &wanted_instance_extension_count);

	vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);
	VkExtensionProperties *instance_extensions = (VkExtensionProperties *)malloc(sizeof(VkExtensionProperties) * instance_extension_count);
	vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, instance_extensions);
	bool missing_instance_extensions =
	    check_extensions(wanted_instance_extensions, wanted_instance_extension_count, instance_extensions, instance_extension_count);

	if (missing_instance_extensions) {
		iron_error("");
	}

#ifndef NDEBUG
	// this extension should be provided by the validation layers
	if (validation_found) {
		wanted_instance_extensions[wanted_instance_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
#endif

	VkApplicationInfo app = {
	    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pApplicationName   = iron_application_name(),
	    .applicationVersion = 0,
	    .pEngineName        = "Iron",
	    .engineVersion      = 0,
	    .apiVersion         = VK_API_VERSION_1_3,
	};

	VkInstanceCreateInfo info = {0};
	info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo     = &app;
#ifndef NDEBUG
	if (validation_found) {
		info.enabledLayerCount   = wanted_instance_layer_count;
		info.ppEnabledLayerNames = (const char *const *)wanted_instance_layers;
	}
	else
#endif
	{
		info.enabledLayerCount   = 0;
		info.ppEnabledLayerNames = NULL;
	}
	info.enabledExtensionCount   = wanted_instance_extension_count;
	info.ppEnabledExtensionNames = (const char *const *)wanted_instance_extensions;

	VkResult err = vkCreateInstance(&info, NULL, &instance);
	if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
		iron_error("Vulkan driver is incompatible");
	}
	else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
		iron_error("Vulkan extension not found");
	}
	else if (err) {
		iron_error("Can not create Vulkan instance");
	}

	uint32_t gpu_count;
	vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);

	if (gpu_count > 0) {
		VkPhysicalDevice *physical_devices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * gpu_count);
		vkEnumeratePhysicalDevices(instance, &gpu_count, physical_devices);

		float best_score = 0.0;
		for (uint32_t gpu_idx = 0; gpu_idx < gpu_count; gpu_idx++) {
			VkPhysicalDevice current_gpu = physical_devices[gpu_idx];
			uint32_t         queue_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(current_gpu, &queue_count, NULL);
			VkQueueFamilyProperties *queue_props = (VkQueueFamilyProperties *)malloc(queue_count * sizeof(VkQueueFamilyProperties));
			vkGetPhysicalDeviceQueueFamilyProperties(current_gpu, &queue_count, queue_props);
			bool can_present = false;
			bool can_render  = false;
			for (uint32_t i = 0; i < queue_count; i++) {
				VkBool32 queue_supports_present = iron_vulkan_get_physical_device_presentation_support(current_gpu, i);
				if (queue_supports_present) {
					can_present = true;
				}
				VkQueueFamilyProperties queue_properties = queue_props[i];
				uint32_t                flags            = queue_properties.queueFlags;
				if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0) {
					can_render = true;
				}
			}
			if (!can_present || !can_render) {
				continue;
			}

			float                      score = 0.0;
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(current_gpu, &properties);
			switch (properties.deviceType) {
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
				score = 2;
				break;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			default:
				score = 1;
				break;
			}

			if (gpu == VK_NULL_HANDLE || score > best_score) {
				gpu        = current_gpu;
				best_score = score;
			}
		}

		if (gpu == VK_NULL_HANDLE) {
			iron_error("No Vulkan device that supports presentation found");
		}

		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(gpu, &properties);
		iron_log("Chosen Vulkan device: %s", properties.deviceName);
		strcpy(device_name, properties.deviceName);
		is_amd = properties.vendorID == 0x1002;
		free(physical_devices);
	}
	else {
		iron_error("No Vulkan device found");
	}

	const char *wanted_device_extensions[64];
	int         wanted_device_extension_count = 0;

	wanted_device_extensions[wanted_device_extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

	if (gpu_raytrace_supported()) {
		wanted_device_extensions[wanted_device_extension_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
		wanted_device_extensions[wanted_device_extension_count++] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
		wanted_device_extensions[wanted_device_extension_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
		wanted_device_extensions[wanted_device_extension_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
	}

	uint32_t device_extension_count = 0;
	vkEnumerateDeviceExtensionProperties(gpu, NULL, &device_extension_count, NULL);

	VkExtensionProperties *device_extensions = (VkExtensionProperties *)malloc(sizeof(VkExtensionProperties) * device_extension_count);
	vkEnumerateDeviceExtensionProperties(gpu, NULL, &device_extension_count, device_extensions);

	bool missing_device_extensions = check_extensions(wanted_device_extensions, wanted_device_extension_count, device_extensions, device_extension_count);
	free(device_extensions);

	if (missing_device_extensions) {
		exit(1);
	}

#ifndef NDEBUG
	if (validation_found) {
		VkDebugUtilsMessengerCreateInfoEXT create_info = {
		    .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		    .pfnUserCallback = vk_debug_utils_messenger_callback_ext,
		    .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
		    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
		};
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
		    (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		vkCreateDebugUtilsMessengerEXT(instance, &create_info, NULL, &debug_messenger);
	}
#endif

	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, NULL);

	VkQueueFamilyProperties *queue_props = (VkQueueFamilyProperties *)malloc(queue_count * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props);

	VkBool32 *supports_present = (VkBool32 *)malloc(queue_count * sizeof(VkBool32));
	for (uint32_t i = 0; i < queue_count; i++) {
		supports_present[i] = iron_vulkan_get_physical_device_presentation_support(gpu, i);
	}

	uint32_t graphics_queue_node_index = UINT32_MAX;
	uint32_t present_queue_node_index  = UINT32_MAX;
	for (uint32_t i = 0; i < queue_count; i++) {
		if ((queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			if (graphics_queue_node_index == UINT32_MAX) {
				graphics_queue_node_index = i;
			}

			if (supports_present[i] == VK_TRUE) {
				graphics_queue_node_index = i;
				present_queue_node_index  = i;
				break;
			}
		}
	}
	if (present_queue_node_index == UINT32_MAX) {
		for (uint32_t i = 0; i < queue_count; ++i) {
			if (supports_present[i] == VK_TRUE) {
				present_queue_node_index = i;
				break;
			}
		}
	}
	free(supports_present);

	if (graphics_queue_node_index == UINT32_MAX || present_queue_node_index == UINT32_MAX) {
		iron_error("Graphics or present queue not found");
	}

	if (graphics_queue_node_index != present_queue_node_index) {
		iron_error("Graphics and present queue do not match");
	}

	{
		float                   queue_priorities[1] = {0.0};
		VkDeviceQueueCreateInfo queue               = {
		                  .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		                  .queueFamilyIndex = graphics_queue_node_index,
		                  .queueCount       = 1,
		                  .pQueuePriorities = queue_priorities,
        };

		VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
		    .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
		    .dynamicRendering = VK_TRUE,
		};

		VkPhysicalDeviceFeatures enabled_features = {};
		enabled_features.independentBlend         = VK_TRUE;

		VkDeviceCreateInfo deviceinfo = {
		    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		    .pNext                   = &dynamic_rendering_features,
		    .queueCreateInfoCount    = 1,
		    .pQueueCreateInfos       = &queue,
		    .enabledLayerCount       = 0,
		    .ppEnabledLayerNames     = NULL,
		    .enabledExtensionCount   = wanted_device_extension_count,
		    .ppEnabledExtensionNames = (const char *const *)wanted_device_extensions,
		    .pEnabledFeatures        = &enabled_features,
		};

		VkPhysicalDeviceAccelerationStructureFeaturesKHR raytracing_acceleration_structure_ext = {0};
		VkPhysicalDeviceBufferDeviceAddressFeatures      buffer_device_address_ext             = {0};
		VkPhysicalDeviceRayQueryFeaturesKHR              ray_query_ext                         = {0};
		if (gpu_raytrace_supported()) {
			raytracing_acceleration_structure_ext.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			raytracing_acceleration_structure_ext.pNext                 = deviceinfo.pNext;
			raytracing_acceleration_structure_ext.accelerationStructure = VK_TRUE;

			buffer_device_address_ext.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
			buffer_device_address_ext.pNext               = &raytracing_acceleration_structure_ext;
			buffer_device_address_ext.bufferDeviceAddress = VK_TRUE;

			ray_query_ext.sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
			ray_query_ext.pNext    = &buffer_device_address_ext;
			ray_query_ext.rayQuery = VK_TRUE;

			deviceinfo.pNext = &ray_query_ext;
		}

		vkCreateDevice(gpu, &deviceinfo, NULL, &device);
	}

	vkGetDeviceQueue(device, graphics_queue_node_index, 0, &queue);
	vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);
	VkCommandPoolCreateInfo cmd_pool_info = {
	    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	    .queueFamilyIndex = graphics_queue_node_index,
	    .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};

	vkCreateCommandPool(device, &cmd_pool_info, NULL, &cmd_pool);

	create_descriptors();

	VkSemaphoreCreateInfo sem_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	    .flags = 0,
	};

	vkCreateSemaphore(device, &sem_info, NULL, &framebuffer_available_semaphore);
	for (uint32_t i = 0; i < GPU_FRAMEBUFFER_COUNT; i++) {
		vkCreateSemaphore(device, &sem_info, NULL, &rendering_finished_semaphores[i]);
	}

	window_depth_bits = depth_buffer_bits;
	window_vsync      = vsync;

	iron_vulkan_create_surface(instance, &surface);

	VkBool32 surface_supported;
	vkGetPhysicalDeviceSurfaceSupportKHR(gpu, graphics_queue_node_index, surface, &surface_supported);

	VkSurfaceFormatKHR surf_formats[256];
	uint32_t           format_count = sizeof(surf_formats) / sizeof(surf_formats[0]);
	VkResult           result       = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, surf_formats);

	if (format_count == 1 && surf_formats[0].format == VK_FORMAT_UNDEFINED) {
		surface_format = surf_formats[0];
	}
	else {
		bool found = false;
		for (uint32_t i = 0; i < format_count; ++i) {
			if (surf_formats[i].format != VK_FORMAT_B8G8R8A8_SRGB) {
				surface_format = surf_formats[i];
				found          = true;
				break;
			}
		}
		if (!found) {
			surface_format = surf_formats[0];
		}
	}

	VkCommandBufferAllocateInfo cmd = {
	    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool        = cmd_pool,
	    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	    .commandBufferCount = 1,
	};
	vkAllocateCommandBuffers(device, &cmd, &command_buffer);

	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = 0,
	};
	vkBeginCommandBuffer(command_buffer, &begin_info);

	gpu_create_framebuffers(depth_buffer_bits);
	create_swapchain();

	VkFenceCreateInfo fence_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	vkCreateFence(device, &fence_info, NULL, &fence);
}

void iron_vulkan_surface_destroyed() {
	surface_destroyed = true;
}

bool iron_vulkan_get_size(int *width, int *height) {
	if (surface) {
		VkSurfaceCapabilitiesKHR capabilities;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &capabilities);
		*width  = capabilities.currentExtent.width;
		*height = capabilities.currentExtent.height;
		return true;
	}
	return false;
}

void gpu_begin_internal(gpu_clear_t flags, uint32_t color, float depth) {
	if (!framebuffer_acquired) {
		acquire_next_image();
		framebuffer_acquired = true;
	}

	gpu_texture_t *target = current_render_targets[0];

	VkRect2D render_area      = {.offset = {0, 0}};
	render_area.extent.width  = target->width;
	render_area.extent.height = target->height;

	VkClearValue clear_value;
	memset(&clear_value, 0, sizeof(VkClearValue));
	clear_value.color.float32[0] = ((color & 0x00ff0000) >> 16) / 255.0f;
	clear_value.color.float32[1] = ((color & 0x0000ff00) >> 8) / 255.0f;
	clear_value.color.float32[2] = ((color & 0x000000ff)) / 255.0f;
	clear_value.color.float32[3] = ((color & 0xff000000) >> 24) / 255.0f;

	for (size_t i = 0; i < current_render_targets_count; ++i) {
		current_color_attachment_infos[i] = (VkRenderingAttachmentInfo){
		    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		    .imageView          = current_render_targets[i]->impl.view,
		    .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		    .resolveMode        = VK_RESOLVE_MODE_NONE,
		    .resolveImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		    .loadOp             = (flags & GPU_CLEAR_COLOR) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
		    .clearValue         = clear_value,
		};
	}

	if (current_depth_buffer != NULL) {
		current_depth_attachment_info = (VkRenderingAttachmentInfo){
		    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		    .imageView          = current_depth_buffer->impl.view,
		    .imageLayout        = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		    .resolveMode        = VK_RESOLVE_MODE_NONE,
		    .resolveImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		    .loadOp             = (flags & GPU_CLEAR_DEPTH) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
		    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
		    .clearValue         = depth,
		};
	}

	current_rendering_info = (VkRenderingInfo){
	    .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
	    .renderArea           = render_area,
	    .layerCount           = 1,
	    .viewMask             = 0,
	    .colorAttachmentCount = (uint32_t)current_render_targets_count,
	    .pColorAttachments    = current_color_attachment_infos,
	    .pDepthAttachment     = current_depth_buffer == NULL ? VK_NULL_HANDLE : &current_depth_attachment_info,
	};
	vkCmdBeginRendering(command_buffer, &current_rendering_info);

	for (size_t i = 0; i < current_render_targets_count; ++i) {
		current_color_attachment_infos[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	if (current_depth_buffer != NULL) {
		current_depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	}

	gpu_viewport(0, 0, current_render_targets[0]->width, current_render_targets[0]->height);
	gpu_scissor(0, 0, current_render_targets[0]->width, current_render_targets[0]->height);
}

void gpu_end_internal() {
	vkCmdEndRendering(command_buffer);

	for (int i = 0; i < current_render_targets_count; ++i) {
		gpu_barrier(current_render_targets[i],
		            current_render_targets[i] == &framebuffers[framebuffer_index] ? GPU_TEXTURE_STATE_PRESENT : GPU_TEXTURE_STATE_SHADER_RESOURCE);
	}
	current_render_targets_count = 0;

	if (is_amd) {
		gpu_execute_and_wait(); ////
	}
}

void gpu_execute_and_wait() {
	if (gpu_in_use) {
		vkCmdEndRendering(command_buffer);
	}
	vkEndCommandBuffer(command_buffer);
	vkResetFences(device, 1, &fence);

	VkSubmitInfo submit_info = {
	    .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers    = &command_buffer,
	};

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (framebuffer_wait_pending) {
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores    = &framebuffer_available_semaphore;
		submit_info.pWaitDstStageMask  = &wait_stage;
		framebuffer_wait_pending       = false;
	}
	vkQueueSubmit(queue, 1, &submit_info, fence);
	vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

	vkResetCommandBuffer(command_buffer, 0);
	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(command_buffer, &begin_info);

	if (gpu_in_use) {
		vkCmdBeginRendering(command_buffer, &current_rendering_info);
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline->impl.pipeline);
		VkBuffer     buffers[1];
		VkDeviceSize offsets[1];
		buffers[0] = current_vb->impl.buf;
		offsets[0] = (VkDeviceSize)(0);
		vkCmdBindVertexBuffers(command_buffer, 0, 1, buffers, offsets);
		vkCmdBindIndexBuffer(command_buffer, current_ib->impl.buf, 0, VK_INDEX_TYPE_UINT32);
		vkCmdSetViewport(command_buffer, 0, 1, &current_viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &current_scissor);
	}
}

void gpu_present_internal() {
	vkEndCommandBuffer(command_buffer);
	vkResetFences(device, 1, &fence);

	VkSubmitInfo submit_info = {
	    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount   = 1,
	    .pCommandBuffers      = &command_buffer,
	    .signalSemaphoreCount = 1,
	    .pSignalSemaphores    = &rendering_finished_semaphores[framebuffer_index],
	};

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (framebuffer_wait_pending) {
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores    = &framebuffer_available_semaphore;
		submit_info.pWaitDstStageMask  = &wait_stage;
		framebuffer_wait_pending       = false;
	}
	vkQueueSubmit(queue, 1, &submit_info, fence);
	vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

	VkPresentInfoKHR present = {
	    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .swapchainCount     = 1,
	    .pSwapchains        = &swapchain,
	    .pImageIndices      = (uint32_t *)&framebuffer_index,
	    .pWaitSemaphores    = &rendering_finished_semaphores[framebuffer_index],
	    .waitSemaphoreCount = 1,
	};
	vkQueuePresentKHR(queue, &present);

	vkResetCommandBuffer(command_buffer, 0);
	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(command_buffer, &begin_info);

	// acquire_next_image(); // Breaks window resize
	framebuffer_acquired = false;
	framebuffer_index    = (framebuffer_index + 1) % GPU_FRAMEBUFFER_COUNT;

	gpu_cleanup_internal();
}

void gpu_draw_internal() {
	vkCmdDrawIndexed(command_buffer, current_ib->count, 1, 0, 0, 0);
}

void gpu_viewport(int x, int y, int width, int height) {
	current_viewport = (VkViewport){
	    .x        = (float)x,
	    .y        = y + (float)height,
	    .width    = (float)width,
	    .height   = (float)-height,
	    .minDepth = (float)0.0f,
	    .maxDepth = (float)1.0f,
	};
	vkCmdSetViewport(command_buffer, 0, 1, &current_viewport);
}

void gpu_scissor(int x, int y, int width, int height) {
	if (width < 0 || height < 0) {
		return;
	}
	current_scissor = (VkRect2D){
	    .offset.x      = x,
	    .offset.y      = y,
	    .extent.width  = width,
	    .extent.height = height,
	};
	vkCmdSetScissor(command_buffer, 0, 1, &current_scissor);
}

void gpu_disable_scissor() {
	current_scissor = (VkRect2D){
	    .extent.width  = current_render_targets[0]->width,
	    .extent.height = current_render_targets[0]->height,
	};
	vkCmdSetScissor(command_buffer, 0, 1, &current_scissor);
}

void gpu_set_pipeline_internal(gpu_pipeline_t *pipeline) {
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		current_textures[i] = NULL;
	}
	if (pipeline->impl.pipeline == NULL) {
		return;
	}
	current_pipeline = pipeline;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline->impl.pipeline);
}

void gpu_set_vertex_buffer(gpu_buffer_t *buffer) {
	current_vb = buffer;
	VkBuffer     buffers[1];
	VkDeviceSize offsets[1];
	buffers[0] = buffer->impl.buf;
	offsets[0] = (VkDeviceSize)(0);
	vkCmdBindVertexBuffers(command_buffer, 0, 1, buffers, offsets);
}

void gpu_set_index_buffer(gpu_buffer_t *buffer) {
	current_ib = buffer;
	vkCmdBindIndexBuffer(command_buffer, buffer->impl.buf, 0, VK_INDEX_TYPE_UINT32);
}

void gpu_get_render_target_pixels(gpu_texture_t *render_target, uint8_t *data) {
	int buffer_size              = render_target->width * render_target->height * gpu_texture_format_size(render_target->format);
	int new_readback_buffer_size = buffer_size;
	if (new_readback_buffer_size < (2048 * 2048 * 4)) {
		new_readback_buffer_size = (2048 * 2048 * 4);
	}
	if (readback_buffer_size < new_readback_buffer_size) {
		if (readback_buffer_size > 0) {
			vkFreeMemory(device, readback_mem, NULL);
			vkDestroyBuffer(device, readback_buffer, NULL);
		}
		readback_buffer_size = new_readback_buffer_size;

		VkBufferCreateInfo buf_info = {
		    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .size  = readback_buffer_size,
		    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		};
		vkCreateBuffer(device, &buf_info, NULL, &readback_buffer);

		VkMemoryRequirements mem_reqs = {0};
		vkGetBufferMemoryRequirements(device, readback_buffer, &mem_reqs);

		VkMemoryAllocateInfo mem_alloc = {0};
		mem_alloc.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_alloc.allocationSize       = mem_reqs.size;
		mem_alloc.memoryTypeIndex =
		    memory_type_from_properties(mem_reqs.memoryTypeBits,
		                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		vkAllocateMemory(device, &mem_alloc, NULL, &readback_mem);
		vkBindBufferMemory(device, readback_buffer, readback_mem, 0);
	}

	set_image_layout(render_target->impl.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	VkBufferImageCopy region;
	region.bufferOffset                    = 0;
	region.bufferRowLength                 = render_target->width;
	region.bufferImageHeight               = render_target->height;
	region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount     = 1;
	region.imageSubresource.mipLevel       = 0;
	region.imageOffset.x                   = 0;
	region.imageOffset.y                   = 0;
	region.imageOffset.z                   = 0;
	region.imageExtent.width               = (uint32_t)render_target->width;
	region.imageExtent.height              = (uint32_t)render_target->height;
	region.imageExtent.depth               = 1;
	if (gpu_in_use) {
		vkCmdEndRendering(command_buffer);
	}
	vkCmdCopyImageToBuffer(command_buffer, render_target->impl.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer, 1, &region);
	if (gpu_in_use) {
		vkCmdBeginRendering(command_buffer, &current_rendering_info);
	}

	set_image_layout(render_target->impl.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	gpu_execute_and_wait();

	// Read buffer
	void *p;
	vkMapMemory(device, readback_mem, 0, VK_WHOLE_SIZE, 0, (void **)&p);
	memcpy(data, p, buffer_size);
	vkUnmapMemory(device, readback_mem);
}

static VkDescriptorSet get_descriptor_set(VkBuffer buffer) {
	VkDescriptorSet descriptor_set = descriptor_sets[constant_buffer_index];

	VkDescriptorBufferInfo buffer_descs[1];
	memset(&buffer_descs, 0, sizeof(buffer_descs));
	buffer_descs[0].buffer = buffer;
	buffer_descs[0].offset = 0;
	buffer_descs[0].range  = GPU_CONSTANT_BUFFER_SIZE;

	VkDescriptorImageInfo tex_desc[GPU_MAX_TEXTURES];
	memset(&tex_desc, 0, sizeof(tex_desc));
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		if (current_textures[i] != NULL) {
			tex_desc[i].imageView   = current_textures[i]->impl.view;
			tex_desc[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	}

	VkWriteDescriptorSet writes[18];
	memset(&writes, 0, sizeof(writes));

	int write_count           = 0;
	writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet          = descriptor_set;
	writes[0].dstBinding      = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writes[0].pBufferInfo     = &buffer_descs[0];
	write_count++;

	VkDescriptorImageInfo sampler_info = {
	    .sampler = linear_sampling ? linear_sampler : point_sampler,
	};
	writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet          = descriptor_set;
	writes[1].dstBinding      = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
	writes[1].pImageInfo      = &sampler_info;
	write_count++;

	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		if (current_textures[i] != NULL) {
			writes[2 + i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[2 + i].dstSet          = descriptor_set;
			writes[2 + i].dstBinding      = i + 2;
			writes[2 + i].descriptorCount = 1;
			writes[2 + i].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			writes[2 + i].pImageInfo      = &tex_desc[i];
			write_count++;
		}
	}

	vkUpdateDescriptorSets(device, write_count, writes, 0, NULL);
	return descriptor_set;
}

void gpu_set_constant_buffer(gpu_buffer_t *buffer, uint32_t offset, size_t size) {
	VkDescriptorSet descriptor_set = get_descriptor_set(buffer->impl.buf);
	uint32_t        offsets[1]     = {offset};
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline->impl.pipeline_layout, 0, 1, &descriptor_set, 1, offsets);
}

void gpu_set_texture(uint32_t unit, gpu_texture_t *texture) {
	current_textures[unit] = texture;
}

void gpu_use_linear_sampling(bool b) {
	linear_sampling = b;
}

void gpu_pipeline_destroy_internal(gpu_pipeline_t *pipeline) {
	vkDestroyPipeline(device, pipeline->impl.pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline->impl.pipeline_layout, NULL);
}

static VkShaderModule create_shader_module(const void *code, size_t size) {
	VkShaderModuleCreateInfo module_create_info = {0};
	module_create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_create_info.codeSize                 = size;
	module_create_info.pCode                    = (const uint32_t *)code;
	VkShaderModule module;
	vkCreateShaderModule(device, &module_create_info, NULL, &module);
	return module;
}

void gpu_pipeline_compile(gpu_pipeline_t *pipeline) {
	if (pipeline->vertex_shader->impl.length == 0 || pipeline->fragment_shader->impl.length == 0) {
		// Shader compilation error
		return;
	}

	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
	    .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .setLayoutCount = 1,
	    .pSetLayouts    = &descriptor_layout,
	};
	vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline->impl.pipeline_layout);

	VkGraphicsPipelineCreateInfo           pipeline_info = {0};
	VkPipelineInputAssemblyStateCreateInfo ia            = {0};
	VkPipelineRasterizationStateCreateInfo rs            = {0};
	VkPipelineColorBlendStateCreateInfo    cb            = {0};
	VkPipelineDepthStencilStateCreateInfo  ds            = {0};
	VkPipelineViewportStateCreateInfo      vp            = {0};
	VkPipelineMultisampleStateCreateInfo   ms            = {0};
	VkDynamicState                         dynamic_state[2];
	VkPipelineDynamicStateCreateInfo       dynamic_state_create_info = {0};

	memset(dynamic_state, 0, sizeof(dynamic_state));
	dynamic_state_create_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state_create_info.pDynamicStates = dynamic_state;

	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType  = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.layout = pipeline->impl.pipeline_layout;

	VkVertexInputBindingDescription   vi_bindings[1];
	int                               vertexAttributeCount = pipeline->input_layout->size;
	VkVertexInputAttributeDescription vi_attrs[vertexAttributeCount];

	VkPipelineVertexInputStateCreateInfo vi = {
	    .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	    .vertexBindingDescriptionCount   = 1,
	    .pVertexBindingDescriptions      = vi_bindings,
	    .vertexAttributeDescriptionCount = vertexAttributeCount,
	    .pVertexAttributeDescriptions    = vi_attrs,
	};

	uint32_t attr   = 0;
	uint32_t offset = 0;
	for (int i = 0; i < pipeline->input_layout->size; ++i) {
		gpu_vertex_element_t element = pipeline->input_layout->elements[i];
		vi_attrs[attr].binding       = 0;
		vi_attrs[attr].location      = i;
		vi_attrs[attr].offset        = offset;
		offset += gpu_vertex_data_size(element.data);

		switch (element.data) {
		case GPU_VERTEX_DATA_F32_1X:
			vi_attrs[attr].format = VK_FORMAT_R32_SFLOAT;
			break;
		case GPU_VERTEX_DATA_F32_2X:
			vi_attrs[attr].format = VK_FORMAT_R32G32_SFLOAT;
			break;
		case GPU_VERTEX_DATA_F32_3X:
			vi_attrs[attr].format = VK_FORMAT_R32G32B32_SFLOAT;
			break;
		case GPU_VERTEX_DATA_F32_4X:
			vi_attrs[attr].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			break;
		case GPU_VERTEX_DATA_I16_2X_NORM:
			vi_attrs[attr].format = VK_FORMAT_R16G16_SNORM;
			break;
		case GPU_VERTEX_DATA_I16_4X_NORM:
			vi_attrs[attr].format = VK_FORMAT_R16G16B16A16_SNORM;
			break;
		}
		attr++;
	}
	vi_bindings[0].binding   = 0;
	vi_bindings[0].stride    = offset;
	vi_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	memset(&ia, 0, sizeof(ia));
	ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	memset(&rs, 0, sizeof(rs));
	rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode             = VK_POLYGON_MODE_FILL;
	rs.cullMode                = convert_cull_mode(pipeline->cull_mode);
	rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.depthClampEnable        = VK_FALSE;
	rs.rasterizerDiscardEnable = VK_FALSE;
	rs.depthBiasEnable         = VK_FALSE;
	rs.lineWidth               = 1.0f;

	memset(&cb, 0, sizeof(cb));
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	VkPipelineColorBlendAttachmentState att_state[8];
	memset(att_state, 0, sizeof(att_state));
	for (int i = 0; i < pipeline->color_attachment_count; ++i) {
		att_state[i].colorWriteMask =
		    (pipeline->color_write_mask_red[i] ? VK_COLOR_COMPONENT_R_BIT : 0) | (pipeline->color_write_mask_green[i] ? VK_COLOR_COMPONENT_G_BIT : 0) |
		    (pipeline->color_write_mask_blue[i] ? VK_COLOR_COMPONENT_B_BIT : 0) | (pipeline->color_write_mask_alpha[i] ? VK_COLOR_COMPONENT_A_BIT : 0);
		att_state[i].blendEnable = pipeline->blend_source != GPU_BLEND_ONE || pipeline->blend_destination != GPU_BLEND_ZERO ||
		                           pipeline->alpha_blend_source != GPU_BLEND_ONE || pipeline->alpha_blend_destination != GPU_BLEND_ZERO;
		att_state[i].srcColorBlendFactor = convert_blend_factor(pipeline->blend_source);
		att_state[i].dstColorBlendFactor = convert_blend_factor(pipeline->blend_destination);
		att_state[i].colorBlendOp        = VK_BLEND_OP_ADD;
		att_state[i].srcAlphaBlendFactor = convert_blend_factor(pipeline->alpha_blend_source);
		att_state[i].dstAlphaBlendFactor = convert_blend_factor(pipeline->alpha_blend_destination);
		att_state[i].alphaBlendOp        = VK_BLEND_OP_ADD;
	}
	cb.attachmentCount = pipeline->color_attachment_count;
	cb.pAttachments    = att_state;

	memset(&vp, 0, sizeof(vp));
	vp.sType                                                     = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount                                             = 1;
	dynamic_state[dynamic_state_create_info.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
	vp.scissorCount                                              = 1;
	dynamic_state[dynamic_state_create_info.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;

	memset(&ds, 0, sizeof(ds));
	ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable       = pipeline->depth_mode != GPU_COMPARE_MODE_ALWAYS;
	ds.depthWriteEnable      = pipeline->depth_write;
	ds.depthCompareOp        = convert_compare_mode(pipeline->depth_mode);
	ds.depthBoundsTestEnable = VK_FALSE;
	ds.back.failOp           = VK_STENCIL_OP_KEEP;
	ds.back.passOp           = VK_STENCIL_OP_KEEP;
	ds.back.compareOp        = VK_COMPARE_OP_ALWAYS;
	ds.stencilTestEnable     = VK_FALSE;
	ds.front                 = ds.back;

	memset(&ms, 0, sizeof(ms));
	ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	pipeline_info.stageCount = 2;
	VkPipelineShaderStageCreateInfo shaderStages[2];
	memset(&shaderStages, 0, 2 * sizeof(VkPipelineShaderStageCreateInfo));

	shaderStages[0].sType             = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage             = VK_SHADER_STAGE_VERTEX_BIT;
	VkShaderModule vert_shader_module = create_shader_module(pipeline->vertex_shader->impl.source, pipeline->vertex_shader->impl.length);
	shaderStages[0].module            = vert_shader_module;
	shaderStages[0].pName             = "main";

	shaderStages[1].sType             = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage             = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkShaderModule frag_shader_module = create_shader_module(pipeline->fragment_shader->impl.source, pipeline->fragment_shader->impl.length);
	shaderStages[1].module            = frag_shader_module;
	shaderStages[1].pName             = "main";

	pipeline_info.pVertexInputState   = &vi;
	pipeline_info.pInputAssemblyState = &ia;
	pipeline_info.pRasterizationState = &rs;
	pipeline_info.pColorBlendState    = &cb;
	pipeline_info.pMultisampleState   = &ms;
	pipeline_info.pViewportState      = &vp;
	pipeline_info.pDepthStencilState  = &ds;
	pipeline_info.pStages             = shaderStages;
	pipeline_info.pDynamicState       = &dynamic_state_create_info;

	VkFormat color_attachment_formats[8];
	for (int i = 0; i < pipeline->color_attachment_count; ++i) {
		color_attachment_formats[i] = convert_image_format(pipeline->color_attachment[i]);
	}

	VkPipelineRenderingCreateInfo rendering_info = {
	    .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
	    .colorAttachmentCount    = pipeline->color_attachment_count,
	    .pColorAttachmentFormats = color_attachment_formats,
	    .depthAttachmentFormat   = pipeline->depth_attachment_bits > 0 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
	};
	pipeline_info.pNext = &rendering_info;

	VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline->impl.pipeline);
	vkDestroyShaderModule(device, frag_shader_module, NULL);
	vkDestroyShaderModule(device, vert_shader_module, NULL);
}

void gpu_shader_init(gpu_shader_t *shader, const void *source, size_t length, gpu_shader_type_t type) {
	shader->impl.length = (int)length;
	shader->impl.source = (char *)malloc(length);
	memcpy(shader->impl.source, source, length);
}

void gpu_shader_destroy(gpu_shader_t *shader) {
	free(shader->impl.source);
	shader->impl.source = NULL;
}

void gpu_texture_init_from_bytes(gpu_texture_t *texture, void *data, uint32_t width, uint32_t height, gpu_texture_format_t format) {
	texture->width  = width;
	texture->height = height;
	texture->format = format;
	texture->state  = GPU_TEXTURE_STATE_SHADER_RESOURCE;
	texture->buffer = NULL;

	VkFormat vk_format = convert_image_format(format);
	if (vk_format == VK_FORMAT_B8G8R8A8_UNORM) {
		vk_format = VK_FORMAT_R8G8B8A8_UNORM;
	}

	VkDeviceSize _upload_size  = width * height * (VkDeviceSize)gpu_texture_format_size(format);
	void        *original_data = data;

#ifdef WITH_BC7
	if (gpu_bc7_supported(width, height, format)) {
		texture->format = GPU_TEXTURE_FORMAT_RGBA32_BC7;
		vk_format       = VK_FORMAT_BC7_UNORM_BLOCK;
		data            = gpu_bc7_compress(data, width, height);
		_upload_size    = (VkDeviceSize)((width + 3) / 4) * ((height + 3) / 4) * 16; // BC7ENC_BLOCK_SIZE
	}
#endif

	uint32_t new_upload_buffer_size = _upload_size;
	if (new_upload_buffer_size < (1024 * 1024 * 4)) {
		new_upload_buffer_size = (1024 * 1024 * 4);
	}
	if (upload_buffer_size < new_upload_buffer_size) {
		if (upload_buffer_size > 0) {
			vkFreeMemory(device, upload_mem, NULL);
			vkDestroyBuffer(device, upload_buffer, NULL);
		}
		upload_buffer_size             = new_upload_buffer_size;
		VkBufferCreateInfo buffer_info = {
		    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .size        = upload_buffer_size,
		    .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		vkCreateBuffer(device, &buffer_info, NULL, &upload_buffer);

		VkMemoryRequirements mem_reqs;
		vkGetBufferMemoryRequirements(device, upload_buffer, &mem_reqs);
		VkMemoryAllocateInfo mem_alloc = {
		    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		    .allocationSize = mem_reqs.size,
		};
		mem_alloc.memoryTypeIndex =
		    memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkAllocateMemory(device, &mem_alloc, NULL, &upload_mem);
		vkBindBufferMemory(device, upload_buffer, upload_mem, 0);
	}

	void *mapped_data;
	vkMapMemory(device, upload_mem, 0, _upload_size, 0, &mapped_data);
	memcpy(mapped_data, data, _upload_size);
	vkUnmapMemory(device, upload_mem);

	VkImageCreateInfo image_info = {
	    .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType     = VK_IMAGE_TYPE_2D,
	    .format        = vk_format,
	    .extent.width  = (uint32_t)width,
	    .extent.height = (uint32_t)height,
	    .extent.depth  = 1,
	    .mipLevels     = 1,
	    .arrayLayers   = 1,
	    .samples       = VK_SAMPLE_COUNT_1_BIT,
	    .tiling        = VK_IMAGE_TILING_OPTIMAL,
	    .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	    .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	vkCreateImage(device, &image_info, NULL, &texture->impl.image);
	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(device, texture->impl.image, &mem_reqs);
	VkMemoryAllocateInfo mem_alloc = {
	    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = mem_reqs.size,
	};
	mem_alloc.memoryTypeIndex = memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkResult result           = vkAllocateMemory(device, &mem_alloc, NULL, &texture->impl.mem);

	if (result != VK_SUCCESS && gpu_cleanup_pending()) {
		gpu_execute_and_wait();
		gpu_cleanup_internal();
		gpu_cleanup();
#ifdef WITH_BC7
		if (data != original_data) {
			free(data);
		}
#endif
		gpu_texture_init_from_bytes(texture, original_data, width, height, format);
		return;
	}

	vkBindImageMemory(device, texture->impl.image, texture->impl.mem, 0);

	if (gpu_in_use) {
		vkCmdEndRendering(command_buffer);
	}

	VkImageMemoryBarrier barrier = {
	    .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask                   = 0,
	    .dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT,
	    .oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED,
	    .newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    .image                           = texture->impl.image,
	    .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
	    .subresourceRange.baseMipLevel   = 0,
	    .subresourceRange.levelCount     = 1,
	    .subresourceRange.baseArrayLayer = 0,
	    .subresourceRange.layerCount     = 1,
	};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

	VkBufferImageCopy copy_region = {
	    .bufferOffset                    = 0,
	    .bufferRowLength                 = 0,
	    .bufferImageHeight               = 0,
	    .imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
	    .imageSubresource.mipLevel       = 0,
	    .imageSubresource.baseArrayLayer = 0,
	    .imageSubresource.layerCount     = 1,
	    .imageOffset                     = {0, 0, 0},
	    .imageExtent                     = {(uint32_t)width, (uint32_t)height, 1},
	};
	vkCmdCopyBufferToImage(command_buffer, upload_buffer, texture->impl.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

	VkImageViewCreateInfo view_info = {
	    .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .image    = texture->impl.image,
	    .viewType = VK_IMAGE_VIEW_TYPE_2D,
	    .format   = vk_format,
	    .components =
	        {
	            .r = VK_COMPONENT_SWIZZLE_R,
	            .g = VK_COMPONENT_SWIZZLE_G,
	            .b = VK_COMPONENT_SWIZZLE_B,
	            .a = VK_COMPONENT_SWIZZLE_A,
	        },
	    .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
	    .subresourceRange.baseMipLevel   = 0,
	    .subresourceRange.levelCount     = 1,
	    .subresourceRange.baseArrayLayer = 0,
	    .subresourceRange.layerCount     = 1,
	};
	vkCreateImageView(device, &view_info, NULL, &texture->impl.view);

	if (gpu_in_use) {
		vkCmdBeginRendering(command_buffer, &current_rendering_info);
	}

	gpu_execute_and_wait(); ////

#ifdef WITH_BC7
	if (data != original_data) {
		free(data);
	}
#endif
}

void gpu_texture_destroy_internal(gpu_texture_t *target) {
	if (target->impl.image != NULL) {
		vkDestroyImage(device, target->impl.image, NULL);
		vkFreeMemory(device, target->impl.mem, NULL);
	}
	if (target->impl.view != NULL) {
		vkDestroyImageView(device, target->impl.view, NULL);
	}
}

void gpu_render_target_init(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format) {
	gpu_render_target_init2(target, width, height, format, -1);
}

void _gpu_buffer_init(VkBuffer *buf, VkDeviceMemory *mem, uint32_t size, uint32_t usage, uint32_t memory_requirements) {
	if (buf != NULL && *buf != NULL) {
		assert(buffers_to_destroy_count < 512);
		buffers_to_destroy[buffers_to_destroy_count]         = *buf;
		buffer_memories_to_destroy[buffers_to_destroy_count] = *mem;
		buffers_to_destroy_count++;
	}

	VkBufferCreateInfo buf_info = {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size  = size,
	    .usage = usage,
	};
	bool raytrace = gpu_raytrace_supported() && ((usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) || (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
	if (raytrace) {
		buf_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		buf_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		buf_info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	}
	vkCreateBuffer(device, &buf_info, NULL, buf);
	VkMemoryRequirements mem_reqs = {0};
	vkGetBufferMemoryRequirements(device, *buf, &mem_reqs);

	VkMemoryAllocateInfo mem_alloc = {
	    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = mem_reqs.size,
	};
	mem_alloc.memoryTypeIndex                            = memory_type_from_properties(mem_reqs.memoryTypeBits, memory_requirements);
	VkMemoryAllocateFlagsInfo memory_allocate_flags_info = {0};
	if (raytrace) {
		memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		mem_alloc.pNext                  = &memory_allocate_flags_info;
	}
	VkResult result = vkAllocateMemory(device, &mem_alloc, NULL, mem);

	if (result != VK_SUCCESS && gpu_cleanup_pending()) {
		gpu_execute_and_wait();
		gpu_cleanup_internal();
		gpu_cleanup();
		_gpu_buffer_init(buf, mem, size, usage, memory_requirements);
		return;
	}

	vkBindBufferMemory(device, *buf, *mem, 0);
}

void _gpu_buffer_copy(VkBuffer dest, VkBuffer source, uint32_t size) {
	if (gpu_in_use) {
		vkCmdEndRendering(command_buffer);
	}
	VkBufferCopy copy_region = {
	    .size = size,
	};
	vkCmdCopyBuffer(command_buffer, source, dest, 1, &copy_region);
	VkBufferMemoryBarrier buf_barrier = {
	    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
	    .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
	    .dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .buffer              = dest,
	    .offset              = 0,
	    .size                = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, NULL, 1, &buf_barrier, 0, NULL);
	if (gpu_in_use) {
		vkCmdBeginRendering(command_buffer, &current_rendering_info);
	}
}

void gpu_vertex_buffer_init(gpu_buffer_t *buffer, uint32_t count, gpu_vertex_structure_t *structure) {
	buffer->count  = count;
	buffer->stride = 0;
	for (int i = 0; i < structure->size; ++i) {
		gpu_vertex_element_t element = structure->elements[i];
		buffer->stride += gpu_vertex_data_size(element.data);
	}
	buffer->cpu_write    = false;
	buffer->impl.buf     = NULL;
	buffer->impl.cpu_buf = NULL;
}

void *gpu_vertex_buffer_lock(gpu_buffer_t *buffer) {
	if (unified_memory && buffer->cpu_write) {
		if (buffer->impl.buf == NULL) {
			_gpu_buffer_init(&buffer->impl.buf, &buffer->impl.mem, buffer->count * buffer->stride, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}
		void *p;
		vkMapMemory(device, buffer->impl.mem, 0, buffer->count * buffer->stride, 0, (void **)&p);
		return p;
	}

	if (!buffer->cpu_write || buffer->impl.cpu_buf == NULL) {
		_gpu_buffer_init(&buffer->impl.cpu_buf, &buffer->impl.cpu_mem, buffer->count * buffer->stride, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
	void *p;
	vkMapMemory(device, buffer->impl.cpu_mem, 0, buffer->count * buffer->stride, 0, (void **)&p);
	return p;
}

void gpu_vertex_buffer_unlock(gpu_buffer_t *buffer) {

	if (unified_memory && buffer->cpu_write) {
		vkUnmapMemory(device, buffer->impl.mem);
		return;
	}

	if (!buffer->cpu_write || buffer->impl.buf == NULL) {
		_gpu_buffer_init(&buffer->impl.buf, &buffer->impl.mem, buffer->count * buffer->stride,
		                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}
	vkUnmapMemory(device, buffer->impl.cpu_mem);
	_gpu_buffer_copy(buffer->impl.buf, buffer->impl.cpu_buf, buffer->count * buffer->stride);

	if (!buffer->cpu_write) {
		assert(buffers_to_destroy_count < 512);
		buffers_to_destroy[buffers_to_destroy_count]         = buffer->impl.cpu_buf;
		buffer_memories_to_destroy[buffers_to_destroy_count] = buffer->impl.cpu_mem;
		buffers_to_destroy_count++;
		buffer->impl.cpu_buf = NULL;
		buffer->impl.cpu_mem = VK_NULL_HANDLE;
	}
}

void gpu_index_buffer_init(gpu_buffer_t *buffer, uint32_t count) {
	buffer->count        = count;
	buffer->stride       = sizeof(uint32_t);
	buffer->cpu_write    = false;
	buffer->impl.buf     = NULL;
	buffer->impl.cpu_buf = NULL;
}

void *gpu_index_buffer_lock(gpu_buffer_t *buffer) {
	_gpu_buffer_init(&buffer->impl.buf, &buffer->impl.mem, buffer->count * buffer->stride, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	void *p;
	vkMapMemory(device, buffer->impl.mem, 0, buffer->count * buffer->stride, 0, (void **)&p);
	return p;
}

void gpu_index_buffer_unlock(gpu_buffer_t *buffer) {
	vkUnmapMemory(device, buffer->impl.mem);
	VkBuffer upload_buffer = buffer->impl.buf;
	_gpu_buffer_init(&buffer->impl.buf, &buffer->impl.mem, buffer->count * buffer->stride, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_gpu_buffer_copy(buffer->impl.buf, upload_buffer, buffer->count * buffer->stride);
}

void gpu_constant_buffer_init(gpu_buffer_t *buffer, uint32_t size) {
	buffer->count        = size;
	buffer->data         = NULL;
	buffer->cpu_write    = false;
	buffer->impl.buf     = NULL;
	buffer->impl.cpu_buf = NULL;
	_gpu_buffer_init(&buffer->impl.buf, &buffer->impl.mem, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void gpu_constant_buffer_lock(gpu_buffer_t *buffer, uint32_t start, uint32_t count) {
	vkMapMemory(device, buffer->impl.mem, start, count, 0, (void **)&buffer->data);
}

void gpu_constant_buffer_unlock(gpu_buffer_t *buffer) {
	vkUnmapMemory(device, buffer->impl.mem);
	buffer->data = NULL;
}

void gpu_buffer_destroy_internal(gpu_buffer_t *buffer) {
	vkFreeMemory(device, buffer->impl.mem, NULL);
	vkDestroyBuffer(device, buffer->impl.buf, NULL);
}

char *gpu_device_name() {
	return device_name;
}

bool gpu_bc7_supported(int width, int height, gpu_texture_format_t format) {
	static bool bc7_supported = false;
#ifdef WITH_BC7
	static bool bc7_checked = false;
	if (!bc7_checked) {
		bc7_checked = true;
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_BC7_UNORM_BLOCK, &props);
		bc7_supported = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
	}
#endif
	return bc7_supported && format == GPU_TEXTURE_FORMAT_RGBA32 && width >= 2048 && height >= 2048 && (width & (width - 1)) == 0 &&
	       (height & (height - 1)) == 0;
}

typedef struct inst {
	mat4_t m;
	int    i;
} inst_t;

static VkDescriptorPool              raytrace_descriptor_pool;
static gpu_acceleration_structure_t *accel;
static gpu_raytrace_pipeline_t      *pipeline;
static gpu_texture_t                *output = NULL;
static gpu_texture_t                *texpaint0;
static gpu_texture_t                *texpaint1;
static gpu_texture_t                *texpaint2;
static gpu_texture_t                *texenv;
static gpu_texture_t                *texsobol;
static gpu_texture_t                *texscramble;
static gpu_texture_t                *texrank;
static gpu_buffer_t                 *vb[16];
static gpu_buffer_t                 *vb_last[16];
static gpu_buffer_t                 *ib[16];
static int                           vb_count      = 0;
static int                           vb_count_last = 0;
static inst_t                        instances[1024];
static int                           instances_count = 0;
static VkBuffer                      vb_full         = VK_NULL_HANDLE;
static VkBuffer                      ib_full         = VK_NULL_HANDLE;
static VkDeviceMemory                vb_full_mem     = VK_NULL_HANDLE;
static VkDeviceMemory                ib_full_mem     = VK_NULL_HANDLE;

static PFN_vkGetBufferDeviceAddressKHR                _vkGetBufferDeviceAddressKHR                = NULL;
static PFN_vkCreateAccelerationStructureKHR           _vkCreateAccelerationStructureKHR           = NULL;
static PFN_vkGetAccelerationStructureDeviceAddressKHR _vkGetAccelerationStructureDeviceAddressKHR = NULL;
static PFN_vkGetAccelerationStructureBuildSizesKHR    _vkGetAccelerationStructureBuildSizesKHR    = NULL;
static PFN_vkCmdBuildAccelerationStructuresKHR        _vkCmdBuildAccelerationStructuresKHR        = NULL;
static PFN_vkDestroyAccelerationStructureKHR          _vkDestroyAccelerationStructureKHR          = NULL;

bool gpu_raytrace_supported() {
	static bool extensions_checked = false;
	static bool raytrace_supported = true;
	if (extensions_checked) {
		return raytrace_supported;
	}

	const char *required_extensions[]     = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
	                                         VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME};
	uint32_t    required_extensions_count = sizeof(required_extensions) / sizeof(required_extensions[0]);
	uint32_t    extensions_count          = 0;
	vkEnumerateDeviceExtensionProperties(gpu, NULL, &extensions_count, NULL);
	VkExtensionProperties *extensions = (VkExtensionProperties *)malloc(sizeof(VkExtensionProperties) * extensions_count);
	vkEnumerateDeviceExtensionProperties(gpu, NULL, &extensions_count, extensions);
	for (uint32_t i = 0; i < required_extensions_count; i++) {
		bool found = false;
		for (uint32_t j = 0; j < extensions_count; j++) {
			if (strcmp(required_extensions[i], extensions[j].extensionName) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			raytrace_supported = false;
			break;
		}
	}
	free(extensions);
	extensions_checked = true;
	return raytrace_supported;
}

void gpu_raytrace_pipeline_init(gpu_raytrace_pipeline_t *pipeline, void *compute_shader, int compute_shader_size, gpu_buffer_t *constant_buffer) {
	output                    = NULL;
	pipeline->constant_buffer = constant_buffer;

	{
		VkDescriptorSetLayoutBinding bindings[] = {{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
		                                           {12, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT}};

		VkDescriptorSetLayoutCreateInfo layout_info = {
		    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		    .bindingCount = 13,
		    .pBindings    = &bindings[0],
		};
		vkCreateDescriptorSetLayout(device, &layout_info, NULL, &pipeline->impl.descriptor_set_layout);

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		    .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		    .setLayoutCount = 1,
		    .pSetLayouts    = &pipeline->impl.descriptor_set_layout,
		};
		vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline->impl.pipeline_layout);

		VkShaderModuleCreateInfo module_create_info = {
		    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		    .codeSize = compute_shader_size,
		    .pCode    = (const uint32_t *)compute_shader,
		};
		VkShaderModule shader_module;
		vkCreateShaderModule(device, &module_create_info, NULL, &shader_module);

		VkPipelineShaderStageCreateInfo shader_stage = {
		    .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
		    .module = shader_module,
		    .pName  = "main",
		};

		VkComputePipelineCreateInfo pipeline_info = {
		    .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .stage  = shader_stage,
		    .layout = pipeline->impl.pipeline_layout,
		};

		vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline->impl.pipeline);
		vkDestroyShaderModule(device, shader_module, NULL);
	}

	{
		VkDescriptorPoolSize type_counts[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
		                                      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 7},
		                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
		                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
		                                      {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};

		VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
		    .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		    .maxSets       = 1024,
		    .poolSizeCount = 6,
		    .pPoolSizes    = type_counts,
		};

		vkCreateDescriptorPool(device, &descriptor_pool_create_info, NULL, &raytrace_descriptor_pool);

		VkDescriptorSetAllocateInfo alloc_info = {
		    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		    .descriptorPool     = raytrace_descriptor_pool,
		    .descriptorSetCount = 1,
		    .pSetLayouts        = &pipeline->impl.descriptor_set_layout,
		};
		vkAllocateDescriptorSets(device, &alloc_info, &pipeline->impl.descriptor_set);
	}
}

void gpu_raytrace_pipeline_destroy(gpu_raytrace_pipeline_t *pipeline) {
	vkDestroyPipeline(device, pipeline->impl.pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline->impl.pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(device, pipeline->impl.descriptor_set_layout, NULL);
}

uint64_t get_buffer_device_address(VkBuffer buffer) {
	VkBufferDeviceAddressInfoKHR buffer_device_address_info = {
	    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	    .buffer = buffer,
	};
	_vkGetBufferDeviceAddressKHR = (void *)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR");
	return _vkGetBufferDeviceAddressKHR(device, &buffer_device_address_info);
}

void gpu_raytrace_acceleration_structure_init(gpu_acceleration_structure_t *accel) {
	_vkGetBufferDeviceAddressKHR                = (void *)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR");
	_vkCreateAccelerationStructureKHR           = (void *)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
	_vkGetAccelerationStructureDeviceAddressKHR = (void *)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
	_vkGetAccelerationStructureBuildSizesKHR    = (void *)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");

	vb_count        = 0;
	instances_count = 0;
	memset(vb_last, 0, sizeof(vb_last));
}

void gpu_raytrace_acceleration_structure_add(gpu_acceleration_structure_t *accel, gpu_buffer_t *_vb, gpu_buffer_t *_ib, mat4_t _transform) {
	int vb_i = -1;
	for (int i = 0; i < vb_count; ++i) {
		if (_vb == vb[i]) {
			vb_i = i;
			break;
		}
	}
	if (vb_i == -1) {
		vb_i         = vb_count;
		vb[vb_count] = _vb;
		ib[vb_count] = _ib;
		vb_count++;
	}

	inst_t inst                = {.i = vb_i, .m = _transform};
	instances[instances_count] = inst;
	instances_count++;
}

void _gpu_raytrace_acceleration_structure_destroy_bottom(gpu_acceleration_structure_t *accel) {
	_vkDestroyAccelerationStructureKHR = (void *)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
	for (int i = 0; i < vb_count_last; ++i) {
		_vkDestroyAccelerationStructureKHR(device, accel->impl.bottom_level_acceleration_structure[i], NULL);
		vkFreeMemory(device, accel->impl.bottom_level_mem[i], NULL);
		vkDestroyBuffer(device, accel->impl.bottom_level_buffer[i], NULL);
	}
}

void _gpu_raytrace_acceleration_structure_destroy_top(gpu_acceleration_structure_t *accel) {
	_vkDestroyAccelerationStructureKHR = (void *)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
	_vkDestroyAccelerationStructureKHR(device, accel->impl.top_level_acceleration_structure, NULL);
	vkFreeMemory(device, accel->impl.top_level_mem, NULL);
	vkDestroyBuffer(device, accel->impl.top_level_buffer, NULL);
	vkFreeMemory(device, accel->impl.instances_mem, NULL);
	vkDestroyBuffer(device, accel->impl.instances_buffer, NULL);
}

void gpu_raytrace_acceleration_structure_build(gpu_acceleration_structure_t *accel, gpu_buffer_t *_vb_full, gpu_buffer_t *_ib_full) {
	gpu_execute_and_wait();

	bool build_bottom = false;
	for (int i = 0; i < 16; ++i) {
		if (vb_last[i] != vb[i]) {
			build_bottom = true;
		}
		vb_last[i] = vb[i];
	}

	if (vb_count_last > 0) {
		if (build_bottom) {
			_gpu_raytrace_acceleration_structure_destroy_bottom(accel);
		}
		_gpu_raytrace_acceleration_structure_destroy_top(accel);
	}

	vb_count_last = vb_count;

	if (vb_count == 0) {
		return;
	}

	// Bottom level
	if (build_bottom) {
		for (int i = 0; i < vb_count; ++i) {

			uint32_t prim_count = ib[i]->count / 3;
			uint32_t vert_count = vb[i]->count;

			VkDeviceOrHostAddressConstKHR vertex_data_device_address = {0};
			VkDeviceOrHostAddressConstKHR index_data_device_address  = {0};

			vertex_data_device_address.deviceAddress = get_buffer_device_address(vb[i]->impl.buf);
			index_data_device_address.deviceAddress  = get_buffer_device_address(ib[i]->impl.buf);

			VkAccelerationStructureGeometryKHR acceleration_geometry = {
			    .sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			    .flags                                       = VK_GEOMETRY_OPAQUE_BIT_KHR,
			    .geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			    .geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
			    .geometry.triangles.vertexFormat             = VK_FORMAT_R16G16B16A16_SNORM,
			    .geometry.triangles.vertexData.deviceAddress = vertex_data_device_address.deviceAddress,
			    .geometry.triangles.vertexStride             = vb[i]->stride,
			    .geometry.triangles.maxVertex                = vb[i]->count,
			    .geometry.triangles.indexType                = VK_INDEX_TYPE_UINT32,
			    .geometry.triangles.indexData.deviceAddress  = index_data_device_address.deviceAddress,
			};

			VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info = {
			    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			    .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			    .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
			    .geometryCount = 1,
			    .pGeometries   = &acceleration_geometry,
			};

			VkAccelerationStructureBuildSizesInfoKHR acceleration_build_sizes_info = {
			    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
			};
			_vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &acceleration_structure_build_geometry_info,
			                                         &prim_count, &acceleration_build_sizes_info);

			VkBufferCreateInfo buffer_create_info = {
			    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			    .size        = acceleration_build_sizes_info.accelerationStructureSize,
			    .usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			};
			VkBuffer bottom_level_buffer = VK_NULL_HANDLE;
			vkCreateBuffer(device, &buffer_create_info, NULL, &bottom_level_buffer);

			VkMemoryRequirements memory_requirements2;
			vkGetBufferMemoryRequirements(device, bottom_level_buffer, &memory_requirements2);

			VkMemoryAllocateFlagsInfo memory_allocate_flags_info2 = {
			    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
			    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
			};

			VkMemoryAllocateInfo memory_allocate_info = {
			    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			    .pNext          = &memory_allocate_flags_info2,
			    .allocationSize = memory_requirements2.size,
			};
			memory_allocate_info.memoryTypeIndex = memory_type_from_properties(memory_requirements2.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VkDeviceMemory bottom_level_mem;
			vkAllocateMemory(device, &memory_allocate_info, NULL, &bottom_level_mem);
			vkBindBufferMemory(device, bottom_level_buffer, bottom_level_mem, 0);

			VkAccelerationStructureCreateInfoKHR acceleration_create_info = {
			    .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			    .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			    .buffer = bottom_level_buffer,
			    .size   = acceleration_build_sizes_info.accelerationStructureSize,
			};
			_vkCreateAccelerationStructureKHR(device, &acceleration_create_info, NULL, &accel->impl.bottom_level_acceleration_structure[i]);

			VkBuffer       scratch_buffer = VK_NULL_HANDLE;
			VkDeviceMemory scratch_memory = VK_NULL_HANDLE;

			buffer_create_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_create_info.size        = acceleration_build_sizes_info.buildScratchSize;
			buffer_create_info.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			vkCreateBuffer(device, &buffer_create_info, NULL, &scratch_buffer);

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(device, scratch_buffer, &memory_requirements);

			VkMemoryAllocateFlagsInfo memory_allocate_flags_info = {
			    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
			    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
			};

			memory_allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memory_allocate_info.pNext           = &memory_allocate_flags_info;
			memory_allocate_info.allocationSize  = memory_requirements.size;
			memory_allocate_info.memoryTypeIndex = memory_type_from_properties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			vkAllocateMemory(device, &memory_allocate_info, NULL, &scratch_memory);
			vkBindBufferMemory(device, scratch_buffer, scratch_memory, 0);

			VkBufferDeviceAddressInfoKHR buffer_device_address_info = {
			    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			    .buffer = scratch_buffer,
			};
			uint64_t scratch_buffer_device_address = _vkGetBufferDeviceAddressKHR(device, &buffer_device_address_info);

			VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info = {
			    .sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			    .type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			    .flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
			    .mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			    .dstAccelerationStructure  = accel->impl.bottom_level_acceleration_structure[i],
			    .geometryCount             = 1,
			    .pGeometries               = &acceleration_geometry,
			    .scratchData.deviceAddress = scratch_buffer_device_address,
			};

			VkAccelerationStructureBuildRangeInfoKHR acceleration_build_range_info = {
			    .primitiveCount = prim_count,
			};

			const VkAccelerationStructureBuildRangeInfoKHR *acceleration_build_infos[1] = {&acceleration_build_range_info};

			{
				VkCommandBufferAllocateInfo cmd_buf_allocate_info = {
				    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				    .commandPool        = cmd_pool,
				    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				    .commandBufferCount = 1,
				};

				VkCommandBuffer command_buffer;
				vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &command_buffer);

				VkCommandBufferBeginInfo command_buffer_info = {
				    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				};
				vkBeginCommandBuffer(command_buffer, &command_buffer_info);

				_vkCmdBuildAccelerationStructuresKHR = (void *)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
				_vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &acceleration_build_geometry_info, &acceleration_build_infos[0]);

				vkEndCommandBuffer(command_buffer);

				VkSubmitInfo submit_info = {
				    .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				    .commandBufferCount = 1,
				    .pCommandBuffers    = &command_buffer,
				};

				VkFenceCreateInfo fence_info = {
				    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				};

				VkFence fence;
				vkCreateFence(device, &fence_info, NULL, &fence);

				vkQueueSubmit(queue, 1, &submit_info, fence);
				vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000);
				vkDestroyFence(device, fence, NULL);
				vkFreeCommandBuffers(device, cmd_pool, 1, &command_buffer);
			}

			VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info = {
			    .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
			    .accelerationStructure = accel->impl.bottom_level_acceleration_structure[i],
			};

			accel->impl.bottom_level_acceleration_structure_handle[i] = _vkGetAccelerationStructureDeviceAddressKHR(device, &acceleration_device_address_info);

			vkFreeMemory(device, scratch_memory, NULL);
			vkDestroyBuffer(device, scratch_buffer, NULL);

			accel->impl.bottom_level_buffer[i] = bottom_level_buffer;
			accel->impl.bottom_level_mem[i]    = bottom_level_mem;
		}
	}

	// Top level

	{
		VkBufferCreateInfo buf_info = {
		    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .size  = instances_count * sizeof(VkAccelerationStructureInstanceKHR),
		    .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		    .flags = 0,
		};

		VkMemoryAllocateInfo mem_alloc = {0};
		memset(&mem_alloc, 0, sizeof(VkMemoryAllocateInfo));
		mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

		VkBuffer instances_buffer;
		vkCreateBuffer(device, &buf_info, NULL, &instances_buffer);

		VkMemoryRequirements mem_reqs = {0};
		vkGetBufferMemoryRequirements(device, instances_buffer, &mem_reqs);

		mem_alloc.allocationSize  = mem_reqs.size;
		mem_alloc.memoryTypeIndex = memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		VkMemoryAllocateFlagsInfo memory_allocate_flags_info = {
		    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
		};
		mem_alloc.pNext = &memory_allocate_flags_info;

		VkDeviceMemory instances_mem;
		vkAllocateMemory(device, &mem_alloc, NULL, &instances_mem);

		vkBindBufferMemory(device, instances_buffer, instances_mem, 0);
		void *data;
		vkMapMemory(device, instances_mem, 0, sizeof(VkAccelerationStructureInstanceKHR), 0, (void **)&data);

		for (int i = 0; i < instances_count; ++i) {
			VkTransformMatrixKHR               transform_matrix = {instances[i].m.m[0], instances[i].m.m[4], instances[i].m.m[8],  instances[i].m.m[12],
			                                                       instances[i].m.m[1], instances[i].m.m[5], instances[i].m.m[9],  instances[i].m.m[13],
			                                                       instances[i].m.m[2], instances[i].m.m[6], instances[i].m.m[10], instances[i].m.m[14]};
			VkAccelerationStructureInstanceKHR instance         = {
			            .transform = transform_matrix,
            };

			int ib_off = 0;
			for (int j = 0; j < instances[i].i; ++j) {
				ib_off += ib[j]->count * 4;
			}
			instance.instanceCustomIndex = ib_off;

			instance.mask                                   = 0xFF;
			instance.instanceShaderBindingTableRecordOffset = 0;
			instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			instance.accelerationStructureReference         = accel->impl.bottom_level_acceleration_structure_handle[instances[i].i];
			memcpy(data + i * sizeof(VkAccelerationStructureInstanceKHR), &instance, sizeof(VkAccelerationStructureInstanceKHR));
		}

		vkUnmapMemory(device, instances_mem);

		VkDeviceOrHostAddressConstKHR instance_data_device_address = {
		    .deviceAddress = get_buffer_device_address(instances_buffer),
		};

		VkAccelerationStructureGeometryKHR acceleration_geometry = {
		    .sType                                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		    .flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR,
		    .geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		    .geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
		    .geometry.instances.arrayOfPointers    = VK_FALSE,
		    .geometry.instances.data.deviceAddress = instance_data_device_address.deviceAddress,
		};

		VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info = {
		    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		    .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		    .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		    .geometryCount = 1,
		    .pGeometries   = &acceleration_geometry,
		};

		VkAccelerationStructureBuildSizesInfoKHR acceleration_build_sizes_info = {
		    acceleration_build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
		};

		uint32_t instance_count = instances_count;

		_vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &acceleration_structure_build_geometry_info,
		                                         &instance_count, &acceleration_build_sizes_info);

		VkBufferCreateInfo buffer_create_info = {
		    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .size        = acceleration_build_sizes_info.accelerationStructureSize,
		    .usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		VkBuffer top_level_buffer = VK_NULL_HANDLE;
		vkCreateBuffer(device, &buffer_create_info, NULL, &top_level_buffer);

		VkMemoryRequirements memory_requirements2;
		vkGetBufferMemoryRequirements(device, top_level_buffer, &memory_requirements2);

		memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

		VkMemoryAllocateInfo memory_allocate_info = {
		    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		    .pNext          = &memory_allocate_flags_info,
		    .allocationSize = memory_requirements2.size,
		};
		memory_allocate_info.memoryTypeIndex = memory_type_from_properties(memory_requirements2.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkDeviceMemory top_level_mem;
		vkAllocateMemory(device, &memory_allocate_info, NULL, &top_level_mem);
		vkBindBufferMemory(device, top_level_buffer, top_level_mem, 0);

		VkAccelerationStructureCreateInfoKHR acceleration_create_info = {
		    .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		    .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		    .buffer = top_level_buffer,
		    .size   = acceleration_build_sizes_info.accelerationStructureSize,
		};
		_vkCreateAccelerationStructureKHR(device, &acceleration_create_info, NULL, &accel->impl.top_level_acceleration_structure);

		VkBuffer       scratch_buffer = VK_NULL_HANDLE;
		VkDeviceMemory scratch_memory = VK_NULL_HANDLE;

		buffer_create_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size        = acceleration_build_sizes_info.buildScratchSize;
		buffer_create_info.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vkCreateBuffer(device, &buffer_create_info, NULL, &scratch_buffer);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(device, scratch_buffer, &memory_requirements);

		memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

		memory_allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.pNext           = &memory_allocate_flags_info;
		memory_allocate_info.allocationSize  = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = memory_type_from_properties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkAllocateMemory(device, &memory_allocate_info, NULL, &scratch_memory);
		vkBindBufferMemory(device, scratch_buffer, scratch_memory, 0);

		VkBufferDeviceAddressInfoKHR buffer_device_address_info = {
		    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		    .buffer = scratch_buffer,
		};
		uint64_t scratch_buffer_device_address = _vkGetBufferDeviceAddressKHR(device, &buffer_device_address_info);

		VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info = {
		    .sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		    .type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		    .flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		    .mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		    .srcAccelerationStructure  = VK_NULL_HANDLE,
		    .dstAccelerationStructure  = accel->impl.top_level_acceleration_structure,
		    .geometryCount             = 1,
		    .pGeometries               = &acceleration_geometry,
		    .scratchData.deviceAddress = scratch_buffer_device_address,
		};

		VkAccelerationStructureBuildRangeInfoKHR acceleration_build_range_info = {
		    .primitiveCount = instances_count,
		};

		const VkAccelerationStructureBuildRangeInfoKHR *acceleration_build_infos[1] = {&acceleration_build_range_info};

		{
			VkCommandBufferAllocateInfo cmd_buf_allocate_info = {
			    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			    .commandPool        = cmd_pool,
			    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			    .commandBufferCount = 1,
			};

			VkCommandBuffer command_buffer;
			vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &command_buffer);

			VkCommandBufferBeginInfo command_buffer_info = {
			    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			};
			vkBeginCommandBuffer(command_buffer, &command_buffer_info);

			_vkCmdBuildAccelerationStructuresKHR = (void *)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
			_vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &acceleration_build_geometry_info, &acceleration_build_infos[0]);

			vkEndCommandBuffer(command_buffer);

			VkSubmitInfo submit_info = {
			    .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			    .commandBufferCount = 1,
			    .pCommandBuffers    = &command_buffer,
			};

			VkFenceCreateInfo fence_info = {
			    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			};

			VkFence fence;
			vkCreateFence(device, &fence_info, NULL, &fence);
			vkQueueSubmit(queue, 1, &submit_info, fence);
			vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000);
			vkDestroyFence(device, fence, NULL);
			vkFreeCommandBuffers(device, cmd_pool, 1, &command_buffer);
		}

		VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info = {
		    .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		    .accelerationStructure = accel->impl.top_level_acceleration_structure,
		};

		accel->impl.top_level_acceleration_structure_handle = _vkGetAccelerationStructureDeviceAddressKHR(device, &acceleration_device_address_info);

		vkFreeMemory(device, scratch_memory, NULL);
		vkDestroyBuffer(device, scratch_buffer, NULL);

		accel->impl.top_level_buffer = top_level_buffer;
		accel->impl.top_level_mem    = top_level_mem;
		accel->impl.instances_buffer = instances_buffer;
		accel->impl.instances_mem    = instances_mem;
	}

	{
		// if (vb_full != NULL) {
		// 	vkFreeMemory(device, vb_full_mem, NULL);
		// 	vkDestroyBuffer(device, vb_full, NULL);
		// }

		// VkBufferCreateInfo buf_info = {
		// .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		// .pNext = NULL,
		// .size = vert_count * vb[0]->stride,
		// .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		// .usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		// .usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		// .usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		// .flags = 0,
		// };

		// VkMemoryAllocateInfo mem_alloc = {
		// .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		// .pNext = NULL,
		// .allocationSize = 0,
		// .memoryTypeIndex = 0,
		// };

		// vkCreateBuffer(device, &buf_info, NULL, &vb_full);

		// VkMemoryRequirements mem_reqs = {0};
		// vkGetBufferMemoryRequirements(device, vb_full, &mem_reqs);

		// mem_alloc.allocationSize = mem_reqs.size;
		// mem_alloc.memoryTypeIndex = memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		// VkMemoryAllocateFlagsInfo memory_allocate_flags_info = {
		// .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		// .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
		// };
		// mem_alloc.pNext = &memory_allocate_flags_info;

		// vkAllocateMemory(device, &mem_alloc, NULL, &vb_full_mem);
		// vkBindBufferMemory(device, vb_full, vb_full_mem, 0);

		// float *data;
		// vkMapMemory(device, vb_full_mem, 0, vert_count * vb[0]->stride, 0, (void **)&data);
		// vkUnmapMemory(device, vb_full_mem);

		////

#ifdef is_forge

		vb_full     = _vb_full->impl.buf;
		vb_full_mem = _vb_full->impl.mem;

#else

		vb_full     = vb[0]->impl.buf;
		vb_full_mem = vb[0]->impl.mem;

#endif
	}

	{
		// if (ib_full != NULL) {
		// 	vkFreeMemory(device, ib_full_mem, NULL);
		// 	vkDestroyBuffer(device, ib_full, NULL);
		// }

		// VkBufferCreateInfo buf_info = {
		// .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		// .pNext = NULL,
		// .size = prim_count * 3 * sizeof(uint32_t),
		// .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		// .usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		// .usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		// .usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		// .flags = 0,
		// };

		// VkMemoryAllocateInfo mem_alloc = {
		// .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		// .pNext = NULL,
		// .allocationSize = 0,
		// .memoryTypeIndex = 0,
		// };

		// vkCreateBuffer(device, &buf_info, NULL, &ib_full);

		// VkMemoryRequirements mem_reqs = {0};
		// vkGetBufferMemoryRequirements(device, ib_full, &mem_reqs);

		// mem_alloc.allocationSize = mem_reqs.size;
		// mem_alloc.memoryTypeIndex = memory_type_from_properties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		// VkMemoryAllocateFlagsInfo memory_allocate_flags_info = {
		// .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		// .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
		// };
		// mem_alloc.pNext = &memory_allocate_flags_info;

		// vkAllocateMemory(device, &mem_alloc, NULL, &ib_full_mem);
		// vkBindBufferMemory(device, ib_full, ib_full_mem, 0);

		// uint8_t *data;
		// vkMapMemory(device, ib_full_mem, 0, mem_alloc.allocationSize, 0, (void **)&data);
		// for (int i = 0; i < instances_count; ++i) {
		// 	memcpy(data, ib[i]->impl., sizeof(VkAccelerationStructureInstanceKHR));
		// }
		// vkUnmapMemory(device, ib_full_mem);

		////

#ifdef is_forge

		ib_full     = _ib_full->impl.buf;
		ib_full_mem = _ib_full->impl.mem;

#else

		ib_full     = ib[0]->impl.buf;
		ib_full_mem = ib[0]->impl.mem;

#endif
	}
}

void gpu_raytrace_acceleration_structure_destroy(gpu_acceleration_structure_t *accel) {
	// _vkDestroyAccelerationStructureKHR = (void *)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
	// for (int i = 0; i < vb_count; ++i) {
	// 	_vkDestroyAccelerationStructureKHR(device, accel->impl.bottom_level_acceleration_structure[i], NULL);
	// 	vkFreeMemory(device, accel->impl.bottom_level_mem[i], NULL);
	// 	vkDestroyBuffer(device, accel->impl.bottom_level_buffer[i], NULL);
	// }
	// _vkDestroyAccelerationStructureKHR(device, accel->impl.top_level_acceleration_structure, NULL);
	// vkFreeMemory(device, accel->impl.top_level_mem, NULL);
	// vkDestroyBuffer(device, accel->impl.top_level_buffer, NULL);
	// vkFreeMemory(device, accel->impl.instances_mem, NULL);
	// vkDestroyBuffer(device, accel->impl.instances_buffer, NULL);
}

void gpu_raytrace_set_textures(gpu_texture_t *_texpaint0, gpu_texture_t *_texpaint1, gpu_texture_t *_texpaint2, gpu_texture_t *_texenv,
                               gpu_texture_t *_texsobol, gpu_texture_t *_texscramble, gpu_texture_t *_texrank) {
	texpaint0   = _texpaint0;
	texpaint1   = _texpaint1;
	texpaint2   = _texpaint2;
	texenv      = _texenv;
	texsobol    = _texsobol;
	texscramble = _texscramble;
	texrank     = _texrank;
}

void gpu_raytrace_set_acceleration_structure(gpu_acceleration_structure_t *_accel) {
	accel = _accel;
}

void gpu_raytrace_set_pipeline(gpu_raytrace_pipeline_t *_pipeline) {
	pipeline = _pipeline;
}

void gpu_raytrace_set_target(gpu_texture_t *_output) {
	if (!_output->gpu_write) {
		_output->gpu_write = true;
		gpu_texture_destroy(_output);

		VkImageCreateInfo image_info = {
		    .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		    .imageType     = VK_IMAGE_TYPE_2D,
		    .format        = convert_image_format(_output->format),
		    .extent.width  = _output->width,
		    .extent.height = _output->height,
		    .extent.depth  = 1,
		    .mipLevels     = 1,
		    .arrayLayers   = 1,
		    .samples       = VK_SAMPLE_COUNT_1_BIT,
		    .tiling        = VK_IMAGE_TILING_OPTIMAL,
		    .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		};
		vkCreateImage(device, &image_info, NULL, &_output->impl.image);

		VkMemoryRequirements memory_reqs;
		vkGetImageMemoryRequirements(device, _output->impl.image, &memory_reqs);

		VkMemoryAllocateInfo allocation_nfo = {
		    .sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		    .allocationSize = memory_reqs.size,
		};
		allocation_nfo.memoryTypeIndex = memory_type_from_properties(memory_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkAllocateMemory(device, &allocation_nfo, NULL, &_output->impl.mem);
		vkBindImageMemory(device, _output->impl.image, _output->impl.mem, 0);

		VkImageViewCreateInfo image_view_info = {
		    .sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		    .viewType                        = VK_IMAGE_VIEW_TYPE_2D,
		    .format                          = convert_image_format(_output->format),
		    .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		    .subresourceRange.baseMipLevel   = 0,
		    .subresourceRange.levelCount     = 1,
		    .subresourceRange.baseArrayLayer = 0,
		    .subresourceRange.layerCount     = 1,
		    .image                           = _output->impl.image,
		};
		vkCreateImageView(device, &image_view_info, NULL, &_output->impl.view);

		set_image_layout(_output->impl.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	output = _output;
}

void gpu_raytrace_dispatch_rays() {
	VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info = {
	    .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
	    .accelerationStructureCount = 1,
	    .pAccelerationStructures    = &accel->impl.top_level_acceleration_structure,
	};

	VkWriteDescriptorSet acceleration_structure_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .pNext           = &descriptor_acceleration_structure_info,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 0,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	};

	VkDescriptorImageInfo image_descriptor = {
	    .imageView   = output->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkDescriptorBufferInfo buffer_descriptor = {
	    .buffer = pipeline->constant_buffer->impl.buf,
	    .range  = VK_WHOLE_SIZE,
	};

	VkWriteDescriptorSet result_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 10,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	    .pImageInfo      = &image_descriptor,
	};

	VkWriteDescriptorSet uniform_buffer_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 11,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	    .pBufferInfo     = &buffer_descriptor,
	};

	VkDescriptorBufferInfo ib_descriptor = {
	    .buffer = ib_full,
	    .range  = VK_WHOLE_SIZE,
	};

	VkWriteDescriptorSet ib_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 1,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo     = &ib_descriptor,
	};

	VkDescriptorBufferInfo vb_descriptor = {
	    .buffer = vb_full,
	    .range  = VK_WHOLE_SIZE,
	};

	VkWriteDescriptorSet vb_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 2,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo     = &vb_descriptor,
	};

	VkDescriptorImageInfo tex0image_descriptor = {
	    .imageView   = texpaint0->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet tex0_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 3,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &tex0image_descriptor,
	};

	VkDescriptorImageInfo tex1image_descriptor = {
	    .imageView   = texpaint1->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet tex1_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 4,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &tex1image_descriptor,
	};

	VkDescriptorImageInfo tex2image_descriptor = {
	    .imageView   = texpaint2->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet tex2_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 5,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &tex2image_descriptor,
	};

	VkDescriptorImageInfo texenvimage_descriptor = {
	    .imageView   = texenv->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet texenv_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 6,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &texenvimage_descriptor,
	};

	VkDescriptorImageInfo texsobolimage_descriptor = {
	    .imageView   = texsobol->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet texsobol_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 7,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &texsobolimage_descriptor,
	};

	VkDescriptorImageInfo texscrambleimage_descriptor = {
	    .imageView   = texscramble->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet texscramble_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 8,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &texscrambleimage_descriptor,
	};

	VkDescriptorImageInfo texrankimage_descriptor = {
	    .imageView   = texrank->impl.view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet texrank_image_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 9,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    .pImageInfo      = &texrankimage_descriptor,
	};

	VkDescriptorImageInfo sampler_info = {
	    .sampler = linear_sampler,
	};
	VkWriteDescriptorSet sampler_linear_write = {
	    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet          = pipeline->impl.descriptor_set,
	    .dstBinding      = 12,
	    .descriptorCount = 1,
	    .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
	    .pImageInfo      = &sampler_info,
	};

	VkWriteDescriptorSet write_descriptor_sets[13] = {acceleration_structure_write,
	                                                  result_image_write,
	                                                  uniform_buffer_write,
	                                                  vb_write,
	                                                  ib_write,
	                                                  tex0_image_write,
	                                                  tex1_image_write,
	                                                  tex2_image_write,
	                                                  texenv_image_write,
	                                                  texsobol_image_write,
	                                                  texscramble_image_write,
	                                                  texrank_image_write,
	                                                  sampler_linear_write};
	vkUpdateDescriptorSets(device, 13, write_descriptor_sets, 0, VK_NULL_HANDLE);

	set_image_layout(output->impl.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->impl.pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->impl.pipeline_layout, 0, 1, &pipeline->impl.descriptor_set, 0, 0);

	uint32_t group_count_x = (output->width + 7) / 8;
	uint32_t group_count_y = (output->height + 7) / 8;
	vkCmdDispatch(command_buffer, group_count_x, group_count_y, 1);

	set_image_layout(output->impl.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
