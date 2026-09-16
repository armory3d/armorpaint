#include "webgpu_gpu.h"
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t                             constant_buffer_index;
static gpu_buffer_t                        *current_vb;
static gpu_buffer_t                        *current_ib;
static WGPUBindGroupLayout                  descriptor_layout;
static WGPUBindGroupLayout                  descriptor_layout_unfilterable;
static WGPUSampler                          linear_sampler;
static WGPUSampler                          point_sampler;
static bool                                 linear_sampling = true;
static WGPUCommandEncoder                   command_encoder = NULL;
static WGPURenderPassEncoder                render_pass_encoder;
static char                                 device_name[256];
static WGPUInstance                         instance;
static WGPUAdapter                          gpu    = NULL;
static WGPUDevice                           device = NULL;
static WGPUQueue                            queue;
static int                                  window_depth_bits;
static bool                                 window_vsync;
static WGPUSurface                          surface;
static WGPUTextureFormat                    surface_format;
static uint32_t                             framebuffer_count    = 1;
static bool                                 framebuffer_acquired = false;
static WGPUBuffer                           readback_buffer;
static int                                  readback_buffer_size = 0;
static WGPUBuffer                           upload_buffer;
static int                                  upload_buffer_size = 0;
static WGPUTexture                          dummy_texture;
static WGPUTextureView                      dummy_view;
static int                                  current_width  = 0;
static int                                  current_height = 0;
static WGPURenderPassColorAttachment        current_color_attachment_infos[8];
static WGPURenderPassDepthStencilAttachment current_depth_attachment_info;
static int                                  current_viewport[4];
static int                                  current_scissor[4];
static uint8_t                             *readback_staging      = NULL;
static int                                  readback_staging_size = 0;
static bool                                 float32_filterable    = false;

static WGPUTextureFormat convert_image_format(gpu_texture_format_t format) {
	switch (format) {
	case GPU_TEXTURE_FORMAT_RGBA128:
		return WGPUTextureFormat_RGBA32Float;
	case GPU_TEXTURE_FORMAT_RGBA64:
		return WGPUTextureFormat_RGBA16Float;
	case GPU_TEXTURE_FORMAT_R8:
		return WGPUTextureFormat_R8Unorm;
	case GPU_TEXTURE_FORMAT_R16:
		return WGPUTextureFormat_R16Float;
	case GPU_TEXTURE_FORMAT_R32:
		return WGPUTextureFormat_R32Float;
	case GPU_TEXTURE_FORMAT_D32:
		return WGPUTextureFormat_Depth32Float;
	default:
		return WGPUTextureFormat_RGBA8Unorm;
	}
}

static WGPUCullMode convert_cull_mode(gpu_cull_mode_t cull_mode) {
	switch (cull_mode) {
	case GPU_CULL_MODE_CLOCKWISE:
		return WGPUCullMode_Back;
	case GPU_CULL_MODE_COUNTER_CLOCKWISE:
		return WGPUCullMode_Front;
	default:
		return WGPUCullMode_None;
	}
}

static WGPUCompareFunction convert_compare_mode(gpu_compare_mode_t compare) {
	switch (compare) {
	default:
	case GPU_COMPARE_MODE_ALWAYS:
		return WGPUCompareFunction_Always;
	case GPU_COMPARE_MODE_NEVER:
		return WGPUCompareFunction_Never;
	case GPU_COMPARE_MODE_EQUAL:
		return WGPUCompareFunction_Equal;
	case GPU_COMPARE_MODE_LESS:
		return WGPUCompareFunction_Less;
	}
}

static WGPUBlendFactor convert_blend_factor(gpu_blend_t factor) {
	switch (factor) {
	case GPU_BLEND_ONE:
		return WGPUBlendFactor_One;
	case GPU_BLEND_ZERO:
		return WGPUBlendFactor_Zero;
	case GPU_BLEND_SOURCE_ALPHA:
		return WGPUBlendFactor_SrcAlpha;
	case GPU_BLEND_DEST_ALPHA:
		return WGPUBlendFactor_DstAlpha;
	case GPU_BLEND_INV_SOURCE_ALPHA:
		return WGPUBlendFactor_OneMinusSrcAlpha;
	case GPU_BLEND_INV_DEST_ALPHA:
		return WGPUBlendFactor_OneMinusDstAlpha;
	default:
		return WGPUBlendFactor_One;
	}
}

static int bytes_per_row_align(int bpr) {
	return (bpr + 255) & ~255;
}

static bool unfilterable_texture_bound(void) {
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		if (current_textures[i] == NULL) {
			continue;
		}
		gpu_texture_format_t format = current_textures[i]->format;
		if (format == GPU_TEXTURE_FORMAT_D32) {
			return true;
		}
		if (!float32_filterable && (format == GPU_TEXTURE_FORMAT_RGBA128 || format == GPU_TEXTURE_FORMAT_R32)) {
			return true;
		}
	}
	return false;
}

static void create_descriptors(void) {
	WGPUBindGroupLayoutEntry bindings[18];
	memset(bindings, 0, sizeof(bindings));

	bindings[0].binding                 = 0;
	bindings[0].visibility              = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	bindings[0].buffer.type             = WGPUBufferBindingType_Uniform;
	bindings[0].buffer.hasDynamicOffset = true;
	bindings[0].buffer.minBindingSize   = 0;

	bindings[1].binding      = 1;
	bindings[1].visibility   = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	bindings[1].sampler.type = WGPUSamplerBindingType_Filtering;

	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		bindings[2 + i].binding               = 2 + i;
		bindings[2 + i].visibility            = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
		bindings[2 + i].texture.sampleType    = WGPUTextureSampleType_Float;
		bindings[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
		bindings[2 + i].texture.multisampled  = false;
	}

	WGPUBindGroupLayoutDescriptor layout_create_info = {
	    .entryCount = 2 + GPU_MAX_TEXTURES,
	    .entries    = bindings,
	};
	descriptor_layout = wgpuDeviceCreateBindGroupLayout(device, &layout_create_info);

	bindings[1].sampler.type = WGPUSamplerBindingType_NonFiltering;
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		bindings[2 + i].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
	}
	descriptor_layout_unfilterable = wgpuDeviceCreateBindGroupLayout(device, &layout_create_info);

	WGPUTextureDescriptor dummy_desc = {
	    .size          = {1, 1, 1},
	    .mipLevelCount = 1,
	    .sampleCount   = 1,
	    .dimension     = WGPUTextureDimension_2D,
	    .format        = WGPUTextureFormat_RGBA8Unorm,
	    .usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
	};
	dummy_texture = wgpuDeviceCreateTexture(device, &dummy_desc);

	WGPUTextureViewDescriptor dummy_view_desc = {
	    .dimension       = WGPUTextureViewDimension_2D,
	    .format          = WGPUTextureFormat_RGBA8Unorm,
	    .mipLevelCount   = 1,
	    .arrayLayerCount = 1,
	    .aspect          = WGPUTextureAspect_All,
	};
	dummy_view = wgpuTextureCreateView(dummy_texture, &dummy_view_desc);

	uint8_t              white[4]          = {255, 255, 255, 255};
	WGPUBufferDescriptor dummy_upload_desc = {.size = 4, .usage = WGPUBufferUsage_CopySrc, .mappedAtCreation = true};
	WGPUBuffer           dummy_upload      = wgpuDeviceCreateBuffer(device, &dummy_upload_desc);
	memcpy(wgpuBufferGetMappedRange(dummy_upload, 0, 4), white, 4);
	wgpuBufferUnmap(dummy_upload);

	WGPUCommandEncoder       dummy_encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
	WGPUTexelCopyBufferInfo  src           = {.layout = {.bytesPerRow = bytes_per_row_align(4), .rowsPerImage = 1}, .buffer = dummy_upload};
	WGPUTexelCopyTextureInfo dst           = {.texture = dummy_texture};
	WGPUExtent3D             extent        = {1, 1, 1};
	wgpuCommandEncoderCopyBufferToTexture(dummy_encoder, &src, &dst, &extent);
	WGPUCommandBuffer dummy_cmd = wgpuCommandEncoderFinish(dummy_encoder, NULL);
	wgpuQueueSubmit(queue, 1, &dummy_cmd);
	wgpuBufferRelease(dummy_upload);

	WGPUSamplerDescriptor sampler_info = {
	    .addressModeU  = WGPUAddressMode_Repeat,
	    .addressModeV  = WGPUAddressMode_Repeat,
	    .addressModeW  = WGPUAddressMode_Repeat,
	    .magFilter     = WGPUFilterMode_Linear,
	    .minFilter     = WGPUFilterMode_Linear,
	    .mipmapFilter  = WGPUMipmapFilterMode_Linear,
	    .maxAnisotropy = 1,
	};
	linear_sampler = wgpuDeviceCreateSampler(device, &sampler_info);

	sampler_info.magFilter    = WGPUFilterMode_Nearest;
	sampler_info.minFilter    = WGPUFilterMode_Nearest;
	sampler_info.mipmapFilter = WGPUMipmapFilterMode_Nearest;
	point_sampler             = wgpuDeviceCreateSampler(device, &sampler_info);
}

void gpu_barrier(gpu_texture_t *render_target, gpu_texture_state_t state_after) {}

void gpu_render_target_init2(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format, int framebuffer_index) {
	target->width  = width;
	target->height = height;
	target->format = format;
	target->state  = (framebuffer_index >= 0) ? GPU_TEXTURE_STATE_PRESENT : GPU_TEXTURE_STATE_SHADER_RESOURCE;

	if (framebuffer_index >= 0) {
		return;
	}

	WGPUTextureFormat wgpu_format = convert_image_format(format);
	WGPUTextureUsage  usage       = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
	if (format == GPU_TEXTURE_FORMAT_D32) {
		usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
	}

	WGPUTextureDescriptor image = {
	    .size          = {(uint32_t)width, (uint32_t)height, 1},
	    .mipLevelCount = 1,
	    .sampleCount   = 1,
	    .dimension     = WGPUTextureDimension_2D,
	    .format        = wgpu_format,
	    .usage         = usage,
	};
	target->impl.texture = wgpuDeviceCreateTexture(device, &image);

	WGPUTextureViewDescriptor view_desc = {
	    .dimension       = WGPUTextureViewDimension_2D,
	    .format          = wgpu_format,
	    .mipLevelCount   = 1,
	    .arrayLayerCount = 1,
	    .aspect          = format == GPU_TEXTURE_FORMAT_D32 ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All,
	};
	target->impl.view = wgpuTextureCreateView(target->impl.texture, &view_desc);
}

static void create_swapchain() {
	uint32_t width  = iron_window_width();
	uint32_t height = iron_window_height();
	current_width   = width;
	current_height  = height;

	WGPUSurfaceConfiguration swapchain_info = {
	    .device          = device,
	    .format          = surface_format,
	    .usage           = WGPUTextureUsage_RenderAttachment,
	    .viewFormatCount = 1,
	    .viewFormats     = &surface_format,
	    .alphaMode       = WGPUCompositeAlphaMode_Opaque,
	    .width           = width,
	    .height          = height,
	    .presentMode     = window_vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Mailbox,
	};
	wgpuSurfaceConfigure(surface, &swapchain_info);

	framebuffer_index = 0;
	if (window_depth_bits > 0) {
		gpu_render_target_init2(&framebuffer_depth, width, height, GPU_TEXTURE_FORMAT_D32, -1);
	}
}

void gpu_resize_internal(int width, int height) {}

// static void adapter_request_callback(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *userdata2) {
// 	*(WGPUAdapter *)userdata1 = adapter;
// }

// static void device_request_callback(WGPURequestDeviceStatus status, WGPUDevice dev, WGPUStringView message, void *userdata1, void *userdata2) {
// 	*(WGPUDevice *)userdata1 = dev;
// }

void gpu_init_internal(int depth_buffer_bits, bool vsync) {
	instance          = wgpuCreateInstance(NULL);
	window_depth_bits = depth_buffer_bits;
	window_vsync      = vsync;

	// WGPURequestAdapterOptions options = {.compatibleSurface = surface, .powerPreference = WGPUPowerPreference_HighPerformance};
	// WGPURequestAdapterCallbackInfo adapter_cbi = {.callback = adapter_request_callback, .userdata1 = &gpu};
	// wgpuInstanceRequestAdapter(instance, &options, adapter_cbi);
	// while (gpu == NULL) {
	// sleep(10);
	// }
	gpu = wgpuInstanceRequestAdapterSync();

	// WGPUAdapterInfo props = {0};
	// wgpuAdapterGetInfo(gpu, &props);
	// if (props.device) {
	// 	strncpy(device_name, props.device, sizeof(device_name) - 1);
	// 	device_name[sizeof(device_name) - 1] = '\0';
	// }
	// wgpuAdapterInfoFreeMembers(props);

	// WGPUDeviceDescriptor          deviceinfo = {0};
	// WGPURequestDeviceCallbackInfo device_cbi = {.callback = device_request_callback, .userdata1 = &device};
	// wgpuAdapterRequestDevice(gpu, &deviceinfo, device_cbi);
	// while (device == NULL) {
	// 	sleep(10);
	// }
	device = wgpuAdapterRequestDeviceSync();

	queue              = wgpuDeviceGetQueue(device);
	float32_filterable = wgpuDeviceHasFeature(device, WGPUFeatureName_Float32Filterable);
	create_descriptors();

	// WGPUSurfaceCapabilities caps = {0};
	// wgpuSurfaceGetCapabilities(surface, gpu, &caps);
	// surface_format = caps.formats[0];
	// wgpuSurfaceCapabilitiesFreeMembers(caps);
	surface_format = WGPUTextureFormat_RGBA8Unorm;

	gpu_create_framebuffers(depth_buffer_bits);
	create_swapchain();
}

void gpu_begin_internal(gpu_clear_t flags, unsigned color, float depth) {
	int width  = iron_window_width();
	int height = iron_window_height();

	if (width != current_width || height != current_height) {
		create_swapchain();
	}

	if (!framebuffer_acquired && current_render_targets[0] == &framebuffers[framebuffer_index]) {
		WGPUSurfaceTexture surface_texture;
		wgpuSurfaceGetCurrentTexture(surface, &surface_texture);
		framebuffers[0].impl.texture        = surface_texture.texture;
		WGPUTextureViewDescriptor view_info = {
		    .dimension       = WGPUTextureViewDimension_2D,
		    .format          = WGPUTextureFormat_RGBA8Unorm,
		    .mipLevelCount   = 1,
		    .arrayLayerCount = 1,
		};
		framebuffers[0].impl.view = wgpuTextureCreateView(surface_texture.texture, &view_info);
		framebuffers[0].width     = width;
		framebuffers[0].height    = height;
		framebuffer_acquired      = true;
	}

	if (command_encoder == NULL) {
		command_encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
	}

	gpu_texture_t *target      = current_render_targets[0];
	WGPUColor      clear_value = {
	         .r = ((color & 0x00ff0000) >> 16) / 255.0,
	         .g = ((color & 0x0000ff00) >> 8) / 255.0,
	         .b = ((color & 0x000000ff)) / 255.0,
	         .a = ((color & 0xff000000) >> 24) / 255.0,
    };

	for (size_t i = 0; i < current_render_targets_count; ++i) {
		current_color_attachment_infos[i].view       = current_render_targets[i]->impl.view;
		current_color_attachment_infos[i].loadOp     = (flags & GPU_CLEAR_COLOR) ? WGPULoadOp_Clear : WGPULoadOp_Load;
		current_color_attachment_infos[i].storeOp    = WGPUStoreOp_Store;
		current_color_attachment_infos[i].clearValue = clear_value;
	}

	if (current_depth_buffer != NULL) {
		current_depth_attachment_info.view            = current_depth_buffer->impl.view;
		current_depth_attachment_info.depthLoadOp     = (flags & GPU_CLEAR_DEPTH) ? WGPULoadOp_Clear : WGPULoadOp_Load;
		current_depth_attachment_info.depthStoreOp    = WGPUStoreOp_Store;
		current_depth_attachment_info.depthClearValue = depth;
	}

	WGPURenderPassDescriptor render_pass_desc = {
	    .colorAttachmentCount   = (uint32_t)current_render_targets_count,
	    .colorAttachments       = current_color_attachment_infos,
	    .depthStencilAttachment = current_depth_buffer ? &current_depth_attachment_info : NULL,
	};
	render_pass_encoder = wgpuCommandEncoderBeginRenderPass(command_encoder, &render_pass_desc);

	gpu_viewport(0, 0, target->width, target->height);
	gpu_scissor(0, 0, target->width, target->height);
}

void gpu_end_internal() {
	wgpuRenderPassEncoderEnd(render_pass_encoder);
	wgpuRenderPassEncoderRelease(render_pass_encoder);
	render_pass_encoder = NULL;
}

static void end_render_pass(void) {
	if (render_pass_encoder == NULL) {
		return;
	}
	wgpuRenderPassEncoderEnd(render_pass_encoder);
	wgpuRenderPassEncoderRelease(render_pass_encoder);
	render_pass_encoder = NULL;
}

static void submit_command_encoder(void) {
	if (command_encoder == NULL) {
		command_encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
		return;
	}
	WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(command_encoder, NULL);
	wgpuQueueSubmit(queue, 1, &command_buffer);
	wgpuCommandBufferRelease(command_buffer);
	wgpuCommandEncoderRelease(command_encoder);
	command_encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
}

static void restore_render_pass(void) {
	for (size_t i = 0; i < (size_t)current_render_targets_count; ++i) {
		current_color_attachment_infos[i].loadOp = WGPULoadOp_Load;
	}
	if (current_depth_buffer != NULL) {
		current_depth_attachment_info.depthLoadOp = WGPULoadOp_Load;
	}
	WGPURenderPassDescriptor render_pass_desc = {
	    .colorAttachmentCount   = (uint32_t)current_render_targets_count,
	    .colorAttachments       = current_color_attachment_infos,
	    .depthStencilAttachment = current_depth_buffer ? &current_depth_attachment_info : NULL,
	};
	render_pass_encoder = wgpuCommandEncoderBeginRenderPass(command_encoder, &render_pass_desc);

	if (current_pipeline != NULL && current_pipeline->impl.pipeline != NULL) {
		wgpuRenderPassEncoderSetPipeline(render_pass_encoder, current_pipeline->impl.pipeline);
	}
	if (current_vb != NULL) {
		wgpuRenderPassEncoderSetVertexBuffer(render_pass_encoder, 0, current_vb->impl.buf, 0, current_vb->impl.allocated_size);
	}
	if (current_ib != NULL) {
		wgpuRenderPassEncoderSetIndexBuffer(render_pass_encoder, current_ib->impl.buf, WGPUIndexFormat_Uint32, 0, current_ib->impl.allocated_size);
	}
	gpu_viewport(current_viewport[0], current_viewport[1], current_viewport[2], current_viewport[3]);
	gpu_scissor(current_scissor[0], current_scissor[1], current_scissor[2], current_scissor[3]);
}

void gpu_execute_and_wait() {
	bool in_render_pass = (render_pass_encoder != NULL);
	end_render_pass();
	submit_command_encoder();
	if (in_render_pass) {
		restore_render_pass();
	}
}

void gpu_present_internal() {
	WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(command_encoder, NULL);
	wgpuQueueSubmit(queue, 1, &command_buffer);
	wgpuCommandBufferRelease(command_buffer);
	wgpuSurfacePresent(surface);
	wgpuCommandEncoderRelease(command_encoder);
	command_encoder      = NULL;
	framebuffer_acquired = false;
}

void gpu_draw_internal() {
	if (unfilterable_texture_bound()) {
		wgpuRenderPassEncoderSetPipeline(render_pass_encoder, current_pipeline->impl.pipeline_unfilterable);
	}
	wgpuRenderPassEncoderDrawIndexed(render_pass_encoder, current_ib->count, 1, 0, 0, 0);
}

void gpu_viewport(int x, int y, int width, int height) {
	current_viewport[0] = x;
	current_viewport[1] = y;
	current_viewport[2] = width;
	current_viewport[3] = height;
	wgpuRenderPassEncoderSetViewport(render_pass_encoder, (float)x, (float)y, (float)width, (float)height, 0.0f, 1.0f);
}

void gpu_scissor(int x, int y, int width, int height) {
	if (width < 0 || height < 0) {
		return;
	}
	current_scissor[0] = x;
	current_scissor[1] = y;
	current_scissor[2] = width;
	current_scissor[3] = height;
	wgpuRenderPassEncoderSetScissorRect(render_pass_encoder, (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height);
}

void gpu_disable_scissor() {
	gpu_scissor(0, 0, current_render_targets[0]->width, current_render_targets[0]->height);
}

void gpu_set_pipeline_internal(gpu_pipeline_t *pipeline) {
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		current_textures[i] = NULL;
	}
	current_pipeline = pipeline;
	wgpuRenderPassEncoderSetPipeline(render_pass_encoder, current_pipeline->impl.pipeline);
}

void gpu_set_vertex_buffer(gpu_buffer_t *buffer) {
	current_vb = buffer;
	wgpuRenderPassEncoderSetVertexBuffer(render_pass_encoder, 0, buffer->impl.buf, 0, buffer->impl.allocated_size);
}

void gpu_set_index_buffer(gpu_buffer_t *buffer) {
	current_ib = buffer;
	wgpuRenderPassEncoderSetIndexBuffer(render_pass_encoder, buffer->impl.buf, WGPUIndexFormat_Uint32, 0, buffer->impl.allocated_size);
}

void gpu_get_render_target_pixels(gpu_texture_t *render_target, uint8_t *data) {
	int row_size    = render_target->width * gpu_texture_format_size(render_target->format);
	int aligned_bpr = bytes_per_row_align(row_size);
	int buffer_size = aligned_bpr * render_target->height;

	int new_readback_buffer_size = buffer_size > (2048 * 2048 * 4) ? buffer_size : (2048 * 2048 * 4);
	if (readback_buffer_size < new_readback_buffer_size) {
		if (readback_buffer_size > 0) {
			wgpuBufferDestroy(readback_buffer);
			wgpuBufferRelease(readback_buffer);
		}
		readback_buffer_size               = new_readback_buffer_size;
		WGPUBufferDescriptor readback_desc = {
		    .size  = readback_buffer_size,
		    .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
		};
		readback_buffer = wgpuDeviceCreateBuffer(device, &readback_desc);
	}

	bool in_render_pass = (render_pass_encoder != NULL);
	end_render_pass();
	if (command_encoder == NULL) {
		command_encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
	}

	WGPUTexelCopyTextureInfo src    = {.texture = render_target->impl.texture};
	WGPUTexelCopyBufferInfo  dst    = {.layout = {.bytesPerRow = aligned_bpr, .rowsPerImage = render_target->height}, .buffer = readback_buffer};
	WGPUExtent3D             extent = {(uint32_t)render_target->width, (uint32_t)render_target->height, 1};
	wgpuCommandEncoderCopyTextureToBuffer(command_encoder, &src, &dst, &extent);

	submit_command_encoder();

	if (aligned_bpr == row_size) {
		wgpuBufferMapRead(readback_buffer, 0, buffer_size, data);
	}
	else {
		if (readback_staging_size < buffer_size) {
			free(readback_staging);
			readback_staging      = malloc(buffer_size);
			readback_staging_size = buffer_size;
		}
		wgpuBufferMapRead(readback_buffer, 0, buffer_size, readback_staging);
		for (int row = 0; row < (int)render_target->height; ++row) {
			memcpy(data + row * row_size, readback_staging + row * aligned_bpr, row_size);
		}
	}

	framebuffer_acquired = false;

	if (in_render_pass) {
		restore_render_pass();
	}
}

static WGPUBindGroup get_descriptor_set(WGPUBuffer buffer) {
	WGPUBindGroupEntry entries[18];
	memset(entries, 0, sizeof(entries));

	bool unfilterable            = unfilterable_texture_bound();
	int  entry_count             = 0;
	entries[entry_count].binding = 0;
	entries[entry_count].buffer  = buffer;
	entries[entry_count].offset  = 0;
	entries[entry_count].size    = GPU_CONSTANT_BUFFER_SIZE;
	entry_count++;

	entries[entry_count].binding = 1;
	entries[entry_count].sampler = (linear_sampling && !unfilterable) ? linear_sampler : point_sampler;
	entry_count++;

	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		entries[entry_count].binding     = 2 + i;
		entries[entry_count].textureView = current_textures[i] ? current_textures[i]->impl.view : dummy_view;
		entry_count++;
	}

	WGPUBindGroupDescriptor desc = {
	    .layout     = unfilterable ? descriptor_layout_unfilterable : descriptor_layout,
	    .entryCount = entry_count,
	    .entries    = entries,
	};
	return wgpuDeviceCreateBindGroup(device, &desc);
}

void gpu_set_constant_buffer(gpu_buffer_t *buffer, uint32_t offset, size_t size) {
	WGPUBindGroup bind_group = get_descriptor_set(buffer->impl.buf);
	uint32_t      offsets[1] = {(uint32_t)offset};
	wgpuRenderPassEncoderSetBindGroup(render_pass_encoder, 0, bind_group, 1, offsets);
	wgpuBindGroupRelease(bind_group);
}

void gpu_set_texture(uint32_t unit, gpu_texture_t *texture) {
	current_textures[unit] = texture;
}

void gpu_use_linear_sampling(bool b) {
	linear_sampling = b;
}

void gpu_pipeline_destroy_internal(gpu_pipeline_t *pipeline) {
	wgpuRenderPipelineRelease(pipeline->impl.pipeline);
	wgpuRenderPipelineRelease(pipeline->impl.pipeline_unfilterable);
	wgpuPipelineLayoutRelease(pipeline->impl.pipeline_layout);
	wgpuPipelineLayoutRelease(pipeline->impl.pipeline_layout_unfilterable);
}

static WGPUShaderModule create_shader_module(const void *code, size_t size) {
	WGPUShaderSourceWGSL wgsl_desc = {
	    .chain.sType = WGPUSType_ShaderSourceWGSL,
	    .code        = {.data = code, .length = size},
	};
	WGPUShaderModuleDescriptor module_desc = {.nextInChain = (WGPUChainedStruct *)&wgsl_desc};
	return wgpuDeviceCreateShaderModule(device, &module_desc);
}

void gpu_pipeline_compile(gpu_pipeline_t *pipeline) {
	WGPUPipelineLayoutDescriptor pipeline_layout_create_info = {
	    .bindGroupLayoutCount = 1,
	    .bindGroupLayouts     = &descriptor_layout,
	};
	pipeline->impl.pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_create_info);

	pipeline_layout_create_info.bindGroupLayouts = &descriptor_layout_unfilterable;
	pipeline->impl.pipeline_layout_unfilterable  = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_create_info);

	WGPURenderPipelineDescriptor pipeline_desc = {0};
	pipeline_desc.layout                       = pipeline->impl.pipeline_layout;
	pipeline_desc.primitive.topology           = WGPUPrimitiveTopology_TriangleList;
	pipeline_desc.primitive.frontFace          = WGPUFrontFace_CCW;
	pipeline_desc.primitive.cullMode           = convert_cull_mode(pipeline->cull_mode);
	pipeline_desc.multisample.count            = 1;
	pipeline_desc.multisample.mask             = ~0u;

	WGPUDepthStencilState ds_state = {
	    .format            = WGPUTextureFormat_Depth32Float,
	    .depthWriteEnabled = pipeline->depth_write,
	    .depthCompare      = convert_compare_mode(pipeline->depth_mode),
	};
	pipeline_desc.depthStencil = pipeline->depth_attachment_bits > 0 ? &ds_state : NULL;

	WGPUColorTargetState color_targets[8];
	WGPUBlendState       blend_states[8];
	for (int i = 0; i < pipeline->color_attachment_count; ++i) {
		color_targets[i].format = convert_image_format(pipeline->color_attachment[i]);
		color_targets[i].writeMask =
		    (pipeline->color_write_mask_red[i] ? WGPUColorWriteMask_Red : 0) | (pipeline->color_write_mask_green[i] ? WGPUColorWriteMask_Green : 0) |
		    (pipeline->color_write_mask_blue[i] ? WGPUColorWriteMask_Blue : 0) | (pipeline->color_write_mask_alpha[i] ? WGPUColorWriteMask_Alpha : 0);

		if (pipeline->blend_source != GPU_BLEND_ONE || pipeline->blend_destination != GPU_BLEND_ZERO || pipeline->alpha_blend_source != GPU_BLEND_ONE ||
		    pipeline->alpha_blend_destination != GPU_BLEND_ZERO) {
			blend_states[i] = (WGPUBlendState){
			    .color = {.srcFactor = convert_blend_factor(pipeline->blend_source),
			              .dstFactor = convert_blend_factor(pipeline->blend_destination),
			              .operation = WGPUBlendOperation_Add},
			    .alpha = {.srcFactor = convert_blend_factor(pipeline->alpha_blend_source),
			              .dstFactor = convert_blend_factor(pipeline->alpha_blend_destination),
			              .operation = WGPUBlendOperation_Add},
			};
			color_targets[i].blend = &blend_states[i];
		}
		else {
			color_targets[i].blend = NULL;
		}
	}

	pipeline_desc.fragment = &(WGPUFragmentState){
	    .module      = create_shader_module(pipeline->fragment_shader->impl.source, pipeline->fragment_shader->impl.length),
	    .entryPoint  = "main",
	    .targetCount = pipeline->color_attachment_count,
	    .targets     = color_targets,
	};

	WGPUVertexBufferLayout vi_buffer = {0};
	vi_buffer.arrayStride            = 0;
	vi_buffer.attributeCount         = pipeline->input_layout->size;

	WGPUVertexAttribute vi_attrs[16];
	uint32_t            offset = 0;
	for (int i = 0; i < pipeline->input_layout->size; ++i) {
		gpu_vertex_element_t element = pipeline->input_layout->elements[i];
		vi_attrs[i].shaderLocation   = i;
		vi_attrs[i].offset           = offset;
		offset += gpu_vertex_data_size(element.data);
		vi_buffer.arrayStride += gpu_vertex_data_size(element.data);

		switch (element.data) {
		case GPU_VERTEX_DATA_F32_1X:
			vi_attrs[i].format = WGPUVertexFormat_Float32;
			break;
		case GPU_VERTEX_DATA_F32_2X:
			vi_attrs[i].format = WGPUVertexFormat_Float32x2;
			break;
		case GPU_VERTEX_DATA_F32_3X:
			vi_attrs[i].format = WGPUVertexFormat_Float32x3;
			break;
		case GPU_VERTEX_DATA_F32_4X:
			vi_attrs[i].format = WGPUVertexFormat_Float32x4;
			break;
		case GPU_VERTEX_DATA_I16_2X_NORM:
			vi_attrs[i].format = WGPUVertexFormat_Snorm16x2;
			break;
		case GPU_VERTEX_DATA_I16_4X_NORM:
			vi_attrs[i].format = WGPUVertexFormat_Snorm16x4;
			break;
		}
	}
	vi_buffer.attributes = vi_attrs;

	pipeline_desc.vertex = (WGPUVertexState){
	    .module      = create_shader_module(pipeline->vertex_shader->impl.source, pipeline->vertex_shader->impl.length),
	    .entryPoint  = "main",
	    .bufferCount = 1,
	    .buffers     = &vi_buffer,
	};

	pipeline->impl.pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);

	pipeline_desc.layout                 = pipeline->impl.pipeline_layout_unfilterable;
	pipeline->impl.pipeline_unfilterable = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);

	wgpuShaderModuleRelease(pipeline_desc.vertex.module);
	wgpuShaderModuleRelease(pipeline_desc.fragment->module);
}

void gpu_shader_init(gpu_shader_t *shader, const void *source, size_t length, gpu_shader_type_t type) {
	shader->impl.length = length;
	shader->impl.source = malloc(length + 1);
	memcpy(shader->impl.source, source, length);
	((char *)shader->impl.source)[length] = '\0';
}

void gpu_shader_destroy(gpu_shader_t *shader) {
	free(shader->impl.source);
	shader->impl.source = NULL;
}

void gpu_texture_init_from_bytes(gpu_texture_t *texture, void *data, uint32_t width, uint32_t height, gpu_texture_format_t format, bool compress) {
	texture->width  = width;
	texture->height = height;
	texture->format = format;
	texture->state  = GPU_TEXTURE_STATE_SHADER_RESOURCE;

	WGPUTextureFormat wgpu_format   = convert_image_format(format);
	int               bpp           = gpu_texture_format_size(format);
	void             *original_data = data;

#ifdef WITH_BC7
	if (compress && gpu_bc7_supported(width, height, format)) {
		texture->format = GPU_TEXTURE_FORMAT_RGBA32_BC7;
		wgpu_format     = WGPUTextureFormat_BC7RGBAUnorm;
		data            = gpu_bc7_compress(data, width, height);
	}
#endif

	int    aligned_bpr = bytes_per_row_align(width * bpp);
	size_t upload_size = width * height * bpp;
	void  *upload_data = data;

#ifdef WITH_BC7
	if (data != original_data) {
		aligned_bpr = ((width + 3) / 4) * 16; // BC7ENC_BLOCK_SIZE
		upload_size = (size_t)aligned_bpr * ((height + 3) / 4);
	}
	else
#endif
	    if (aligned_bpr != width * bpp) {
		upload_size = (size_t)aligned_bpr * height;
		upload_data = malloc(upload_size);
		for (int row = 0; row < height; ++row) {
			memcpy((uint8_t *)upload_data + row * aligned_bpr, (uint8_t *)data + row * width * bpp, width * bpp);
		}
	}

	int new_upload_buffer_size = upload_size > (1024 * 1024 * 4) ? upload_size : (1024 * 1024 * 4);

	if (upload_buffer_size < new_upload_buffer_size) {
		if (upload_buffer_size > 0) {
			wgpuBufferDestroy(upload_buffer);
			wgpuBufferRelease(upload_buffer);
		}
		upload_buffer_size               = new_upload_buffer_size;
		WGPUBufferDescriptor upload_desc = {
		    .size             = upload_buffer_size,
		    .usage            = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
		    .mappedAtCreation = false,
		};
		upload_buffer = wgpuDeviceCreateBuffer(device, &upload_desc);
	}

	wgpuQueueWriteBuffer(queue, upload_buffer, 0, upload_data, upload_size);

	if (upload_data != data) {
		free(upload_data);
	}
#ifdef WITH_BC7
	if (data != original_data) {
		free(data);
	}
#endif

	WGPUTextureDescriptor image_info = {
	    .size          = {(uint32_t)width, (uint32_t)height, 1},
	    .mipLevelCount = 1,
	    .sampleCount   = 1,
	    .dimension     = WGPUTextureDimension_2D,
	    .format        = wgpu_format,
	    .usage         = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding,
	};
	texture->impl.texture = wgpuDeviceCreateTexture(device, &image_info);

	WGPUCommandEncoder       encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
	WGPUTexelCopyBufferInfo  src     = {.layout = {.bytesPerRow = aligned_bpr, .rowsPerImage = height}, .buffer = upload_buffer};
	WGPUTexelCopyTextureInfo dst     = {.texture = texture->impl.texture};
	WGPUExtent3D             extent  = {(uint32_t)width, (uint32_t)height, 1};
	wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &extent);
	WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
	wgpuQueueSubmit(queue, 1, &cmd);
	wgpuCommandBufferRelease(cmd);
	wgpuCommandEncoderRelease(encoder);

	WGPUTextureViewDescriptor view_info = {
	    .dimension       = WGPUTextureViewDimension_2D,
	    .format          = wgpu_format,
	    .mipLevelCount   = 1,
	    .arrayLayerCount = 1,
	};
	texture->impl.view = wgpuTextureCreateView(texture->impl.texture, &view_info);
}

void gpu_texture_destroy_internal(gpu_texture_t *target) {
	wgpuTextureDestroy(target->impl.texture);
	wgpuTextureRelease(target->impl.texture);
	wgpuTextureViewRelease(target->impl.view);
}

void gpu_render_target_init(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format) {
	gpu_render_target_init2(target, width, height, format, -1);
}

static void _gpu_buffer_copy(WGPUBuffer dest, WGPUBuffer source, uint32_t size) {
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
	wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, dest, 0, size);
	WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
	wgpuQueueSubmit(queue, 1, &cmd);
	wgpuCommandBufferRelease(cmd);
	wgpuCommandEncoderRelease(encoder);
}

void gpu_vertex_buffer_init(gpu_buffer_t *buffer, uint32_t count, gpu_vertex_structure_t *structure) {
	buffer->count  = count;
	buffer->stride = 0;
	for (int i = 0; i < structure->size; ++i) {
		buffer->stride += gpu_vertex_data_size(structure->elements[i].data);
	}

	WGPUBufferDescriptor desc   = {.size = buffer->count * buffer->stride, .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst};
	buffer->impl.buf            = wgpuDeviceCreateBuffer(device, &desc);
	buffer->impl.mem            = malloc(buffer->count * buffer->stride);
	buffer->impl.allocated_size = buffer->count * buffer->stride;
}

void *gpu_vertex_buffer_lock(gpu_buffer_t *buffer) {
	return buffer->impl.mem;
}

void gpu_vertex_buffer_unlock(gpu_buffer_t *buffer) {
	wgpuQueueWriteBuffer(queue, buffer->impl.buf, 0, buffer->impl.mem, buffer->count * buffer->stride);
}

void gpu_index_buffer_init(gpu_buffer_t *buffer, uint32_t count) {
	buffer->count  = count;
	buffer->stride = sizeof(uint32_t);

	WGPUBufferDescriptor desc   = {.size = buffer->count * buffer->stride, .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst};
	buffer->impl.buf            = wgpuDeviceCreateBuffer(device, &desc);
	buffer->impl.mem            = malloc(buffer->count * buffer->stride);
	buffer->impl.allocated_size = buffer->count * buffer->stride;
}

void *gpu_index_buffer_lock(gpu_buffer_t *buffer) {
	return buffer->impl.mem;
}

void gpu_index_buffer_unlock(gpu_buffer_t *buffer) {
	wgpuQueueWriteBuffer(queue, buffer->impl.buf, 0, buffer->impl.mem, buffer->count * buffer->stride);
}

void gpu_constant_buffer_init(gpu_buffer_t *buffer, uint32_t size) {
	buffer->count  = size;
	buffer->stride = 1;

	WGPUBufferDescriptor desc = {.size = size, .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .mappedAtCreation = false};
	buffer->impl.buf          = wgpuDeviceCreateBuffer(device, &desc);
	buffer->impl.mem          = malloc(buffer->count * buffer->stride);
}

void gpu_constant_buffer_lock(gpu_buffer_t *buffer, uint32_t start, uint32_t count) {
	buffer->impl.start = start;
	buffer->count      = count;
	buffer->data       = &buffer->impl.mem[start];
}

void gpu_constant_buffer_unlock(gpu_buffer_t *buffer) {
	wgpuQueueWriteBuffer(queue, buffer->impl.buf, buffer->impl.start, buffer->data, buffer->count);
	buffer->data = NULL;
}

void gpu_buffer_destroy_internal(gpu_buffer_t *buffer) {
	wgpuBufferDestroy(buffer->impl.buf);
	wgpuBufferRelease(buffer->impl.buf);
}

char *gpu_device_name() {
	return device_name;
}

bool gpu_bc7_supported(int width, int height, gpu_texture_format_t format) {
	bool bc7_supported = false;
#ifdef WITH_BC7
	// bc7_supported = wgpuDeviceHasFeature(device, WGPUFeatureName_TextureCompressionBC);
#endif
	return bc7_supported && format == GPU_TEXTURE_FORMAT_RGBA32 && width >= 2048 && height >= 2048 && (width & (width - 1)) == 0 &&
	       (height & (height - 1)) == 0;
}

bool gpu_raytrace_supported(void) {
	return false;
}
void gpu_raytrace_pipeline_init(gpu_raytrace_pipeline_t *pipeline, void *shader, int shader_size, gpu_buffer_t *constant_buffer) {}
void gpu_raytrace_pipeline_destroy(gpu_raytrace_pipeline_t *pipeline) {}
void gpu_raytrace_acceleration_structure_init(gpu_acceleration_structure_t *accel) {}
void gpu_raytrace_acceleration_structure_add(gpu_acceleration_structure_t *accel, gpu_buffer_t *vb, gpu_buffer_t *ib, mat4_t transform) {}
void gpu_raytrace_acceleration_structure_build(gpu_acceleration_structure_t *accel, gpu_buffer_t *_vb_full, gpu_buffer_t *_ib_full) {}
void gpu_raytrace_acceleration_structure_destroy(gpu_acceleration_structure_t *accel) {}
void gpu_raytrace_set_textures(gpu_texture_t *texpaint0, gpu_texture_t *texpaint1, gpu_texture_t *texpaint2, gpu_texture_t *texenv, gpu_texture_t *texsobol,
                               gpu_texture_t *texscramble, gpu_texture_t *texrank, gpu_texture_t *texenv_cdf) {}
void gpu_raytrace_set_acceleration_structure(gpu_acceleration_structure_t *accel) {}
void gpu_raytrace_set_pipeline(gpu_raytrace_pipeline_t *pipeline) {}
void gpu_raytrace_set_target(gpu_texture_t *output) {}
void gpu_raytrace_dispatch_rays() {}
