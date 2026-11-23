#pragma once
#include "webgpu.h"
#include <stdbool.h>

#define GPU_RAYTRACE_MAX_OBJECTS 64

typedef struct {
	WGPUBuffer buf;
	void      *mem;
	int        start;
	int        allocated_size;
} gpu_buffer_impl_t;

typedef struct {
	WGPUTexture     texture;
	WGPUTextureView view;
} gpu_texture_impl_t;

typedef struct {
	WGPURenderPipeline pipeline;
	WGPURenderPipeline pipeline_unfilterable;
	WGPUPipelineLayout pipeline_layout;
	WGPUPipelineLayout pipeline_layout_unfilterable;
} gpu_pipeline_impl_t;

typedef struct {
	void  *source;
	size_t length;
} gpu_shader_impl_t;

typedef struct {
	int empty;
} gpu_acceleration_structure_impl_t;

#define GPU_COMPUTE_MAX_BINDINGS 16

typedef struct {
	WGPUComputePipeline pipeline;
	WGPUBindGroupLayout layout;
	WGPUTextureFormat   storage_format;
	uint32_t            workgroup_size[2];
	int                 binding_count;
	uint32_t            bindings[GPU_COMPUTE_MAX_BINDINGS];
	uint8_t             kinds[GPU_COMPUTE_MAX_BINDINGS];
	int                 units[GPU_COMPUTE_MAX_BINDINGS];
} gpu_compute_pipeline_impl_t;
