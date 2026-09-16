#include "iron_gpu.h"
#include "iron_math.h"
#include "iron_system.h"
#include <string.h>

static gpu_buffer_t   constant_buffer;
static bool           gpu_thrown = false;
static gpu_texture_t  textures_to_destroy[128];
static gpu_buffer_t   buffers_to_destroy[128];
static gpu_pipeline_t pipelines_to_destroy[128];
static int            textures_to_destroy_count  = 0;
static int            buffers_to_destroy_count   = 0;
static int            pipelines_to_destroy_count = 0;

extern int      draw_vb_index;
uint32_t        constant_buffer_index              = 0;
uint32_t        draw_calls                         = 0;
uint32_t        draw_calls_last                    = 0;
bool            gpu_in_use                         = false;
bool            gpu_raytrace_multi                 = false;
gpu_texture_t  *current_textures[GPU_MAX_TEXTURES] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
gpu_texture_t  *current_render_targets[8]          = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
uint32_t        current_render_targets_count       = 0;
gpu_texture_t  *current_depth_buffer               = NULL;
gpu_pipeline_t *current_pipeline                   = NULL;
gpu_texture_t   framebuffers[GPU_FRAMEBUFFER_COUNT];
gpu_texture_t   framebuffer_depth;
uint32_t        framebuffer_index = 0;

void gpu_init(int depth_buffer_bits, bool vsync) {
	gpu_init_internal(depth_buffer_bits, vsync);
	gpu_constant_buffer_init(&constant_buffer, GPU_CONSTANT_BUFFER_SIZE * GPU_CONSTANT_BUFFER_MULTIPLE);
	gpu_constant_buffer_lock(&constant_buffer, 0, GPU_CONSTANT_BUFFER_SIZE);
}

void gpu_begin(gpu_texture_t **targets, int count, gpu_texture_t *depth_buffer, gpu_clear_t flags, unsigned color, float depth) {
	if (gpu_in_use && !gpu_thrown) {
		gpu_thrown = true;
		iron_log("End before you begin");
	}
	gpu_in_use = true;

	if (current_render_targets_count > 0 && current_render_targets[0] != &framebuffers[framebuffer_index]) {
		for (int i = 0; i < current_render_targets_count; ++i) {
			gpu_barrier(current_render_targets[i], GPU_TEXTURE_STATE_SHADER_RESOURCE);
		}
	}
	if (current_depth_buffer != NULL) {
		gpu_barrier(current_depth_buffer, GPU_TEXTURE_STATE_SHADER_RESOURCE);
	}

	if (targets == NULL) {
		current_render_targets[0]    = &framebuffers[framebuffer_index];
		current_render_targets_count = 1;
		current_depth_buffer         = framebuffer_depth.width > 0 ? &framebuffer_depth : NULL;
	}
	else {
		for (int i = 0; i < count; ++i) {
			current_render_targets[i] = targets[i];
		}
		current_render_targets_count = count;
		current_depth_buffer         = depth_buffer;
	}

	for (int i = 0; i < current_render_targets_count; ++i) {
		gpu_barrier(current_render_targets[i], GPU_TEXTURE_STATE_RENDER_TARGET);
	}
	if (current_depth_buffer != NULL) {
		gpu_barrier(current_depth_buffer, GPU_TEXTURE_STATE_RENDER_TARGET_DEPTH);
	}

	gpu_begin_internal(flags, color, depth);
}

void gpu_draw() {
	if (current_pipeline == NULL || current_pipeline->impl.pipeline == NULL) {
		return;
	}
	gpu_constant_buffer_unlock(&constant_buffer);
	gpu_set_constant_buffer(&constant_buffer, constant_buffer_index * GPU_CONSTANT_BUFFER_SIZE, GPU_CONSTANT_BUFFER_SIZE);
	gpu_draw_internal();

	constant_buffer_index++;
	if (constant_buffer_index >= GPU_CONSTANT_BUFFER_MULTIPLE) {
		constant_buffer_index = 0;
	}

	draw_calls++;
	if (draw_calls + draw_calls_last >= GPU_CONSTANT_BUFFER_MULTIPLE) {
		draw_calls = draw_calls_last = constant_buffer_index = 0;
		gpu_execute_and_wait();
	}

	gpu_constant_buffer_lock(&constant_buffer, constant_buffer_index * GPU_CONSTANT_BUFFER_SIZE, GPU_CONSTANT_BUFFER_SIZE);
}

void gpu_end() {
	if (!gpu_in_use && !gpu_thrown) {
		gpu_thrown = true;
		iron_log("Begin before you end");
	}
	gpu_in_use = false;
	gpu_end_internal();
}

void gpu_cleanup() {
	while (textures_to_destroy_count > 0) {
		textures_to_destroy_count--;
		gpu_texture_destroy_internal(&textures_to_destroy[textures_to_destroy_count]);
	}
	while (buffers_to_destroy_count > 0) {
		buffers_to_destroy_count--;
		gpu_buffer_destroy_internal(&buffers_to_destroy[buffers_to_destroy_count]);
	}
	while (pipelines_to_destroy_count > 0) {
		pipelines_to_destroy_count--;
		gpu_pipeline_destroy_internal(&pipelines_to_destroy[pipelines_to_destroy_count]);
	}
}

bool gpu_cleanup_pending() {
	return textures_to_destroy_count > 0 || buffers_to_destroy_count > 0 || pipelines_to_destroy_count > 0;
}

void gpu_present() {
	gpu_present_internal();
	draw_calls_last = draw_calls;
	draw_calls      = 0;
	draw_vb_index   = 0;
	gpu_cleanup();
}

void gpu_resize(int width, int height) {
	if (width == 0 || height == 0) {
		return;
	}
	if (width == framebuffers[0].width && height == framebuffers[0].height) {
		return;
	}
	gpu_resize_internal(width, height);
}

void gpu_set_int(int location, int value) {
	int *ints = (int *)(&constant_buffer.data[location]);
	ints[0]   = value;
}

void gpu_set_int2(int location, int value1, int value2) {
	int *ints = (int *)(&constant_buffer.data[location]);
	ints[0]   = value1;
	ints[1]   = value2;
}

void gpu_set_int3(int location, int value1, int value2, int value3) {
	int *ints = (int *)(&constant_buffer.data[location]);
	ints[0]   = value1;
	ints[1]   = value2;
	ints[2]   = value3;
}

void gpu_set_int4(int location, int value1, int value2, int value3, int value4) {
	int *ints = (int *)(&constant_buffer.data[location]);
	ints[0]   = value1;
	ints[1]   = value2;
	ints[2]   = value3;
	ints[3]   = value4;
}

void gpu_set_ints(int location, int *values, int count) {
	int *ints = (int *)(&constant_buffer.data[location]);
	for (int i = 0; i < count; ++i) {
		ints[i] = values[i];
	}
}

void gpu_set_float(int location, float value) {
	float *floats = (float *)(&constant_buffer.data[location]);
	floats[0]     = value;
}

void gpu_set_float2(int location, float value1, float value2) {
	float *floats = (float *)(&constant_buffer.data[location]);
	floats[0]     = value1;
	floats[1]     = value2;
}

void gpu_set_float3(int location, float value1, float value2, float value3) {
	float *floats = (float *)(&constant_buffer.data[location]);
	floats[0]     = value1;
	floats[1]     = value2;
	floats[2]     = value3;
}

void gpu_set_float4(int location, float value1, float value2, float value3, float value4) {
	float *floats = (float *)(&constant_buffer.data[location]);
	floats[0]     = value1;
	floats[1]     = value2;
	floats[2]     = value3;
	floats[3]     = value4;
}

void gpu_set_floats(int location, f32_array_t *values) {
	float *floats = (float *)(&constant_buffer.data[location]);
	for (int i = 0; i < values->length; ++i) {
		floats[i] = values->buffer[i];
	}
}

void gpu_set_bool(int location, bool value) {
	int *ints = (int *)(&constant_buffer.data[location]);
	ints[0]   = value ? 1 : 0;
}

void gpu_set_mat3(int location, mat3_t value) {
	float *floats = (float *)(&constant_buffer.data[location]);
	for (int y = 0; y < 3; ++y) {
		memcpy(&floats[y * 4], &value.m[y * 3], 3 * sizeof(float));
	}
}

void gpu_set_mat4(int location, mat4_t value) {
	memcpy(&constant_buffer.data[location], value.m, 16 * sizeof(float));
}

void gpu_vertex_structure_add(gpu_vertex_structure_t *structure, const char *name, gpu_vertex_data_t data) {
	structure->elements[structure->size].name = name;
	structure->elements[structure->size].data = data;
	structure->size++;
}

void gpu_pipeline_init(gpu_pipeline_t *pipe) {
	pipe->input_layout            = NULL;
	pipe->vertex_shader           = NULL;
	pipe->fragment_shader         = NULL;
	pipe->cull_mode               = GPU_CULL_MODE_NONE;
	pipe->depth_write             = false;
	pipe->depth_mode              = GPU_COMPARE_MODE_ALWAYS;
	pipe->blend_source            = GPU_BLEND_ONE;
	pipe->blend_destination       = GPU_BLEND_ZERO;
	pipe->alpha_blend_source      = GPU_BLEND_ONE;
	pipe->alpha_blend_destination = GPU_BLEND_ZERO;
	for (int i = 0; i < 8; ++i) {
		pipe->color_write_mask_red[i]   = true;
		pipe->color_write_mask_green[i] = true;
		pipe->color_write_mask_blue[i]  = true;
		pipe->color_write_mask_alpha[i] = true;
		pipe->color_attachment[i]       = GPU_TEXTURE_FORMAT_RGBA32;
	}
	pipe->color_attachment_count = 1;
	pipe->depth_attachment_bits  = 0;
	memset(&pipe->impl, 0, sizeof(gpu_pipeline_impl_t));
}

void gpu_create_framebuffers(int depth_buffer_bits) {
	for (int i = 0; i < GPU_FRAMEBUFFER_COUNT; ++i) {
		gpu_render_target_init2(&framebuffers[i], iron_window_width(), iron_window_height(), GPU_TEXTURE_FORMAT_RGBA32, i);
	}
	if (depth_buffer_bits > 0) {
		gpu_render_target_init(&framebuffer_depth, iron_window_width(), iron_window_height(), GPU_TEXTURE_FORMAT_D32);
	}
	else {
		framebuffer_depth.width = framebuffer_depth.height = 0;
	}
}

void gpu_texture_destroy(gpu_texture_t *texture) {
	textures_to_destroy[textures_to_destroy_count] = *texture;
	textures_to_destroy_count++;
	if (textures_to_destroy_count >= 128) {
		gpu_execute_and_wait();
		gpu_cleanup();
	}
}

void gpu_set_pipeline(gpu_pipeline_t *pipeline) {
	current_pipeline = pipeline;
	for (int i = 0; i < GPU_MAX_TEXTURES; ++i) {
		current_textures[i] = NULL;
	}
	if (pipeline->impl.pipeline == NULL) {
		return;
	}
	gpu_set_pipeline_internal(pipeline);
}

void gpu_pipeline_destroy(gpu_pipeline_t *pipeline) {
	pipelines_to_destroy[pipelines_to_destroy_count] = *pipeline;
	pipelines_to_destroy_count++;
	if (pipelines_to_destroy_count >= 128) {
		gpu_execute_and_wait();
		gpu_cleanup();
	}
}

void gpu_buffer_destroy(gpu_buffer_t *buffer) {
	buffers_to_destroy[buffers_to_destroy_count] = *buffer;
	buffers_to_destroy_count++;
	if (buffers_to_destroy_count >= 128) {
		gpu_execute_and_wait();
		gpu_cleanup();
	}
}

uint32_t gpu_vertex_data_size(gpu_vertex_data_t data) {
	switch (data) {
	case GPU_VERTEX_DATA_F32_1X:
		return 1 * 4;
	case GPU_VERTEX_DATA_F32_2X:
		return 2 * 4;
	case GPU_VERTEX_DATA_F32_3X:
		return 3 * 4;
	case GPU_VERTEX_DATA_F32_4X:
		return 4 * 4;
	case GPU_VERTEX_DATA_I16_2X_NORM:
		return 2 * 2;
	case GPU_VERTEX_DATA_I16_4X_NORM:
		return 4 * 2;
	}
}

uint32_t gpu_vertex_struct_size(gpu_vertex_structure_t *s) {
	uint32_t size = 0;
	for (int i = 0; i < s->size; ++i) {
		size += gpu_vertex_data_size(s->elements[i].data);
	}
	return size;
}

uint32_t gpu_texture_format_size(gpu_texture_format_t format) {
	switch (format) {
	case GPU_TEXTURE_FORMAT_RGBA128:
		return 16;
	case GPU_TEXTURE_FORMAT_RGBA64:
		return 8;
	case GPU_TEXTURE_FORMAT_R16:
		return 2;
	case GPU_TEXTURE_FORMAT_R8:
		return 1;
	default:
		return 4;
	}
}

static gpu_buffer_t                 rt_constant_buffer;
static gpu_raytrace_pipeline_t      rt_pipeline;
static gpu_acceleration_structure_t rt_accel;
static bool                         rt_created              = false;
static bool                         rt_accel_created        = false;
static const int                    rt_constant_buffer_size = 24;

void _gpu_raytrace_init(buffer_t *shader) {
	if (rt_created) {
		gpu_buffer_destroy(&rt_constant_buffer);
		gpu_raytrace_pipeline_destroy(&rt_pipeline);
	}
	rt_created = true;
	gpu_constant_buffer_init(&rt_constant_buffer, rt_constant_buffer_size * 4);
	gpu_raytrace_pipeline_init(&rt_pipeline, shader->buffer, (int)shader->length, &rt_constant_buffer);
}

void _gpu_raytrace_as_init() {
	if (rt_accel_created) {
		gpu_raytrace_acceleration_structure_destroy(&rt_accel);
	}
	rt_accel_created = true;
	gpu_raytrace_acceleration_structure_init(&rt_accel);
}

void _gpu_raytrace_as_add(struct gpu_buffer *vb, gpu_buffer_t *ib, mat4_t transform) {
	gpu_raytrace_acceleration_structure_add(&rt_accel, vb, ib, transform);
}

void _gpu_raytrace_as_build(struct gpu_buffer *vb_full, gpu_buffer_t *ib_full) {
	gpu_raytrace_acceleration_structure_build(&rt_accel, vb_full, ib_full);
}

void _gpu_raytrace_dispatch_rays(gpu_texture_t *render_target, buffer_t *buffer) {
	float *cb = (float *)buffer->buffer;
	gpu_constant_buffer_lock(&rt_constant_buffer, 0, rt_constant_buffer.count);
	for (int i = 0; i < rt_constant_buffer_size; ++i) {
		float *floats = (float *)(&rt_constant_buffer.data[i * 4]);
		floats[0]     = cb[i];
	}
	gpu_constant_buffer_unlock(&rt_constant_buffer);

	gpu_raytrace_set_acceleration_structure(&rt_accel);
	gpu_raytrace_set_pipeline(&rt_pipeline);
	gpu_raytrace_set_target(render_target);
	gpu_raytrace_dispatch_rays();
}

#ifdef WITH_BC7

#include <iron_thread.h>
#include <libs/bc7enc.h>
#define BC7_THREAD_COUNT 16

typedef struct {
	uint8_t                     *src;
	uint8_t                     *dst;
	int                          width;
	int                          height;
	int                          blocks_x;
	int                          total_blocks;
	volatile int32_t            *next_block;
	bc7enc_compress_block_params params;
} bc7_thread_params_t;

static void bc7_thread_func(void *arg) {
	bc7_thread_params_t *p = (bc7_thread_params_t *)arg;
	for (;;) {
		int bi = iron_atomic_increment(p->next_block);
		if (bi >= p->total_blocks)
			break;
		int     bx = bi % p->blocks_x;
		int     by = bi / p->blocks_x;
		uint8_t block[64];
		for (int py = 0; py < 4; py++) {
			for (int px = 0; px < 4; px++) {
				int sx             = bx * 4 + px < p->width ? bx * 4 + px : p->width - 1;
				int sy             = by * 4 + py < p->height ? by * 4 + py : p->height - 1;
				int src_idx        = (sy * p->width + sx) * 4;
				int dst_idx        = (py * 4 + px) * 4;
				block[dst_idx + 0] = p->src[src_idx + 0];
				block[dst_idx + 1] = p->src[src_idx + 1];
				block[dst_idx + 2] = p->src[src_idx + 2];
				block[dst_idx + 3] = p->src[src_idx + 3];
			}
		}
		bc7enc_compress_block(p->dst + (size_t)bi * BC7ENC_BLOCK_SIZE, block, &p->params);
	}
}

void *gpu_bc7_compress(void *data, int width, int height) {
	int                blocks_x     = (width + 3) / 4;
	int                blocks_y     = (height + 3) / 4;
	void              *bc7_data     = malloc(blocks_x * blocks_y * BC7ENC_BLOCK_SIZE);
	static bc7enc_bool bc7enc_ready = BC7ENC_FALSE;
	if (!bc7enc_ready) {
		bc7enc_compress_block_init();
		bc7enc_ready = BC7ENC_TRUE;
	}
	volatile int32_t    next_block = 0;
	bc7_thread_params_t tp         = {
	            .src          = (uint8_t *)data,
	            .dst          = (uint8_t *)bc7_data,
	            .width        = width,
	            .height       = height,
	            .blocks_x     = blocks_x,
	            .total_blocks = blocks_x * blocks_y,
	            .next_block   = &next_block,
    };
	bc7enc_compress_block_params_init(&tp.params);
	tp.params.m_max_partitions_mode = 0;
	tp.params.m_try_least_squares   = false;

	iron_thread_t threads[BC7_THREAD_COUNT];
	for (int i = 0; i < BC7_THREAD_COUNT; i++) {
		iron_thread_init(&threads[i], bc7_thread_func, &tp);
	}
	for (int i = 0; i < BC7_THREAD_COUNT; i++) {
		iron_thread_wait_and_destroy(&threads[i]);
	}
	return bc7_data;
}

#endif
