#pragma once
#include "webgpu.h"

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
