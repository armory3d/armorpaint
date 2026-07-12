#import <Metal/Metal.h>
#import <MetalKit/MTKView.h>
#include <iron_gpu.h>
#include <iron_math.h>
#include <iron_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

id get_metal_layer(void);
id get_metal_device(void);
id get_metal_queue(void);

static id<MTLCommandBuffer>        command_buffer   = nil;
static id<MTLRenderCommandEncoder> command_encoder  = nil;
static id<MTLArgumentEncoder>      argument_encoder = nil;
static id<MTLBuffer>               argument_buffer  = nil;
static id<CAMetalDrawable>         drawable;
static id<MTLSamplerState>         linear_sampler;
static id<MTLSamplerState>         point_sampler;
static int                         argument_buffer_step;
static gpu_buffer_t               *current_vb;
static gpu_buffer_t               *current_ib;
static MTLViewport                 current_viewport;
static MTLScissorRect              current_scissor;
static MTLRenderPassDescriptor    *render_pass_desc;
static bool                        resized = false;
static void                       *readback_buffer;
static int                         readback_buffer_size = 0;
static bool                        linear_sampling      = true;

static MTLBlendFactor convert_blending_factor(gpu_blend_t factor) {
	switch (factor) {
	case GPU_BLEND_ONE:
		return MTLBlendFactorOne;
	case GPU_BLEND_ZERO:
		return MTLBlendFactorZero;
	case GPU_BLEND_SOURCE_ALPHA:
		return MTLBlendFactorSourceAlpha;
	case GPU_BLEND_DEST_ALPHA:
		return MTLBlendFactorDestinationAlpha;
	case GPU_BLEND_INV_SOURCE_ALPHA:
		return MTLBlendFactorOneMinusSourceAlpha;
	case GPU_BLEND_INV_DEST_ALPHA:
		return MTLBlendFactorOneMinusDestinationAlpha;
	}
}

static MTLCompareFunction convert_compare_mode(gpu_compare_mode_t compare) {
	switch (compare) {
	case GPU_COMPARE_MODE_ALWAYS:
		return MTLCompareFunctionAlways;
	case GPU_COMPARE_MODE_NEVER:
		return MTLCompareFunctionNever;
	case GPU_COMPARE_MODE_EQUAL:
		return MTLCompareFunctionEqual;
	case GPU_COMPARE_MODE_LESS:
		return MTLCompareFunctionLess;
	}
}

static MTLCullMode convert_cull_mode(gpu_cull_mode_t cull) {
	switch (cull) {
	case GPU_CULL_MODE_CLOCKWISE:
		return MTLCullModeFront;
	case GPU_CULL_MODE_COUNTER_CLOCKWISE:
		return MTLCullModeBack;
	case GPU_CULL_MODE_NONE:
		return MTLCullModeNone;
	}
}

static MTLPixelFormat convert_texture_format(gpu_texture_format_t format) {
	switch (format) {
	case GPU_TEXTURE_FORMAT_RGBA128:
		return MTLPixelFormatRGBA32Float;
	case GPU_TEXTURE_FORMAT_RGBA64:
		return MTLPixelFormatRGBA16Float;
	case GPU_TEXTURE_FORMAT_R32:
		return MTLPixelFormatR32Float;
	case GPU_TEXTURE_FORMAT_R16:
		return MTLPixelFormatR16Float;
	case GPU_TEXTURE_FORMAT_R8:
		return MTLPixelFormatR8Unorm;
	case GPU_TEXTURE_FORMAT_D32:
		return MTLPixelFormatDepth32Float;
	default:
		return MTLPixelFormatBGRA8Unorm;
	}
}

void gpu_render_target_init2(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format, int framebuffer_index) {
	target->width  = width;
	target->height = height;
	target->format = format;
	target->state  = GPU_TEXTURE_STATE_RENDER_TARGET;
	target->buffer = NULL;

	if (framebuffer_index < 0) {
		id<MTLDevice>         device     = get_metal_device();
		MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
		descriptor.textureType           = MTLTextureType2D;
		descriptor.width                 = width;
		descriptor.height                = height;
		descriptor.depth                 = 1;
		descriptor.pixelFormat           = convert_texture_format(format);
		descriptor.arrayLength           = 1;
		descriptor.mipmapLevelCount      = 1;
		descriptor.usage                 = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		descriptor.resourceOptions       = MTLResourceStorageModePrivate;
		target->impl._tex                = (__bridge_retained void *)[device newTextureWithDescriptor:descriptor];
		if (target->impl._tex == nil) {
			gpu_cleanup();
			target->impl._tex = (__bridge_retained void *)[device newTextureWithDescriptor:descriptor];
		}
	}
}

void gpu_resize_internal(int width, int height) {
	resized = true;
}

static void next_drawable() {
	CAMetalLayer *layer                       = get_metal_layer();
	drawable                                  = [layer nextDrawable];
	framebuffers[framebuffer_index].impl._tex = (__bridge void *)drawable.texture;
}

void gpu_init_internal(int depth_buffer_bits, bool vsync) {
	id<MTLDevice> device = get_metal_device();

	MTLSamplerDescriptor *linear_desc  = [MTLSamplerDescriptor new];
	linear_desc.minFilter              = MTLSamplerMinMagFilterLinear;
	linear_desc.magFilter              = MTLSamplerMinMagFilterLinear;
	linear_desc.mipFilter              = MTLSamplerMipFilterLinear;
	linear_desc.sAddressMode           = MTLSamplerAddressModeRepeat;
	linear_desc.tAddressMode           = MTLSamplerAddressModeRepeat;
	linear_desc.supportArgumentBuffers = true;
	linear_sampler                     = [device newSamplerStateWithDescriptor:linear_desc];

	MTLSamplerDescriptor *point_desc  = [MTLSamplerDescriptor new];
	point_desc.minFilter              = MTLSamplerMinMagFilterNearest;
	point_desc.magFilter              = MTLSamplerMinMagFilterNearest;
	point_desc.mipFilter              = MTLSamplerMipFilterNearest;
	point_desc.sAddressMode           = MTLSamplerAddressModeRepeat;
	point_desc.tAddressMode           = MTLSamplerAddressModeRepeat;
	point_desc.supportArgumentBuffers = true;
	point_sampler                     = [device newSamplerStateWithDescriptor:point_desc];

	MTLArgumentDescriptor *constants_desc = [MTLArgumentDescriptor argumentDescriptor];
	constants_desc.dataType               = MTLDataTypePointer;
	constants_desc.index                  = 0;

	MTLArgumentDescriptor *sampler_desc = [MTLArgumentDescriptor argumentDescriptor];
	sampler_desc.dataType               = MTLDataTypeSampler;
	sampler_desc.index                  = 1;

	MTLArgumentDescriptor *texture_desc[GPU_MAX_TEXTURES];
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		texture_desc[i]             = [MTLArgumentDescriptor argumentDescriptor];
		texture_desc[i].dataType    = MTLDataTypeTexture;
		texture_desc[i].index       = i + 2;
		texture_desc[i].textureType = MTLTextureType2D;
	}

	NSArray *arguments =
	    [NSArray arrayWithObjects:constants_desc, sampler_desc, texture_desc[0], texture_desc[1], texture_desc[2], texture_desc[3], texture_desc[4],
	                              texture_desc[5], texture_desc[6], texture_desc[7], texture_desc[8], texture_desc[9], texture_desc[10], texture_desc[11],
	                              texture_desc[12], texture_desc[13], texture_desc[14], texture_desc[15], nil];
	argument_encoder     = [device newArgumentEncoderWithArguments:arguments];
	argument_buffer_step = [argument_encoder encodedLength];
	argument_buffer      = [device newBufferWithLength:(argument_buffer_step * GPU_CONSTANT_BUFFER_MULTIPLE) options:MTLResourceStorageModeShared];

	gpu_create_framebuffers(depth_buffer_bits);
	next_drawable();
}

void gpu_begin_internal(gpu_clear_t flags, unsigned color, float depth) {
	render_pass_desc = [MTLRenderPassDescriptor renderPassDescriptor];
	for (int i = 0; i < current_render_targets_count; ++i) {
		render_pass_desc.colorAttachments[i].texture = (__bridge id<MTLTexture>)current_render_targets[i]->impl._tex;
		if (flags & GPU_CLEAR_COLOR) {
			float red, green, blue, alpha;
			iron_color_components(color, &red, &green, &blue, &alpha);
			render_pass_desc.colorAttachments[i].loadAction  = MTLLoadActionClear;
			render_pass_desc.colorAttachments[i].storeAction = MTLStoreActionStore;
			render_pass_desc.colorAttachments[i].clearColor  = MTLClearColorMake(red, green, blue, alpha);
		}
		else {
			render_pass_desc.colorAttachments[i].loadAction  = MTLLoadActionLoad;
			render_pass_desc.colorAttachments[i].storeAction = MTLStoreActionStore;
			render_pass_desc.colorAttachments[i].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
		}
	}

	if (current_depth_buffer != NULL) {
		render_pass_desc.depthAttachment.texture = (__bridge id<MTLTexture>)current_depth_buffer->impl._tex;
	}

	if (flags & GPU_CLEAR_DEPTH) {
		render_pass_desc.depthAttachment.clearDepth  = depth;
		render_pass_desc.depthAttachment.loadAction  = MTLLoadActionClear;
		render_pass_desc.depthAttachment.storeAction = MTLStoreActionStore;
	}
	else {
		render_pass_desc.depthAttachment.clearDepth  = 1;
		render_pass_desc.depthAttachment.loadAction  = MTLLoadActionLoad;
		render_pass_desc.depthAttachment.storeAction = MTLStoreActionStore;
	}

	id<MTLCommandQueue> queue = get_metal_queue();

	if (command_buffer == nil) {
		command_buffer = [queue commandBuffer];
	}

	command_encoder          = [command_buffer renderCommandEncoderWithDescriptor:render_pass_desc];
	current_viewport.originX = 0;
	current_viewport.originY = 0;
	current_viewport.width   = current_render_targets[0]->width;
	current_viewport.height  = current_render_targets[0]->height;
	current_scissor.x        = 0;
	current_scissor.y        = 0;
	current_scissor.width    = current_render_targets[0]->width;
	current_scissor.height   = current_render_targets[0]->height;
}

void gpu_end_internal() {
	[command_encoder endEncoding];
	current_render_targets_count = 0;
}

void gpu_execute_and_wait() {
	if (gpu_in_use) {
		[command_encoder endEncoding];
	}

	[command_buffer commit];
	[command_buffer waitUntilCompleted];
	id<MTLCommandQueue> queue = get_metal_queue();
	command_buffer            = [queue commandBuffer];

	if (gpu_in_use) {
		for (int i = 0; i < current_render_targets_count; ++i) {
			render_pass_desc.colorAttachments[i].loadAction = MTLLoadActionLoad;
		}
		render_pass_desc.depthAttachment.loadAction = MTLLoadActionLoad;
		command_encoder                             = [command_buffer renderCommandEncoderWithDescriptor:render_pass_desc];
		id<MTLRenderPipelineState> pipe             = (__bridge id<MTLRenderPipelineState>)current_pipeline->impl.pipeline;
		[command_encoder setRenderPipelineState:pipe];
		id<MTLDepthStencilState> depth_state = (__bridge id<MTLDepthStencilState>)current_pipeline->impl.depth;
		[command_encoder setDepthStencilState:depth_state];
		[command_encoder setFrontFacingWinding:MTLWindingClockwise];
		[command_encoder setCullMode:convert_cull_mode(current_pipeline->cull_mode)];
		id<MTLBuffer> vb = (__bridge id<MTLBuffer>)current_vb->impl.metal_buffer;
		[command_encoder setVertexBuffer:vb offset:0 atIndex:0];
		[command_encoder setViewport:current_viewport];
		[command_encoder setScissorRect:current_scissor];
	}
}

void gpu_present_internal() {
	[command_buffer presentDrawable:drawable];
	[command_buffer commit];
	[command_buffer waitUntilCompleted];

	drawable        = nil;
	command_buffer  = nil;
	command_encoder = nil;

	if (resized) {
		CAMetalLayer *layer = get_metal_layer();
		layer.drawableSize  = CGSizeMake(iron_window_width(), iron_window_height());
		for (int i = 0; i < GPU_FRAMEBUFFER_COUNT; ++i) {
			// gpu_texture_destroy_internal(&framebuffers[i]);
			gpu_render_target_init2(&framebuffers[i], iron_window_width(), iron_window_height(), GPU_TEXTURE_FORMAT_RGBA32, i);
		}
		resized = false;
	}

	next_drawable();
}

void gpu_barrier(gpu_texture_t *render_target, gpu_texture_state_t state_after) {}

void gpu_draw_internal() {
	id<MTLBuffer> index_buffer = (__bridge id<MTLBuffer>)current_ib->impl.metal_buffer;
	[command_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
	                            indexCount:current_ib->count
	                             indexType:MTLIndexTypeUInt32
	                           indexBuffer:index_buffer
	                     indexBufferOffset:0];
}

void gpu_viewport(int x, int y, int width, int height) {
	current_viewport.originX = x;
	current_viewport.originY = y;
	current_viewport.width   = width;
	current_viewport.height  = height;
	current_viewport.znear   = 0.1;
	current_viewport.zfar    = 100.0;
	[command_encoder setViewport:current_viewport];
}

void gpu_scissor(int x, int y, int width, int height) {
	if (width < 0 || height < 0) {
		return;
	}
	current_scissor.x      = x;
	current_scissor.y      = y;
	current_scissor.width  = width;
	current_scissor.height = height;
	[command_encoder setScissorRect:current_scissor];
}

void gpu_disable_scissor() {
	current_scissor.x      = 0;
	current_scissor.y      = 0;
	current_scissor.width  = current_render_targets[0]->width;
	current_scissor.height = current_render_targets[0]->height;
	[command_encoder setScissorRect:current_scissor];
}

void gpu_set_pipeline_internal(gpu_pipeline_t *pipeline) {
	id<MTLRenderPipelineState> pipe = (__bridge id<MTLRenderPipelineState>)pipeline->impl.pipeline;
	[command_encoder setRenderPipelineState:pipe];
	id<MTLDepthStencilState> depth_state = (__bridge id<MTLDepthStencilState>)pipeline->impl.depth;
	[command_encoder setDepthStencilState:depth_state];
	[command_encoder setFrontFacingWinding:MTLWindingClockwise];
	[command_encoder setCullMode:convert_cull_mode(pipeline->cull_mode)];
}

void gpu_set_vertex_buffer(gpu_buffer_t *buffer) {
	current_vb        = buffer;
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->impl.metal_buffer;
	[command_encoder setVertexBuffer:buf offset:0 atIndex:0];
}

void gpu_set_index_buffer(gpu_buffer_t *buffer) {
	current_ib = buffer;
}

void gpu_get_render_target_pixels(gpu_texture_t *render_target, uint8_t *data) {
	gpu_execute_and_wait();

	int buffer_size              = render_target->width * render_target->height * gpu_texture_format_size(render_target->format);
	int new_readback_buffer_size = buffer_size;
	if (new_readback_buffer_size < (2048 * 2048 * 4)) {
		new_readback_buffer_size = (2048 * 2048 * 4);
	}
	if (readback_buffer_size < new_readback_buffer_size) {
		readback_buffer_size = new_readback_buffer_size;
		if (readback_buffer != NULL) {
			id<MTLTexture> readback = (__bridge_transfer id<MTLTexture>)readback_buffer;
			readback                = nil;
		}
		id<MTLDevice> device = get_metal_device();
		readback_buffer      = (__bridge_retained void *)[device newBufferWithLength:new_readback_buffer_size options:MTLResourceStorageModeShared];
	}

	// Copy render target to readback buffer
	id<MTLCommandQueue>       queue           = get_metal_queue();
	id<MTLCommandBuffer>      command_buffer  = [queue commandBuffer];
	id<MTLBlitCommandEncoder> command_encoder = [command_buffer blitCommandEncoder];
	[command_encoder copyFromTexture:(__bridge id<MTLTexture>)render_target->impl._tex
	                     sourceSlice:0
	                     sourceLevel:0
	                    sourceOrigin:MTLOriginMake(0, 0, 0)
	                      sourceSize:MTLSizeMake(render_target->width, render_target->height, 1)
	                        toBuffer:(__bridge id<MTLBuffer>)readback_buffer
	               destinationOffset:0
	          destinationBytesPerRow:render_target->width * gpu_texture_format_size(render_target->format)
	        destinationBytesPerImage:0];
	[command_encoder endEncoding];
	[command_buffer commit];
	[command_buffer waitUntilCompleted];

	// Read buffer
	id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)readback_buffer;
	memcpy(data, [buffer contents], render_target->width * render_target->height * gpu_texture_format_size(render_target->format));
}

void gpu_set_constant_buffer(gpu_buffer_t *buffer, uint32_t offset, size_t size) {
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->impl.metal_buffer;
	[argument_encoder setArgumentBuffer:argument_buffer offset:argument_buffer_step * constant_buffer_index];
	[argument_encoder setBuffer:buf offset:offset atIndex:0];
	[argument_encoder setSamplerState:(linear_sampling ? linear_sampler : point_sampler) atIndex:1];
	[command_encoder setVertexBuffer:argument_buffer offset:argument_buffer_step * constant_buffer_index atIndex:1];
	[command_encoder setFragmentBuffer:argument_buffer offset:argument_buffer_step * constant_buffer_index atIndex:1];
	[command_encoder useResource:buf usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		if (current_textures[i] == NULL) {
			break;
		}
		id<MTLTexture> tex = (__bridge id<MTLTexture>)current_textures[i]->impl._tex;
		[argument_encoder setTexture:tex atIndex:i + 2];
		[command_encoder useResource:tex usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
	}
}

void gpu_set_texture(uint32_t unit, gpu_texture_t *texture) {
	current_textures[unit] = texture;
}

void gpu_use_linear_sampling(bool b) {
	linear_sampling = b;
}

void gpu_pipeline_destroy_internal(gpu_pipeline_t *pipeline) {
	id<MTLRenderPipelineState> pipe = (__bridge_transfer id<MTLRenderPipelineState>)pipeline->impl.pipeline;
	pipe                            = nil;
	pipeline->impl.pipeline         = NULL;

	id<MTLDepthStencilState> depth_state = (__bridge_transfer id<MTLDepthStencilState>)pipeline->impl.depth;
	depth_state                          = nil;
	pipeline->impl.depth                 = NULL;
}

void gpu_pipeline_compile(gpu_pipeline_t *pipeline) {
	if (pipeline->vertex_shader->impl.length == 0 || pipeline->fragment_shader->impl.length == 0) {
		// Shader compilation error
		return;
	}

	id<MTLDevice>  device  = get_metal_device();
	NSError       *error   = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:[[NSString alloc] initWithBytes:pipeline->vertex_shader->impl.source
	                                                                               length:pipeline->vertex_shader->impl.length
	                                                                             encoding:NSUTF8StringEncoding]
	                                              options:nil
	                                                error:&error];
	if (library == nil) {
		iron_error("%s", error.localizedDescription.UTF8String);
		return;
	}

	pipeline->vertex_shader->impl.mtl_function =
	    (__bridge_retained void *)[library newFunctionWithName:[NSString stringWithCString:pipeline->vertex_shader->impl.name encoding:NSUTF8StringEncoding]];
	pipeline->fragment_shader->impl.mtl_function =
	    (__bridge_retained void *)[library newFunctionWithName:[NSString stringWithCString:pipeline->fragment_shader->impl.name encoding:NSUTF8StringEncoding]];

	MTLRenderPipelineDescriptor *render_pipeline_desc = [[MTLRenderPipelineDescriptor alloc] init];
	render_pipeline_desc.vertexFunction               = (__bridge id<MTLFunction>)pipeline->vertex_shader->impl.mtl_function;
	render_pipeline_desc.fragmentFunction             = (__bridge id<MTLFunction>)pipeline->fragment_shader->impl.mtl_function;

	for (int i = 0; i < pipeline->color_attachment_count; ++i) {
		render_pipeline_desc.colorAttachments[i].pixelFormat     = convert_texture_format(pipeline->color_attachment[i]);
		render_pipeline_desc.colorAttachments[i].blendingEnabled = pipeline->blend_source != GPU_BLEND_ONE || pipeline->blend_destination != GPU_BLEND_ZERO ||
		                                                           pipeline->alpha_blend_source != GPU_BLEND_ONE ||
		                                                           pipeline->alpha_blend_destination != GPU_BLEND_ZERO;
		render_pipeline_desc.colorAttachments[i].sourceRGBBlendFactor        = convert_blending_factor(pipeline->blend_source);
		render_pipeline_desc.colorAttachments[i].destinationRGBBlendFactor   = convert_blending_factor(pipeline->blend_destination);
		render_pipeline_desc.colorAttachments[i].rgbBlendOperation           = MTLBlendOperationAdd;
		render_pipeline_desc.colorAttachments[i].sourceAlphaBlendFactor      = convert_blending_factor(pipeline->alpha_blend_source);
		render_pipeline_desc.colorAttachments[i].destinationAlphaBlendFactor = convert_blending_factor(pipeline->alpha_blend_destination);
		render_pipeline_desc.colorAttachments[i].alphaBlendOperation         = MTLBlendOperationAdd;
		render_pipeline_desc.colorAttachments[i].writeMask =
		    (pipeline->color_write_mask_red[i] ? MTLColorWriteMaskRed : 0) | (pipeline->color_write_mask_green[i] ? MTLColorWriteMaskGreen : 0) |
		    (pipeline->color_write_mask_blue[i] ? MTLColorWriteMaskBlue : 0) | (pipeline->color_write_mask_alpha[i] ? MTLColorWriteMaskAlpha : 0);
	}
	render_pipeline_desc.depthAttachmentPixelFormat = pipeline->depth_attachment_bits > 0 ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;

	float                offset            = 0;
	MTLVertexDescriptor *vertex_descriptor = [[MTLVertexDescriptor alloc] init];

	for (int i = 0; i < pipeline->input_layout->size; ++i) {
		vertex_descriptor.attributes[i].bufferIndex = 0;
		vertex_descriptor.attributes[i].offset      = offset;
		offset += gpu_vertex_data_size(pipeline->input_layout->elements[i].data);

		switch (pipeline->input_layout->elements[i].data) {
		case GPU_VERTEX_DATA_F32_1X:
			vertex_descriptor.attributes[i].format = MTLVertexFormatFloat;
			break;
		case GPU_VERTEX_DATA_F32_2X:
			vertex_descriptor.attributes[i].format = MTLVertexFormatFloat2;
			break;
		case GPU_VERTEX_DATA_F32_3X:
			vertex_descriptor.attributes[i].format = MTLVertexFormatFloat3;
			break;
		case GPU_VERTEX_DATA_F32_4X:
			vertex_descriptor.attributes[i].format = MTLVertexFormatFloat4;
			break;
		case GPU_VERTEX_DATA_I16_2X_NORM:
			vertex_descriptor.attributes[i].format = MTLVertexFormatShort2Normalized;
			break;
		case GPU_VERTEX_DATA_I16_4X_NORM:
			vertex_descriptor.attributes[i].format = MTLVertexFormatShort4Normalized;
			break;
		}
	}

	vertex_descriptor.layouts[0].stride       = offset;
	vertex_descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

	render_pipeline_desc.vertexDescriptor = vertex_descriptor;

	NSError                     *errors     = nil;
	MTLRenderPipelineReflection *reflection = nil;

	pipeline->impl.pipeline = (__bridge_retained void *)[device newRenderPipelineStateWithDescriptor:render_pipeline_desc
	                                                                                         options:MTLPipelineOptionBufferTypeInfo
	                                                                                      reflection:&reflection
	                                                                                           error:&errors];

	MTLDepthStencilDescriptor *depth_descriptor = [MTLDepthStencilDescriptor new];
	depth_descriptor.depthCompareFunction       = convert_compare_mode(pipeline->depth_mode);
	depth_descriptor.depthWriteEnabled          = pipeline->depth_write;
	pipeline->impl.depth                        = (__bridge_retained void *)[device newDepthStencilStateWithDescriptor:depth_descriptor];
}

void gpu_shader_destroy(gpu_shader_t *shader) {
	id<MTLFunction> function  = (__bridge_transfer id<MTLFunction>)shader->impl.mtl_function;
	function                  = nil;
	shader->impl.mtl_function = NULL;
}

void gpu_shader_init(gpu_shader_t *shader, const void *data, size_t length, gpu_shader_type_t type) {
	shader->impl.name[0] = 0;
	const char *source   = data;

	for (int i = 3; i < length; ++i) { // //>
		if (source[i] == '\n') {
			shader->impl.name[i - 3] = 0;
			break;
		}
		shader->impl.name[i - 3] = source[i];
	}

	shader->impl.source = data;
	shader->impl.length = length;
}

void gpu_texture_init_from_bytes(gpu_texture_t *texture, void *data, uint32_t width, uint32_t height, gpu_texture_format_t format) {
	texture->width  = width;
	texture->height = height;
	texture->format = format;
	texture->state  = GPU_TEXTURE_STATE_SHADER_RESOURCE;
	texture->buffer = NULL;

	MTLPixelFormat mtlformat = convert_texture_format(format);
	if (mtlformat == MTLPixelFormatBGRA8Unorm) {
		mtlformat = MTLPixelFormatRGBA8Unorm;
	}

	void *original_data   = data;
	int   bytes_per_row   = width * gpu_texture_format_size(format);
	int   bytes_per_image = bytes_per_row * height;

#ifdef WITH_BC7
	if (gpu_bc7_supported(width, height, format)) {
		texture->format = GPU_TEXTURE_FORMAT_RGBA32_BC7;
		mtlformat       = MTLPixelFormatBC7_RGBAUnorm;
		data            = gpu_bc7_compress(data, width, height);
		bytes_per_row   = ((width + 3) / 4) * 16; // BC7ENC_BLOCK_SIZE
		bytes_per_image = bytes_per_row * ((height + 3) / 4);
	}
#endif

	MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlformat width:width height:height mipmapped:NO];
	descriptor.textureType           = MTLTextureType2D;
	descriptor.width                 = width;
	descriptor.height                = height;
	descriptor.depth                 = 1;
	descriptor.pixelFormat           = mtlformat;
	descriptor.arrayLength           = 1;
	descriptor.mipmapLevelCount      = 1;
	descriptor.usage                 = MTLTextureUsageShaderRead; // MTLTextureUsageShaderWrite

	id<MTLDevice>  device = get_metal_device();
	id<MTLTexture> tex    = [device newTextureWithDescriptor:descriptor];
	if (tex == nil) {
		gpu_cleanup();
#ifdef WITH_BC7
		if (data != original_data) {
			free(data);
		}
#endif
		gpu_texture_init_from_bytes(texture, original_data, width, height, format);
		return;
	}
	texture->impl._tex = (__bridge_retained void *)tex;
	[tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 slice:0 withBytes:data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image];

#ifdef WITH_BC7
	if (data != original_data) {
		free(data);
	}
#endif
}

void gpu_texture_destroy_internal(gpu_texture_t *target) {
	id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)target->impl._tex;
	tex                = nil;
	target->impl._tex  = NULL;
}

void gpu_render_target_init(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format) {
	gpu_render_target_init2(target, width, height, format, -1);
}

void gpu_vertex_buffer_init(gpu_buffer_t *buffer, uint32_t count, gpu_vertex_structure_t *structure) {
	buffer->count = count;
	for (int i = 0; i < structure->size; ++i) {
		gpu_vertex_element_t element = structure->elements[i];
		buffer->stride += gpu_vertex_data_size(element.data);
	}

	id<MTLDevice>      device  = get_metal_device();
	MTLResourceOptions options = MTLResourceCPUCacheModeWriteCombined;
	options |= MTLResourceStorageModeShared;

	id<MTLBuffer> buf = [device newBufferWithLength:count * buffer->stride options:options];
	if (buf == nil) {
		gpu_cleanup();
		buf = [device newBufferWithLength:count * buffer->stride options:options];
	}
	buffer->impl.metal_buffer = (__bridge_retained void *)buf;
}

void *gpu_vertex_buffer_lock(gpu_buffer_t *buffer) {
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->impl.metal_buffer;
	return [buf contents];
}

void gpu_vertex_buffer_unlock(gpu_buffer_t *buffer) {}

void gpu_index_buffer_init(gpu_buffer_t *buffer, uint32_t count) {
	buffer->count = count;

	id<MTLDevice>      device  = get_metal_device();
	MTLResourceOptions options = MTLResourceCPUCacheModeWriteCombined;
	options |= MTLResourceStorageModeShared;

	buffer->impl.metal_buffer = (__bridge_retained void *)[device newBufferWithLength:sizeof(uint32_t) * count options:options];
	if (buffer->impl.metal_buffer == nil) {
		gpu_cleanup();
		buffer->impl.metal_buffer = (__bridge_retained void *)[device newBufferWithLength:sizeof(uint32_t) * count options:options];
	}
}

void *gpu_index_buffer_lock(gpu_buffer_t *buffer) {
	id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer->impl.metal_buffer;
	return [buf contents];
}

void gpu_index_buffer_unlock(gpu_buffer_t *buffer) {}

void gpu_constant_buffer_init(gpu_buffer_t *buffer, uint32_t size) {
	buffer->count             = size;
	buffer->data              = NULL;
	buffer->impl.metal_buffer = (__bridge_retained void *)[get_metal_device() newBufferWithLength:size options:MTLResourceOptionCPUCacheModeDefault];
}

void gpu_constant_buffer_lock(gpu_buffer_t *buffer, uint32_t start, uint32_t count) {
	id<MTLBuffer> buf  = (__bridge id<MTLBuffer>)buffer->impl.metal_buffer;
	uint8_t      *data = (uint8_t *)[buf contents];
	buffer->data       = &data[start];
}

void gpu_constant_buffer_unlock(gpu_buffer_t *buffer) {}

void gpu_buffer_destroy_internal(gpu_buffer_t *buffer) {
	id<MTLBuffer> buf         = (__bridge_transfer id<MTLBuffer>)buffer->impl.metal_buffer;
	buf                       = nil;
	buffer->impl.metal_buffer = NULL;
}

char *gpu_device_name() {
	id<MTLDevice> device = get_metal_device();
	return (char *)[device.name UTF8String];
}

bool gpu_bc7_supported(int width, int height, gpu_texture_format_t format) {
#ifdef WITH_BC7
	id<MTLDevice> device = get_metal_device();
	return [device supportsBCTextureCompression] && format == GPU_TEXTURE_FORMAT_RGBA32 && width >= 2048 && height >= 2048 && (width & (width - 1)) == 0 &&
	       (height & (height - 1)) == 0;
#else
	return false;
#endif
}

typedef struct inst {
	mat4_t m;
	int    i;
} inst_t;

static gpu_acceleration_structure_t *accel;
static gpu_raytrace_pipeline_t      *pipeline;
static gpu_texture_t                *output = NULL;
static gpu_buffer_t                 *constant_buf;
static id<MTLComputePipelineState>   _raytracing_pipeline;
static NSMutableArray               *_primitive_accels;
static id<MTLAccelerationStructure>  _instance_accel;
static dispatch_semaphore_t          _semaphore;
static gpu_texture_t                *_texpaint0;
static gpu_texture_t                *_texpaint1;
static gpu_texture_t                *_texpaint2;
static gpu_texture_t                *_texenv;
static gpu_texture_t                *_texsobol;
static gpu_texture_t                *_texscramble;
static gpu_texture_t                *_texrank;
static gpu_buffer_t                 *vb[16];
static gpu_buffer_t                 *vb_last[16];
static gpu_buffer_t                 *ib[16];
static int                           vb_count      = 0;
static int                           vb_count_last = 0;
static inst_t                        instances[1024];
static int                           instances_count = 0;

void gpu_raytrace_pipeline_init(gpu_raytrace_pipeline_t *pipeline, void *shader, int ray_shader_size, gpu_buffer_t *constant_buffer) {
	id<MTLDevice> device = get_metal_device();
	if (!device.supportsRaytracing)
		return;
	constant_buf = constant_buffer;

	NSError       *error   = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:[[NSString alloc] initWithBytes:shader length:ray_shader_size encoding:NSUTF8StringEncoding]
	                                              options:nil
	                                                error:&error];
	if (library == nil) {
		iron_error("%s", error.localizedDescription.UTF8String);
	}

	MTLComputePipelineDescriptor *descriptor                   = [[MTLComputePipelineDescriptor alloc] init];
	descriptor.computeFunction                                 = [library newFunctionWithName:@"raytracingKernel"];
	descriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth = YES;
	_raytracing_pipeline = [device newComputePipelineStateWithDescriptor:descriptor options:0 reflection:nil error:&error];
	_semaphore           = dispatch_semaphore_create(2);
}

void gpu_raytrace_pipeline_destroy(gpu_raytrace_pipeline_t *pipeline) {}

bool gpu_raytrace_supported() {
	id<MTLDevice> device = get_metal_device();
	return device.supportsRaytracing;
}

id<MTLAccelerationStructure> create_acceleration_sctructure(MTLAccelerationStructureDescriptor *descriptor) {
	id<MTLDevice>       device = get_metal_device();
	id<MTLCommandQueue> queue  = get_metal_queue();

	MTLAccelerationStructureSizes accel_sizes            = [device accelerationStructureSizesWithDescriptor:descriptor];
	id<MTLAccelerationStructure>  acceleration_structure = [device newAccelerationStructureWithSize:accel_sizes.accelerationStructureSize];

	id<MTLBuffer>        scratch_buffer = [device newBufferWithLength:accel_sizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];
	id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
	id<MTLAccelerationStructureCommandEncoder> command_encoder        = [command_buffer accelerationStructureCommandEncoder];
	id<MTLBuffer>                              compacteds_size_buffer = [device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];

	[command_encoder buildAccelerationStructure:acceleration_structure descriptor:descriptor scratchBuffer:scratch_buffer scratchBufferOffset:0];

	[command_encoder writeCompactedAccelerationStructureSize:acceleration_structure toBuffer:compacteds_size_buffer offset:0];

	[command_encoder endEncoding];
	[command_buffer commit];
	[command_buffer waitUntilCompleted];

	uint32_t                     compacted_size                   = *(uint32_t *)compacteds_size_buffer.contents;
	id<MTLAccelerationStructure> compacted_acceleration_structure = [device newAccelerationStructureWithSize:compacted_size];
	command_buffer                                                = [queue commandBuffer];
	command_encoder                                               = [command_buffer accelerationStructureCommandEncoder];
	[command_encoder copyAndCompactAccelerationStructure:acceleration_structure toAccelerationStructure:compacted_acceleration_structure];
	[command_encoder endEncoding];
	[command_buffer commit];

	return compacted_acceleration_structure;
}

void gpu_raytrace_acceleration_structure_init(gpu_acceleration_structure_t *accel) {
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
	//	for (int i = 0; i < vb_count_last; ++i) {
	//	}
	_primitive_accels = nil;
}

void _gpu_raytrace_acceleration_structure_destroy_top(gpu_acceleration_structure_t *accel) {
	_instance_accel = nil;
}

void gpu_raytrace_acceleration_structure_build(gpu_acceleration_structure_t *accel, gpu_buffer_t *_vb_full, gpu_buffer_t *_ib_full) {

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

	id<MTLDevice> device = get_metal_device();
	if (!device.supportsRaytracing) {
		return;
	}

	MTLResourceOptions options = MTLResourceStorageModeShared;

	MTLAccelerationStructureTriangleGeometryDescriptor *descriptor = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
	descriptor.indexType                                           = MTLIndexTypeUInt32;
	descriptor.indexBuffer                                         = (__bridge id<MTLBuffer>)ib[0]->impl.metal_buffer;
	descriptor.vertexBuffer                                        = (__bridge id<MTLBuffer>)vb[0]->impl.metal_buffer;
	descriptor.vertexStride                                        = vb[0]->stride;
	descriptor.triangleCount                                       = ib[0]->count / 3;
	descriptor.vertexFormat                                        = MTLAttributeFormatShort4Normalized;

	MTLPrimitiveAccelerationStructureDescriptor *accel_descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
	accel_descriptor.geometryDescriptors                          = @[ descriptor ];
	id<MTLAccelerationStructure> acceleration_structure           = create_acceleration_sctructure(accel_descriptor);
	_primitive_accels                                             = [[NSMutableArray alloc] init];
	[_primitive_accels addObject:acceleration_structure];

	id<MTLBuffer> instance_buffer = [device newBufferWithLength:sizeof(MTLAccelerationStructureInstanceDescriptor) * 1 options:options];

	MTLAccelerationStructureInstanceDescriptor *instance_descriptors = (MTLAccelerationStructureInstanceDescriptor *)instance_buffer.contents;
	instance_descriptors[0].accelerationStructureIndex               = 0;
	instance_descriptors[0].options                                  = MTLAccelerationStructureInstanceOptionOpaque;
	instance_descriptors[0].mask                                     = 1;
	instance_descriptors[0].transformationMatrix.columns[0]          = MTLPackedFloat3Make(instances[0].m.m[0], instances[0].m.m[1], instances[0].m.m[2]);
	instance_descriptors[0].transformationMatrix.columns[1]          = MTLPackedFloat3Make(instances[0].m.m[4], instances[0].m.m[5], instances[0].m.m[6]);
	instance_descriptors[0].transformationMatrix.columns[2]          = MTLPackedFloat3Make(instances[0].m.m[8], instances[0].m.m[9], instances[0].m.m[10]);
	instance_descriptors[0].transformationMatrix.columns[3]          = MTLPackedFloat3Make(instances[0].m.m[12], instances[0].m.m[13], instances[0].m.m[14]);

	MTLInstanceAccelerationStructureDescriptor *inst_accel_descriptor = [MTLInstanceAccelerationStructureDescriptor descriptor];
	inst_accel_descriptor.instancedAccelerationStructures             = _primitive_accels;
	inst_accel_descriptor.instanceCount                               = 1;
	inst_accel_descriptor.instanceDescriptorBuffer                    = instance_buffer;
	_instance_accel                                                   = create_acceleration_sctructure(inst_accel_descriptor);
}

void gpu_raytrace_acceleration_structure_destroy(gpu_acceleration_structure_t *accel) {}

void gpu_raytrace_set_textures(gpu_texture_t *texpaint0, gpu_texture_t *texpaint1, gpu_texture_t *texpaint2, gpu_texture_t *texenv, gpu_texture_t *texsobol,
                               gpu_texture_t *texscramble, gpu_texture_t *texrank) {
	_texpaint0   = texpaint0;
	_texpaint1   = texpaint1;
	_texpaint2   = texpaint2;
	_texenv      = texenv;
	_texsobol    = texsobol;
	_texscramble = texscramble;
	_texrank     = texrank;
}

void gpu_raytrace_set_acceleration_structure(gpu_acceleration_structure_t *_accel) {
	accel = _accel;
}

void gpu_raytrace_set_pipeline(gpu_raytrace_pipeline_t *_rt_pipeline) {
	pipeline = _rt_pipeline;
}

void gpu_raytrace_set_target(gpu_texture_t *_output) {
	output = _output;
}

void gpu_raytrace_dispatch_rays() {
	id<MTLDevice> device = get_metal_device();
	if (!device.supportsRaytracing)
		return;
	dispatch_semaphore_wait(_semaphore, DISPATCH_TIME_FOREVER);

	id<MTLCommandQueue>          queue          = get_metal_queue();
	id<MTLCommandBuffer>         command_buffer = [queue commandBuffer];
	__block dispatch_semaphore_t sem            = _semaphore;
	[command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
		dispatch_semaphore_signal(sem);
	}];

	NSUInteger width                   = output->width;
	NSUInteger height                  = output->height;
	MTLSize    threads_per_threadgroup = MTLSizeMake(8, 8, 1);
	MTLSize    threadgroups            = MTLSizeMake((width + threads_per_threadgroup.width - 1) / threads_per_threadgroup.width,
	                                                 (height + threads_per_threadgroup.height - 1) / threads_per_threadgroup.height, 1);

	id<MTLComputeCommandEncoder> compute_encoder = [command_buffer computeCommandEncoder];
	[compute_encoder setBuffer:(__bridge id<MTLBuffer>)constant_buf->impl.metal_buffer offset:0 atIndex:0];
	[compute_encoder setAccelerationStructure:_instance_accel atBufferIndex:1];
	[compute_encoder setBuffer:(__bridge id<MTLBuffer>)ib[0]->impl.metal_buffer offset:0 atIndex:2];
	[compute_encoder setBuffer:(__bridge id<MTLBuffer>)vb[0]->impl.metal_buffer offset:0 atIndex:3];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)output->impl._tex atIndex:0];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texpaint0->impl._tex atIndex:1];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texpaint1->impl._tex atIndex:2];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texpaint2->impl._tex atIndex:3];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texenv->impl._tex atIndex:4];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texsobol->impl._tex atIndex:5];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texscramble->impl._tex atIndex:6];
	[compute_encoder setTexture:(__bridge id<MTLTexture>)_texrank->impl._tex atIndex:7];
	[compute_encoder setSamplerState:linear_sampler atIndex:0];

	for (id<MTLAccelerationStructure> primitive_accel in _primitive_accels) {
		[compute_encoder useResource:primitive_accel usage:MTLResourceUsageRead];
	}

	[compute_encoder setComputePipelineState:_raytracing_pipeline];
	[compute_encoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threads_per_threadgroup];
	[compute_encoder endEncoding];
	[command_buffer commit];
}
