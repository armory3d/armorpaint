#pragma once

#include "iron_array.h"
#include "iron_global.h"
#include "iron_gpu.h"
#include "iron_math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(IRON_WINDOWS)
#include "backends/direct3d12_gpu.h"
#elif defined(IRON_MACOS) || defined(IRON_IOS)
#include "backends/metal_gpu.h"
#elif defined(IRON_WASM)
#include "backends/webgpu_gpu.h"
#else
#include "backends/vulkan_gpu.h"
#endif

#define GPU_MAX_VERTEX_ELEMENTS      16
#define GPU_MAX_TEXTURES             16
#define GPU_CONSTANT_BUFFER_SIZE     512
#define GPU_CONSTANT_BUFFER_MULTIPLE 2048
#ifdef IRON_ANDROID
#define GPU_FRAMEBUFFER_COUNT 7
#elif defined(IRON_WASM)
#define GPU_FRAMEBUFFER_COUNT 1
#else
#define GPU_FRAMEBUFFER_COUNT 3
#endif

typedef enum {
	GPU_CLEAR_NONE  = 0,
	GPU_CLEAR_COLOR = 1,
	GPU_CLEAR_DEPTH = 2,
} gpu_clear_t;

typedef enum {
	GPU_TEXTURE_STATE_SHADER_RESOURCE,
	GPU_TEXTURE_STATE_RENDER_TARGET,
	GPU_TEXTURE_STATE_RENDER_TARGET_DEPTH,
	GPU_TEXTURE_STATE_PRESENT
} gpu_texture_state_t;

typedef enum {
	GPU_TEXTURE_FORMAT_RGBA32,
	GPU_TEXTURE_FORMAT_RGBA64,
	GPU_TEXTURE_FORMAT_RGBA128,
	GPU_TEXTURE_FORMAT_R8,
	GPU_TEXTURE_FORMAT_R16,
	GPU_TEXTURE_FORMAT_R32,
	GPU_TEXTURE_FORMAT_D32,
	GPU_TEXTURE_FORMAT_RGBA32_BC7
} gpu_texture_format_t;

typedef enum {
	GPU_VERTEX_DATA_F32_1X,
	GPU_VERTEX_DATA_F32_2X,
	GPU_VERTEX_DATA_F32_3X,
	GPU_VERTEX_DATA_F32_4X,
	GPU_VERTEX_DATA_I16_2X_NORM,
	GPU_VERTEX_DATA_I16_4X_NORM
} gpu_vertex_data_t;

typedef enum {
	GPU_SHADER_TYPE_VERTEX,
	GPU_SHADER_TYPE_FRAGMENT,
	GPU_SHADER_TYPE_COUNT
} gpu_shader_type_t;

typedef enum {
	GPU_BLEND_ONE,
	GPU_BLEND_ZERO,
	GPU_BLEND_SOURCE_ALPHA,
	GPU_BLEND_DEST_ALPHA,
	GPU_BLEND_INV_SOURCE_ALPHA,
	GPU_BLEND_INV_DEST_ALPHA
} gpu_blend_t;

typedef enum {
	GPU_CULL_MODE_CLOCKWISE,
	GPU_CULL_MODE_COUNTER_CLOCKWISE,
	GPU_CULL_MODE_NONE
} gpu_cull_mode_t;

typedef enum {
	GPU_COMPARE_MODE_ALWAYS,
	GPU_COMPARE_MODE_NEVER,
	GPU_COMPARE_MODE_LESS,
	GPU_COMPARE_MODE_EQUAL
} gpu_compare_mode_t;

typedef struct gpu_texture {
	uint32_t             width;
	uint32_t             height;
	gpu_texture_format_t format;
	gpu_texture_state_t  state;
	buffer_t            *buffer;
	bool                 gpu_write;
	gpu_texture_impl_t   impl;
} gpu_texture_t;

typedef struct gpu_buffer {
	uint32_t          count;
	uint32_t          stride;
	uint8_t          *data; // constant buffer data
	bool              cpu_write;
	gpu_buffer_impl_t impl;
} gpu_buffer_t;

typedef struct gpu_vertex_element {
	const char       *name;
	gpu_vertex_data_t data;
} gpu_vertex_element_t;

typedef struct gpu_vertex_structure {
	gpu_vertex_element_t elements[GPU_MAX_VERTEX_ELEMENTS];
	uint32_t             size;
} gpu_vertex_structure_t;

typedef struct gpu_shader {
	gpu_shader_impl_t impl;
} gpu_shader_t;

typedef struct gpu_pipeline {
	gpu_vertex_structure_t *input_layout;
	gpu_shader_t           *vertex_shader;
	gpu_shader_t           *fragment_shader;
	gpu_cull_mode_t         cull_mode;
	bool                    depth_write;
	gpu_compare_mode_t      depth_mode;
	gpu_blend_t             blend_source;
	gpu_blend_t             blend_destination;
	gpu_blend_t             alpha_blend_source;
	gpu_blend_t             alpha_blend_destination;
	bool                    color_write_mask_red[8];
	bool                    color_write_mask_green[8];
	bool                    color_write_mask_blue[8];
	bool                    color_write_mask_alpha[8];
	gpu_texture_format_t    color_attachment[8];
	uint32_t                color_attachment_count;
	uint32_t                depth_attachment_bits;
	gpu_pipeline_impl_t     impl;
} gpu_pipeline_t;

typedef struct gpu_raytrace_pipeline {
	gpu_buffer_t       *constant_buffer;
	gpu_pipeline_impl_t impl;
} gpu_raytrace_pipeline_t;

typedef struct gpu_acceleration_structure {
	gpu_acceleration_structure_impl_t impl;
} gpu_acceleration_structure_t;

int  gpu_max_bound_textures(void);
void gpu_begin(gpu_texture_t **targets, int count, gpu_texture_t *depth_buffer, gpu_clear_t flags, unsigned color, float depth);
void gpu_begin_internal(gpu_clear_t flags, unsigned color, float depth);
void gpu_end(void);
void gpu_end_internal(void);
void gpu_execute_and_wait(void);
void gpu_cleanup(void);
bool gpu_cleanup_pending(void);
void gpu_present(void);
void gpu_present_internal(void);
void gpu_barrier(gpu_texture_t *render_target, gpu_texture_state_t state_after);
void gpu_create_framebuffers(int depth_buffer_bits);
void gpu_init(int depth_buffer_bits, bool vsync);
void gpu_init_internal(int depth_buffer_bits, bool vsync);
void gpu_draw(void);
void gpu_resize(int width, int height);
void gpu_resize_internal(int width, int height);
void gpu_set_int(int location, int value);
void gpu_set_int2(int location, int value1, int value2);
void gpu_set_int3(int location, int value1, int value2, int value3);
void gpu_set_int4(int location, int value1, int value2, int value3, int value4);
void gpu_set_ints(int location, int *values, int count);
void gpu_set_float(int location, float value);
void gpu_set_float2(int location, float value1, float value2);
void gpu_set_float3(int location, float value1, float value2, float value3);
void gpu_set_float4(int location, float value1, float value2, float value3, float value4);
void gpu_set_floats(int location, f32_array_t *values);
void gpu_set_bool(int location, bool value);
void gpu_set_mat3(int location, mat3_t value);
void gpu_set_mat4(int location, mat4_t value);

void  gpu_vertex_structure_add(gpu_vertex_structure_t *structure, const char *name, gpu_vertex_data_t data);
void  gpu_texture_init_from_bytes(gpu_texture_t *texture, void *data, uint32_t width, uint32_t height, gpu_texture_format_t format);
void  gpu_texture_destroy(gpu_texture_t *texture);
void  gpu_texture_destroy_internal(gpu_texture_t *texture);
void  gpu_render_target_init(gpu_texture_t *target, uint32_t width, uint32_t height, gpu_texture_format_t format);
void  gpu_render_target_init2(gpu_texture_t *render_target, uint32_t width, uint32_t height, gpu_texture_format_t format, int framebuffer_index);
void  gpu_vertex_buffer_init(gpu_buffer_t *buffer, uint32_t count, gpu_vertex_structure_t *structure);
void *gpu_vertex_buffer_lock(gpu_buffer_t *buffer);
void  gpu_vertex_buffer_unlock(gpu_buffer_t *buffer);
void  gpu_constant_buffer_init(gpu_buffer_t *buffer, uint32_t size);
void  gpu_constant_buffer_lock(gpu_buffer_t *buffer, uint32_t start, uint32_t count);
void  gpu_constant_buffer_unlock(gpu_buffer_t *buffer);
void  gpu_index_buffer_init(gpu_buffer_t *buffer, uint32_t count);
void  gpu_buffer_destroy(gpu_buffer_t *buffer);
void  gpu_buffer_destroy_internal(gpu_buffer_t *buffer);
void *gpu_index_buffer_lock(gpu_buffer_t *buffer);
void  gpu_index_buffer_unlock(gpu_buffer_t *buffer);

void gpu_pipeline_init(gpu_pipeline_t *pipeline);
void gpu_pipeline_destroy(gpu_pipeline_t *pipeline);
void gpu_pipeline_destroy_internal(gpu_pipeline_t *pipeline);
void gpu_pipeline_compile(gpu_pipeline_t *pipeline);
void gpu_shader_init(gpu_shader_t *shader, const void *source, size_t length, gpu_shader_type_t type);
void gpu_shader_destroy(gpu_shader_t *shader);

void  gpu_draw_internal();
void  gpu_viewport(int x, int y, int width, int height);
void  gpu_scissor(int x, int y, int width, int height);
void  gpu_disable_scissor();
void  gpu_set_pipeline(gpu_pipeline_t *pipeline);
void  gpu_set_pipeline_internal(gpu_pipeline_t *pipeline);
void  gpu_set_vertex_buffer(gpu_buffer_t *buffer);
void  gpu_set_index_buffer(gpu_buffer_t *buffer);
void  gpu_set_constant_buffer(gpu_buffer_t *buffer, uint32_t offset, size_t size);
void  gpu_get_render_target_pixels(gpu_texture_t *render_target, uint8_t *data);
void  gpu_set_texture(uint32_t unit, gpu_texture_t *texture);
void  gpu_use_linear_sampling(bool b);
char *gpu_device_name();
bool  gpu_bc7_supported(int width, int height, gpu_texture_format_t format);

#ifdef WITH_BC7
void *gpu_bc7_compress(void *data, int width, int height);
#endif

bool gpu_raytrace_supported(void);
void gpu_raytrace_pipeline_init(gpu_raytrace_pipeline_t *pipeline, void *shader, int shader_size, gpu_buffer_t *constant_buffer);
void gpu_raytrace_pipeline_destroy(gpu_raytrace_pipeline_t *pipeline);
void gpu_raytrace_acceleration_structure_init(gpu_acceleration_structure_t *accel);
void gpu_raytrace_acceleration_structure_add(gpu_acceleration_structure_t *accel, gpu_buffer_t *vb, gpu_buffer_t *ib, mat4_t transform);
void gpu_raytrace_acceleration_structure_build(gpu_acceleration_structure_t *accel, gpu_buffer_t *_vb_full, gpu_buffer_t *_ib_full);
void gpu_raytrace_acceleration_structure_destroy(gpu_acceleration_structure_t *accel);
void gpu_raytrace_set_textures(gpu_texture_t *texpaint0, gpu_texture_t *texpaint1, gpu_texture_t *texpaint2, gpu_texture_t *texenv, gpu_texture_t *texsobol,
                               gpu_texture_t *texscramble, gpu_texture_t *texrank);
void gpu_raytrace_set_acceleration_structure(gpu_acceleration_structure_t *accel);
void gpu_raytrace_set_pipeline(gpu_raytrace_pipeline_t *pipeline);
void gpu_raytrace_set_target(gpu_texture_t *output);
void gpu_raytrace_dispatch_rays();

void _gpu_raytrace_init(buffer_t *shader);
void _gpu_raytrace_as_init();
void _gpu_raytrace_as_add(gpu_buffer_t *vb, gpu_buffer_t *ib, mat4_t transform);
void _gpu_raytrace_as_build(gpu_buffer_t *vb_full, gpu_buffer_t *ib_full);
void gpu_raytrace_set_textures(gpu_texture_t *tex0, gpu_texture_t *tex1, gpu_texture_t *tex2, gpu_texture_t *texenv, gpu_texture_t *texsobol,
                               gpu_texture_t *texscramble, gpu_texture_t *texrank);
void _gpu_raytrace_dispatch_rays(gpu_texture_t *render_target, buffer_t *buffer);

uint32_t gpu_vertex_data_size(gpu_vertex_data_t data);
uint32_t gpu_vertex_struct_size(gpu_vertex_structure_t *s);
uint32_t gpu_texture_format_size(gpu_texture_format_t format);

extern bool            gpu_in_use;
extern gpu_texture_t  *current_textures[GPU_MAX_TEXTURES];
extern gpu_texture_t  *current_render_targets[8];
extern uint32_t        current_render_targets_count;
extern gpu_texture_t  *current_depth_buffer;
extern gpu_pipeline_t *current_pipeline;
extern uint32_t        constant_buffer_index;
extern gpu_texture_t   framebuffers[GPU_FRAMEBUFFER_COUNT];
extern gpu_texture_t   framebuffer_depth;
extern uint32_t        framebuffer_index;
