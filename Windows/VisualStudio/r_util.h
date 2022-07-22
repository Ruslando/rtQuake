#pragma once

#include "quakedef.h"
#include <stdbool.h>

uint32_t
get_memory_type(uint32_t mem_req_type_bits, VkMemoryPropertyFlags mem_prop);
VkDeviceAddress
get_buffer_device_address(VkBuffer buffer);

VkResult
buffer_destroy(BufferResource_t* buf);

void*
buffer_map(BufferResource_t* buf);

void
buffer_unmap(BufferResource_t* buf);

int accel_matches(accel_match_info_t* match,
    int fast_build,
    uint32_t vertex_count,
    uint32_t index_count);

int accel_matches_top_level(accel_match_info_t* match,
    int fast_build,
    uint32_t instance_count);

void destroy_accel_struct(accel_struct_t* blas);

VkResult
buffer_create(
    BufferResource_t* buf,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags mem_properties);