#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#define GPU_RAYTRACE_MAX_OBJECTS 64

typedef struct gpu_pipeline_impl {
	VkPipeline       pipeline;
	VkPipelineLayout pipeline_layout;

	VkDescriptorSet       descriptor_set;
	VkDescriptorSetLayout descriptor_set_layout;
} gpu_pipeline_impl_t;

typedef struct {
	char *source;
	int   length;
} gpu_shader_impl_t;

typedef struct {
	VkImage        image;
	VkDeviceMemory mem;
	VkImageView    view;
} gpu_texture_impl_t;

typedef struct {
	VkBuffer       buf;
	VkDeviceMemory mem;
	VkBuffer       cpu_buf;
	VkDeviceMemory cpu_mem;
} gpu_buffer_impl_t;

typedef struct {
	VkAccelerationStructureKHR top_level_acceleration_structure;
	VkAccelerationStructureKHR bottom_level_acceleration_structure[GPU_RAYTRACE_MAX_OBJECTS];
	uint64_t                   top_level_acceleration_structure_handle;
	uint64_t                   bottom_level_acceleration_structure_handle[GPU_RAYTRACE_MAX_OBJECTS];

	VkBuffer       bottom_level_buffer[GPU_RAYTRACE_MAX_OBJECTS];
	VkDeviceMemory bottom_level_mem[GPU_RAYTRACE_MAX_OBJECTS];
	VkBuffer       top_level_buffer;
	VkDeviceMemory top_level_mem;
	VkBuffer       instances_buffer;
	VkDeviceMemory instances_mem;
} gpu_acceleration_structure_impl_t;
