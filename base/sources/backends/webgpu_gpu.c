#include "webgpu_gpu.h"
#include <float.h>
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
	target->width     = width;
	target->height    = height;
	target->format    = format;
	target->state     = (framebuffer_index >= 0) ? GPU_TEXTURE_STATE_PRESENT : GPU_TEXTURE_STATE_SHADER_RESOURCE;
	target->gpu_write = false;

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

	uint32_t             size   = gpu_buffer_alloc_size(buffer->count, buffer->stride);
	WGPUBufferDescriptor desc   = {.size = size, .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst};
	buffer->impl.buf            = wgpuDeviceCreateBuffer(device, &desc);
	buffer->impl.mem            = malloc(size);
	buffer->impl.allocated_size = size;
}

void *gpu_vertex_buffer_lock(gpu_buffer_t *buffer) {
	return buffer->impl.mem;
}

void gpu_vertex_buffer_unlock(gpu_buffer_t *buffer) {
	buffer->version = ++gpu_buffer_versions;
	wgpuQueueWriteBuffer(queue, buffer->impl.buf, 0, buffer->impl.mem, buffer->count * buffer->stride);
}

void gpu_index_buffer_init(gpu_buffer_t *buffer, uint32_t count) {
	buffer->count  = count;
	buffer->stride = sizeof(uint32_t);

	uint32_t             size   = gpu_buffer_alloc_size(buffer->count, buffer->stride);
	WGPUBufferDescriptor desc   = {.size = size, .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst};
	buffer->impl.buf            = wgpuDeviceCreateBuffer(device, &desc);
	buffer->impl.mem            = malloc(size);
	buffer->impl.allocated_size = size;
}

void *gpu_index_buffer_lock(gpu_buffer_t *buffer) {
	return buffer->impl.mem;
}

void gpu_index_buffer_unlock(gpu_buffer_t *buffer) {
	buffer->version = ++gpu_buffer_versions;
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

// Raytracing without hardware support

typedef struct {
	float    min[3];
	uint32_t first; // Left child (the right one follows it), or the first triangle of a leaf
	float    max[3];
	uint32_t count; // Triangles, 0 for inner nodes
} rt_node_t;

typedef struct {
	float    world_to_object[16]; // mat4x3<f32>, columns padded to 16 bytes
	float    object_to_world[12]; // mat3x3<f32>
	uint32_t root;
	uint32_t geometry;
	uint32_t pad[2];
} rt_instance_t;

typedef struct {
	gpu_buffer_t *vb;
	uint32_t      vb_version;
	uint32_t      ib_version;
	rt_node_t    *nodes;
	uint32_t      node_count;
	uint32_t     *indices; // 3 per triangle in leaf order, into the vertices of vb
	uint32_t      tri_count;
} rt_blas_t;

typedef struct {
	uint32_t node;
	uint32_t start;
	uint32_t count;
	uint32_t depth;
} rt_build_item_t;

typedef enum {
	RT_BINDING_UNIFORM,
	RT_BINDING_NODES,
	RT_BINDING_INDICES,
	RT_BINDING_VERTICES,
	RT_BINDING_INSTANCES,
	RT_BINDING_TARGET,
	RT_BINDING_PREV,
	RT_BINDING_SAMPLER,
	RT_BINDING_TEXTURE,
	RT_BINDING_GEOMETRY_TEXTURE,
} rt_binding_kind_t;

typedef struct {
	uint32_t          binding;
	rt_binding_kind_t kind;
	int               index; // Geometry texture 0-2
} rt_binding_t;

typedef struct {
	gpu_texture_format_t format;
	WGPUComputePipeline  pipeline;
	WGPUBindGroupLayout  layout;
	WGPUPipelineLayout   pipeline_layout;
} rt_variant_t;

#define RT_MAX_BINDINGS 32
#define RT_MAX_VARIANTS 4
#define RT_MAX_DEPTH    56 // The shader stack holds 64 nodes
#define RT_LEAF_SIZE    4
#define RT_BINS         12

static gpu_raytrace_pipeline_t *rt_pipeline      = NULL;
static char                    *rt_source        = NULL;
static uint32_t                 rt_constant_size = 0;
static rt_binding_t             rt_bindings[RT_MAX_BINDINGS];
static int                      rt_bindings_count = 0;
static rt_variant_t             rt_variants[RT_MAX_VARIANTS];
static int                      rt_variants_count = 0;
static gpu_texture_t           *rt_output         = NULL;
static WGPUTexture              rt_prev           = NULL;
static WGPUTextureView          rt_prev_view      = NULL;
static gpu_texture_format_t     rt_prev_format;
static uint32_t                 rt_prev_width  = 0;
static uint32_t                 rt_prev_height = 0;
static gpu_texture_t           *rt_textures[8]; // Bindings 3-10
static gpu_texture_t           *rt_geometry_textures[GPU_RAYTRACE_MAX_OBJECTS][3];
static gpu_buffer_t            *rt_vb[GPU_RAYTRACE_MAX_OBJECTS];
static gpu_buffer_t            *rt_ib[GPU_RAYTRACE_MAX_OBJECTS];
static int                      rt_vb_count = 0;
static rt_blas_t                rt_blas[GPU_RAYTRACE_MAX_OBJECTS];
static int                      rt_blas_count = 0;
static struct {
	int    geometry;
	mat4_t transform;
} rt_instances[1024];
static int        rt_instances_count = 0;
static WGPUBuffer rt_buffers[4]; // nodes, indices, vertices, instances
static uint32_t   rt_buffer_sizes[4];

bool gpu_raytrace_supported(void) {
	return true;
}

static void rt_parse_bindings(const char *source) {
	rt_bindings_count = 0;
	const char *c     = source;
	while ((c = strstr(c, "@binding(")) != NULL && rt_bindings_count < RT_MAX_BINDINGS) {
		rt_binding_t *b = &rt_bindings[rt_bindings_count++];
		b->binding      = 0;
		for (const char *d = c + 9; *d >= '0' && *d <= '9'; ++d) {
			b->binding = b->binding * 10 + (*d - '0');
		}
		b->index            = 0;
		const char *var     = strstr(c, " var") + 4;
		const char *eol     = strchr(c, '\n');
		bool        uniform = strncmp(var, "<uniform>", 9) == 0;
		if (*var == '<') {
			var = strchr(var, '>') + 1;
		}
		const char *name = var + 1;
		const char *type = strchr(name, ':') + 2;

		if (uniform) {
			b->kind = RT_BINDING_UNIFORM;
		}
		else if (strncmp(name, "_kong_nodes", 11) == 0) {
			b->kind = RT_BINDING_NODES;
		}
		else if (strncmp(name, "_kong_indices", 13) == 0) {
			b->kind = RT_BINDING_INDICES;
		}
		else if (strncmp(name, "_kong_vertices", 14) == 0) {
			b->kind = RT_BINDING_VERTICES;
		}
		else if (strncmp(name, "_kong_instances", 15) == 0) {
			b->kind = RT_BINDING_INSTANCES;
		}
		else if (strncmp(name, "_kong_prev", 10) == 0) {
			b->kind = RT_BINDING_PREV;
		}
		else if (strncmp(name, "_kong_geometry_texture", 22) == 0) {
			b->kind  = RT_BINDING_GEOMETRY_TEXTURE;
			b->index = name[22] - '0';
		}
		else if (strncmp(type, "texture_storage_2d", 18) == 0) {
			b->kind = RT_BINDING_TARGET;
		}
		else if (strncmp(type, "sampler", 7) == 0) {
			b->kind = RT_BINDING_SAMPLER;
		}
		else {
			b->kind = RT_BINDING_TEXTURE;
		}
		c = eol != NULL ? eol : c + 9;
	}
}

static const char *rt_storage_format(gpu_texture_format_t format) {
	switch (format) {
	case GPU_TEXTURE_FORMAT_RGBA128:
		return "rgba32float";
	case GPU_TEXTURE_FORMAT_RGBA64:
		return "rgba16float";
	default:
		return "rgba8unorm";
	}
}

static void rt_destroy_variants(void) {
	for (int i = 0; i < rt_variants_count; ++i) {
		wgpuComputePipelineRelease(rt_variants[i].pipeline);
		wgpuPipelineLayoutRelease(rt_variants[i].pipeline_layout);
		wgpuBindGroupLayoutRelease(rt_variants[i].layout);
	}
	rt_variants_count = 0;
}

// The storage texture format is part of the shader, a variant is compiled per target format
static rt_variant_t *rt_get_variant(gpu_texture_format_t format) {
	for (int i = 0; i < rt_variants_count; ++i) {
		if (rt_variants[i].format == format) {
			return &rt_variants[i];
		}
	}
	if (rt_variants_count == RT_MAX_VARIANTS) {
		rt_destroy_variants();
	}

	const char *placeholder = "texture_storage_2d<rgba32float";
	const char *format_name = rt_storage_format(format);
	const char *at          = strstr(rt_source, placeholder);
	size_t      length      = strlen(rt_source) + 32;
	char       *source      = malloc(length);
	if (at != NULL) {
		snprintf(source, length, "%.*stexture_storage_2d<%s%s", (int)(at - rt_source), rt_source, format_name, at + strlen(placeholder));
	}
	else {
		strcpy(source, rt_source);
	}

	WGPUBindGroupLayoutEntry entries[RT_MAX_BINDINGS];
	memset(entries, 0, sizeof(entries));
	for (int i = 0; i < rt_bindings_count; ++i) {
		WGPUBindGroupLayoutEntry *e = &entries[i];
		e->binding                  = rt_bindings[i].binding;
		e->visibility               = WGPUShaderStage_Compute;
		switch (rt_bindings[i].kind) {
		case RT_BINDING_UNIFORM:
			e->buffer.type = WGPUBufferBindingType_Uniform;
			break;
		case RT_BINDING_NODES:
		case RT_BINDING_INDICES:
		case RT_BINDING_VERTICES:
		case RT_BINDING_INSTANCES:
			e->buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
			break;
		case RT_BINDING_TARGET:
			e->storageTexture.access        = WGPUStorageTextureAccess_WriteOnly;
			e->storageTexture.format        = convert_image_format(format);
			e->storageTexture.viewDimension = WGPUTextureViewDimension_2D;
			break;
		case RT_BINDING_SAMPLER:
			e->sampler.type = float32_filterable ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
			break;
		default:
			e->texture.sampleType    = float32_filterable ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
			e->texture.viewDimension = WGPUTextureViewDimension_2D;
			break;
		}
	}

	rt_variant_t *v = &rt_variants[rt_variants_count++];
	v->format       = format;

	WGPUBindGroupLayoutDescriptor layout_desc = {.entryCount = rt_bindings_count, .entries = entries};
	v->layout                                 = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);

	WGPUPipelineLayoutDescriptor pipeline_layout_desc = {.bindGroupLayoutCount = 1, .bindGroupLayouts = &v->layout};
	v->pipeline_layout                                = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);

	WGPUShaderModule              module = create_shader_module(source, strlen(source));
	WGPUComputePipelineDescriptor desc   = {
	      .layout  = v->pipeline_layout,
	      .compute = {.module = module, .entryPoint = {.data = "main", .length = 4}},
    };
	v->pipeline = wgpuDeviceCreateComputePipeline(device, &desc);
	wgpuShaderModuleRelease(module);
	free(source);
	return v;
}

void gpu_raytrace_pipeline_init(gpu_raytrace_pipeline_t *pipeline, void *shader, int shader_size, gpu_buffer_t *constant_buffer) {
	pipeline->constant_buffer = constant_buffer;
	rt_constant_size          = constant_buffer->count;
	rt_destroy_variants();
	free(rt_source);
	rt_source = malloc(shader_size + 1);
	memcpy(rt_source, shader, shader_size);
	rt_source[shader_size] = '\0';
	rt_parse_bindings(rt_source);
	rt_output = NULL;
}

void gpu_raytrace_pipeline_destroy(gpu_raytrace_pipeline_t *pipeline) {
	rt_destroy_variants();
}

void gpu_raytrace_acceleration_structure_init(gpu_acceleration_structure_t *accel) {
	rt_vb_count        = 0;
	rt_instances_count = 0;
	memset(rt_geometry_textures, 0, sizeof(rt_geometry_textures));
}

void gpu_raytrace_acceleration_structure_add(gpu_acceleration_structure_t *accel, gpu_buffer_t *vb, gpu_buffer_t *ib, mat4_t transform,
                                             gpu_texture_t **textures) {
	int geometry = -1;
	for (int i = 0; i < rt_vb_count; ++i) {
		if (rt_vb[i] == vb) {
			geometry = i;
			break;
		}
	}
	if (geometry == -1) {
		if (rt_vb_count >= GPU_RAYTRACE_MAX_OBJECTS) {
			return;
		}
		geometry        = rt_vb_count++;
		rt_vb[geometry] = vb;
		rt_ib[geometry] = ib;
		for (int k = 0; k < 3; ++k) {
			rt_geometry_textures[geometry][k] = textures != NULL ? textures[k] : NULL;
		}
	}
	if (rt_instances_count >= (int)(sizeof(rt_instances) / sizeof(rt_instances[0]))) {
		return;
	}
	rt_instances[rt_instances_count].geometry  = geometry;
	rt_instances[rt_instances_count].transform = transform;
	rt_instances_count++;
}

// Matches the R16G16B16A16_SNORM vertex format of the hardware backends
static void rt_vertex_position(gpu_buffer_t *vb, uint32_t index, float *p) {
	int16_t *v = (int16_t *)((uint8_t *)vb->impl.mem + (size_t)index * vb->stride);
	for (int i = 0; i < 3; ++i) {
		float f = v[i] / 32767.0f;
		p[i]    = f < -1.0f ? -1.0f : f;
	}
}

static float rt_area(const float *min, const float *max) {
	float d[3] = {max[0] - min[0], max[1] - min[1], max[2] - min[2]};
	if (d[0] < 0.0f || d[1] < 0.0f || d[2] < 0.0f) {
		return 0.0f;
	}
	return d[0] * d[1] + d[1] * d[2] + d[2] * d[0];
}

static void rt_grow(float *min, float *max, const float *pmin, const float *pmax) {
	for (int i = 0; i < 3; ++i) {
		min[i] = pmin[i] < min[i] ? pmin[i] : min[i];
		max[i] = pmax[i] > max[i] ? pmax[i] : max[i];
	}
}

// Binned SAH builder, children of a node are stored next to each other
static void rt_build_blas(rt_blas_t *blas, gpu_buffer_t *vb, gpu_buffer_t *ib) {
	uint32_t  tri_count = ib->count / 3;
	uint32_t *ib_data   = (uint32_t *)ib->impl.mem;
	float    *bounds    = malloc(sizeof(float) * 6 * tri_count); // min, max
	float    *centroids = malloc(sizeof(float) * 3 * tri_count);
	uint32_t *order     = malloc(sizeof(uint32_t) * tri_count);

	for (uint32_t t = 0; t < tri_count; ++t) {
		float *min = &bounds[t * 6];
		float *max = &bounds[t * 6 + 3];
		float  p[3];
		for (int c = 0; c < 3; ++c) {
			rt_vertex_position(vb, ib_data[t * 3 + c], p);
			for (int i = 0; i < 3; ++i) {
				min[i] = c == 0 || p[i] < min[i] ? p[i] : min[i];
				max[i] = c == 0 || p[i] > max[i] ? p[i] : max[i];
			}
		}
		for (int i = 0; i < 3; ++i) {
			centroids[t * 3 + i] = (min[i] + max[i]) * 0.5f;
		}
		order[t] = t;
	}

	free(blas->nodes);
	free(blas->indices);
	blas->nodes      = malloc(sizeof(rt_node_t) * (tri_count * 2 + 1));
	blas->node_count = 1;
	blas->tri_count  = tri_count;

	rt_build_item_t stack[RT_MAX_DEPTH + 2];
	int             sp = 0;
	stack[sp++]        = (rt_build_item_t){0, 0, tri_count, 0};

	while (sp > 0) {
		rt_build_item_t item = stack[--sp];
		rt_node_t      *node = &blas->nodes[item.node];

		float cmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
		float cmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		for (int i = 0; i < 3; ++i) {
			node->min[i] = FLT_MAX;
			node->max[i] = -FLT_MAX;
		}
		for (uint32_t k = item.start; k < item.start + item.count; ++k) {
			uint32_t t = order[k];
			rt_grow(node->min, node->max, &bounds[t * 6], &bounds[t * 6 + 3]);
			rt_grow(cmin, cmax, &centroids[t * 3], &centroids[t * 3]);
		}

		node->first = item.start;
		node->count = item.count;
		if (item.count <= RT_LEAF_SIZE || item.depth >= RT_MAX_DEPTH) {
			continue;
		}

		int   axis   = 0;
		float extent = cmax[0] - cmin[0];
		for (int i = 1; i < 3; ++i) {
			if (cmax[i] - cmin[i] > extent) {
				axis   = i;
				extent = cmax[i] - cmin[i];
			}
		}

		uint32_t mid = item.start + item.count / 2;
		if (extent > 0.0f) {
			struct {
				float    min[3];
				float    max[3];
				uint32_t count;
			} bins[RT_BINS];
			for (int b = 0; b < RT_BINS; ++b) {
				for (int i = 0; i < 3; ++i) {
					bins[b].min[i] = FLT_MAX;
					bins[b].max[i] = -FLT_MAX;
				}
				bins[b].count = 0;
			}
			float scale = RT_BINS / extent;
			for (uint32_t k = item.start; k < item.start + item.count; ++k) {
				uint32_t t = order[k];
				int      b = (int)((centroids[t * 3 + axis] - cmin[axis]) * scale);
				b          = b >= RT_BINS ? RT_BINS - 1 : b;
				rt_grow(bins[b].min, bins[b].max, &bounds[t * 6], &bounds[t * 6 + 3]);
				bins[b].count++;
			}

			// Sweep from the right, then from the left
			float right_area[RT_BINS];
			float rmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
			float rmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
			for (int b = RT_BINS - 1; b > 0; --b) {
				rt_grow(rmin, rmax, bins[b].min, bins[b].max);
				right_area[b] = rt_area(rmin, rmax);
			}
			float    lmin[3]    = {FLT_MAX, FLT_MAX, FLT_MAX};
			float    lmax[3]    = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
			uint32_t left_count = 0;
			float    best_cost  = FLT_MAX;
			int      best_split = -1;
			for (int b = 0; b < RT_BINS - 1; ++b) {
				rt_grow(lmin, lmax, bins[b].min, bins[b].max);
				left_count += bins[b].count;
				uint32_t right_count = item.count - left_count;
				if (left_count == 0 || right_count == 0) {
					continue;
				}
				float cost = rt_area(lmin, lmax) * left_count + right_area[b + 1] * right_count;
				if (cost < best_cost) {
					best_cost  = cost;
					best_split = b;
				}
			}

			float leaf_cost = rt_area(node->min, node->max) * item.count;
			if (best_split >= 0 && best_cost >= leaf_cost && item.count <= RT_LEAF_SIZE * 4) {
				continue;
			}
			if (best_split >= 0) {
				uint32_t i = item.start;
				uint32_t j = item.start + item.count;
				while (i < j) {
					uint32_t t = order[i];
					int      b = (int)((centroids[t * 3 + axis] - cmin[axis]) * scale);
					b          = b >= RT_BINS ? RT_BINS - 1 : b;
					if (b <= best_split) {
						i++;
					}
					else {
						order[i] = order[--j];
						order[j] = t;
					}
				}
				mid = i;
			}
		}

		uint32_t left = blas->node_count;
		blas->node_count += 2;
		node->first = left;
		node->count = 0;
		stack[sp++] = (rt_build_item_t){left, item.start, mid - item.start, item.depth + 1};
		stack[sp++] = (rt_build_item_t){left + 1, mid, item.start + item.count - mid, item.depth + 1};
	}

	blas->indices = malloc(sizeof(uint32_t) * 3 * (tri_count > 0 ? tri_count : 1));
	for (uint32_t k = 0; k < tri_count; ++k) {
		for (int c = 0; c < 3; ++c) {
			blas->indices[k * 3 + c] = ib_data[order[k] * 3 + c];
		}
	}

	free(bounds);
	free(centroids);
	free(order);
}

static void rt_upload(int slot, const void *data, uint32_t size) {
	size = (size + 3) & ~3u;
	if (rt_buffers[slot] == NULL || rt_buffer_sizes[slot] != size) {
		if (rt_buffers[slot] != NULL) {
			wgpuBufferDestroy(rt_buffers[slot]);
			wgpuBufferRelease(rt_buffers[slot]);
		}
		WGPUBufferDescriptor desc = {.size = size, .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst};
		rt_buffers[slot]          = wgpuDeviceCreateBuffer(device, &desc);
		rt_buffer_sizes[slot]     = size;
	}
	wgpuQueueWriteBuffer(queue, rt_buffers[slot], 0, data, size);
}

static void rt_write_instance(rt_instance_t *out, mat4_t transform, uint32_t root, uint32_t geometry) {
	float *m = transform.m;
	// Linear part a (columns m[0..2], m[4..6], m[8..10]) and its inverse
	float a00 = m[0], a10 = m[1], a20 = m[2];
	float a01 = m[4], a11 = m[5], a21 = m[6];
	float a02 = m[8], a12 = m[9], a22 = m[10];
	float c00 = a11 * a22 - a12 * a21, c01 = a02 * a21 - a01 * a22, c02 = a01 * a12 - a02 * a11;
	float c10 = a12 * a20 - a10 * a22, c11 = a00 * a22 - a02 * a20, c12 = a02 * a10 - a00 * a12;
	float c20 = a10 * a21 - a11 * a20, c21 = a01 * a20 - a00 * a21, c22 = a00 * a11 - a01 * a10;
	float det     = a00 * c00 + a01 * c10 + a02 * c20;
	float inv     = det != 0.0f ? 1.0f / det : 0.0f;
	float i[3][3] = {{c00 * inv, c01 * inv, c02 * inv}, {c10 * inv, c11 * inv, c12 * inv}, {c20 * inv, c21 * inv, c22 * inv}}; // [row][col]
	float t[3]    = {m[12], m[13], m[14]};

	memset(out, 0, sizeof(*out));
	for (int col = 0; col < 3; ++col) {
		for (int row = 0; row < 3; ++row) {
			out->world_to_object[col * 4 + row] = i[row][col];
			out->object_to_world[col * 4 + row] = m[col * 4 + row];
		}
	}
	for (int row = 0; row < 3; ++row) {
		out->world_to_object[12 + row] = -(i[row][0] * t[0] + i[row][1] * t[1] + i[row][2] * t[2]);
	}
	out->root     = root;
	out->geometry = geometry;
}

void gpu_raytrace_acceleration_structure_build(gpu_acceleration_structure_t *accel) {
	// Bottom level, rebuilt only for changed meshes
	for (int i = 0; i < rt_vb_count; ++i) {
		rt_blas_t *blas = &rt_blas[i];
		if (i >= rt_blas_count || blas->vb != rt_vb[i] || blas->vb_version != rt_vb[i]->version || blas->ib_version != rt_ib[i]->version) {
			rt_build_blas(blas, rt_vb[i], rt_ib[i]);
			blas->vb         = rt_vb[i];
			blas->vb_version = rt_vb[i]->version;
			blas->ib_version = rt_ib[i]->version;
		}
	}
	rt_blas_count = rt_vb_count > rt_blas_count ? rt_vb_count : rt_blas_count;

	uint32_t node_count   = 1; // Node 0 is an empty root for the empty scene
	uint32_t index_count  = 0;
	uint32_t vertex_count = 0;
	for (int i = 0; i < rt_vb_count; ++i) {
		node_count += rt_blas[i].node_count;
		index_count += rt_blas[i].tri_count * 3;
		vertex_count += rt_vb[i]->count;
	}

	rt_node_t *nodes    = malloc(sizeof(rt_node_t) * node_count);
	uint32_t  *indices  = malloc(sizeof(uint32_t) * (index_count > 0 ? index_count : 1));
	uint32_t  *vertices = malloc(sizeof(uint32_t) * 4 * (vertex_count > 0 ? vertex_count : 1));
	uint32_t   roots[GPU_RAYTRACE_MAX_OBJECTS];
	uint32_t   node_offset   = 1;
	uint32_t   tri_offset    = 0;
	uint32_t   vertex_offset = 0;

	nodes[0]   = (rt_node_t){.min = {1e30f, 1e30f, 1e30f}, .max = {1e30f, 1e30f, 1e30f}};
	indices[0] = 0;
	memset(vertices, 0, sizeof(uint32_t) * 4);

	for (int i = 0; i < rt_vb_count; ++i) {
		rt_blas_t *blas = &rt_blas[i];
		roots[i]        = node_offset;
		for (uint32_t n = 0; n < blas->node_count; ++n) {
			rt_node_t node = blas->nodes[n];
			node.first += node.count > 0 ? tri_offset : node_offset;
			nodes[node_offset + n] = node;
		}
		for (uint32_t k = 0; k < blas->tri_count * 3; ++k) {
			indices[tri_offset * 3 + k] = blas->indices[k] + vertex_offset;
		}
		gpu_buffer_t *vb = rt_vb[i];
		for (uint32_t v = 0; v < vb->count; ++v) {
			memcpy(&vertices[(vertex_offset + v) * 4], (uint8_t *)vb->impl.mem + (size_t)v * vb->stride, 16); // posxy, poszw, nor, tex
		}
		node_offset += blas->node_count;
		tri_offset += blas->tri_count;
		vertex_offset += vb->count;
	}

	int            instance_count = rt_instances_count > 0 ? rt_instances_count : 1;
	rt_instance_t *instances      = malloc(sizeof(rt_instance_t) * instance_count);
	if (rt_instances_count == 0) {
		rt_write_instance(&instances[0], mat4_identity(), 0, 0);
	}
	for (int i = 0; i < rt_instances_count; ++i) {
		rt_write_instance(&instances[i], rt_instances[i].transform, roots[rt_instances[i].geometry], rt_instances[i].geometry);
	}

	rt_upload(0, nodes, sizeof(rt_node_t) * node_count);
	rt_upload(1, indices, sizeof(uint32_t) * (index_count > 0 ? index_count : 1));
	rt_upload(2, vertices, sizeof(uint32_t) * 4 * (vertex_count > 0 ? vertex_count : 1));
	rt_upload(3, instances, sizeof(rt_instance_t) * instance_count);

	free(nodes);
	free(indices);
	free(vertices);
	free(instances);
}

void gpu_raytrace_acceleration_structure_destroy(gpu_acceleration_structure_t *accel) {}

void gpu_raytrace_set_textures(gpu_texture_t *texpaint0, gpu_texture_t *texpaint1, gpu_texture_t *texpaint2, gpu_texture_t *texenv, gpu_texture_t *texsobol,
                               gpu_texture_t *texscramble, gpu_texture_t *texrank, gpu_texture_t *texenv_cdf) {
	rt_textures[0] = texpaint0;
	rt_textures[1] = texpaint1;
	rt_textures[2] = texpaint2;
	rt_textures[3] = texenv;
	rt_textures[4] = texsobol;
	rt_textures[5] = texscramble;
	rt_textures[6] = texrank;
	rt_textures[7] = texenv_cdf != NULL ? texenv_cdf : texenv;
}

void gpu_raytrace_set_acceleration_structure(gpu_acceleration_structure_t *accel) {}

void gpu_raytrace_set_pipeline(gpu_raytrace_pipeline_t *pipeline) {
	rt_pipeline = pipeline;
}

void gpu_raytrace_set_target(gpu_texture_t *output) {
	if (!output->gpu_write) {
		output->gpu_write = true;
		gpu_texture_destroy(output);

		WGPUTextureFormat     format = convert_image_format(output->format);
		WGPUTextureDescriptor image  = {
		     .size          = {output->width, output->height, 1},
		     .mipLevelCount = 1,
		     .sampleCount   = 1,
		     .dimension     = WGPUTextureDimension_2D,
		     .format        = format,
		     .usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_StorageBinding,
        };
		output->impl.texture = wgpuDeviceCreateTexture(device, &image);

		WGPUTextureViewDescriptor view_desc = {
		    .dimension       = WGPUTextureViewDimension_2D,
		    .format          = format,
		    .mipLevelCount   = 1,
		    .arrayLayerCount = 1,
		    .aspect          = WGPUTextureAspect_All,
		};
		output->impl.view = wgpuTextureCreateView(output->impl.texture, &view_desc);
	}
	rt_output = output;
}

// Storage textures are write-only, the shader reads the previous result from a copy
static void rt_update_prev(void) {
	if (rt_prev != NULL && rt_prev_width == rt_output->width && rt_prev_height == rt_output->height && rt_prev_format == rt_output->format) {
		return;
	}
	if (rt_prev != NULL) {
		wgpuTextureViewRelease(rt_prev_view);
		wgpuTextureDestroy(rt_prev);
		wgpuTextureRelease(rt_prev);
	}
	rt_prev_width  = rt_output->width;
	rt_prev_height = rt_output->height;
	rt_prev_format = rt_output->format;

	WGPUTextureFormat     format = convert_image_format(rt_prev_format);
	WGPUTextureDescriptor image  = {
	     .size          = {rt_prev_width, rt_prev_height, 1},
	     .mipLevelCount = 1,
	     .sampleCount   = 1,
	     .dimension     = WGPUTextureDimension_2D,
	     .format        = format,
	     .usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
    };
	rt_prev                             = wgpuDeviceCreateTexture(device, &image);
	WGPUTextureViewDescriptor view_desc = {
	    .dimension       = WGPUTextureViewDimension_2D,
	    .format          = format,
	    .mipLevelCount   = 1,
	    .arrayLayerCount = 1,
	    .aspect          = WGPUTextureAspect_All,
	};
	rt_prev_view = wgpuTextureCreateView(rt_prev, &view_desc);
}

static WGPUTextureView rt_view(gpu_texture_t *texture) {
	return texture != NULL ? texture->impl.view : dummy_view;
}

void gpu_raytrace_dispatch_rays() {
	if (rt_source == NULL || rt_output == NULL || rt_buffers[0] == NULL) {
		return;
	}

	bool reads_target = false;
	for (int i = 0; i < rt_bindings_count; ++i) {
		if (rt_bindings[i].kind == RT_BINDING_PREV) {
			reads_target = true;
		}
	}
	if (reads_target) {
		rt_update_prev();
	}

	rt_variant_t      *variant = rt_get_variant(rt_output->format);
	WGPUBindGroupEntry entries[RT_MAX_BINDINGS];
	memset(entries, 0, sizeof(entries));
	for (int i = 0; i < rt_bindings_count; ++i) {
		WGPUBindGroupEntry *e = &entries[i];
		rt_binding_t       *b = &rt_bindings[i];
		e->binding            = b->binding;
		switch (b->kind) {
		case RT_BINDING_UNIFORM:
			e->buffer = rt_pipeline->constant_buffer->impl.buf;
			e->size   = rt_constant_size;
			break;
		case RT_BINDING_NODES:
		case RT_BINDING_INDICES:
		case RT_BINDING_VERTICES:
		case RT_BINDING_INSTANCES: {
			int slot  = b->kind - RT_BINDING_NODES;
			e->buffer = rt_buffers[slot];
			e->size   = rt_buffer_sizes[slot];
			break;
		}
		case RT_BINDING_TARGET:
			e->textureView = rt_output->impl.view;
			break;
		case RT_BINDING_PREV:
			e->textureView = rt_prev_view;
			break;
		case RT_BINDING_SAMPLER:
			e->sampler = float32_filterable ? linear_sampler : point_sampler;
			break;
		case RT_BINDING_GEOMETRY_TEXTURE: {
			// One texture set fits in the sampled texture limit, per mesh textures are not supported yet
			gpu_texture_t *t = rt_geometry_textures[0][b->index];
			e->textureView   = rt_view(t != NULL ? t : rt_textures[b->index]);
			break;
		}
		default:
			e->textureView = rt_view(b->binding >= 3 && b->binding < 11 ? rt_textures[b->binding - 3] : NULL);
			break;
		}
	}

	WGPUBindGroupDescriptor bind_group_desc = {.layout = variant->layout, .entryCount = rt_bindings_count, .entries = entries};
	WGPUBindGroup           bind_group      = wgpuDeviceCreateBindGroup(device, &bind_group_desc);

	bool in_render_pass = render_pass_encoder != NULL;
	end_render_pass();
	if (command_encoder == NULL) {
		command_encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
	}

	if (reads_target) {
		WGPUTexelCopyTextureInfo src    = {.texture = rt_output->impl.texture};
		WGPUTexelCopyTextureInfo dst    = {.texture = rt_prev};
		WGPUExtent3D             extent = {rt_output->width, rt_output->height, 1};
		wgpuCommandEncoderCopyTextureToTexture(command_encoder, &src, &dst, &extent);
	}

	WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(command_encoder, NULL);
	wgpuComputePassEncoderSetPipeline(pass, variant->pipeline);
	wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
	wgpuComputePassEncoderDispatchWorkgroups(pass, (rt_output->width + 7) / 8, (rt_output->height + 7) / 8, 1);
	wgpuComputePassEncoderEnd(pass);
	wgpuComputePassEncoderRelease(pass);
	wgpuBindGroupRelease(bind_group);

	if (in_render_pass) {
		restore_render_pass();
	}
}
