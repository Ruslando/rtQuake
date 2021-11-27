#pragma once
#ifndef  __VKPT_H__
#define  __VKPT_H__

#include "quakedef.h"
#include "gl_model.h"


typedef struct BufferResource_s {
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceAddress address;
	size_t size;
	int is_mapped;
} BufferResource_t;

typedef struct accel_match_info_s {
	int fast_build;
	uint32_t vertex_count;
	uint32_t index_count;
	uint32_t aabb_count;
	uint32_t instance_count;
} accel_match_info_t;

typedef struct accel_struct_s {
	VkAccelerationStructureKHR accel;
	accel_match_info_t match;
	BufferResource_t mem;
	qboolean present;
} accel_struct_t;

// Utils
VkResult buffer_create(BufferResource_t* buf, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_properties);
uint32_t get_memory_type(uint32_t mem_req_type_bits, VkMemoryPropertyFlags mem_prop);
VkDeviceAddress get_buffer_device_address(VkBuffer buffer);
VkResult buffer_destroy(BufferResource_t* buf);
void* buffer_map(BufferResource_t* buf);
void buffer_unmap(BufferResource_t* buf);


// path tracing
VkResult vkpt_pt_init();
VkResult vkpt_write_descriptor_set(accel_struct_t* tlas);

// Acceleration structures
// Creates bottom level acceleration strucuture (BLAS)
void vkpt_create_bottomlevel_acceleration_structure(accel_struct_t* blas, qmodel_t* model, const aliashdr_t* header);
void vkpt_create_toplevel_acceleration_structure(accel_struct_t* blas);
static void vkpt_destroy_acceleration_structure(accel_struct_t* blas);

#endif