/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "r_util.h"


uint32_t
get_memory_type(uint32_t mem_req_type_bits, VkMemoryPropertyFlags mem_prop)
{
	for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
		if (mem_req_type_bits & (1 << i)) {
			if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & mem_prop) == mem_prop)
				return i;
		}
	}

	Con_Printf("get_memory_type: cannot find a memory type with propertyFlags = 0x%x\n", mem_prop);
	return 0;
}

VkDeviceAddress
get_buffer_device_address(VkBuffer buffer)
{
	VkBufferDeviceAddressInfo address_info = {
	  .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	  .buffer = buffer
	};

	return vkGetBufferDeviceAddress(vulkan_globals.device, &address_info);
}

VkResult
buffer_destroy(BufferResource_t* buf)
{
	assert(!buf->is_mapped);
	if (buf->memory != VK_NULL_HANDLE)
		vkFreeMemory(vulkan_globals.device, buf->memory, NULL);
	if (buf->buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(vulkan_globals.device, buf->buffer, NULL);
	buf->buffer = VK_NULL_HANDLE;
	buf->memory = VK_NULL_HANDLE;
	buf->size = 0;
	buf->address = 0;

	return VK_SUCCESS;
}

void*
buffer_map(BufferResource_t* buf)
{
	assert(!buf->is_mapped);
	buf->is_mapped = 1;
	void* ret = NULL;
	assert(buf->memory != VK_NULL_HANDLE);
	assert(buf->size > 0);
	vkMapMemory(vulkan_globals.device, buf->memory, 0 /*offset*/, buf->size, 0 /*flags*/, &ret);
	return ret;
}

void
buffer_unmap(BufferResource_t* buf)
{
	assert(buf->is_mapped);
	buf->is_mapped = 0;
	vkUnmapMemory(vulkan_globals.device, buf->memory);
}

VkResult
buffer_create(
	BufferResource_t* buf,
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VkMemoryPropertyFlags mem_properties)
{
	assert(size > 0);
	assert(buf);
	VkResult result = VK_SUCCESS;

	VkBufferCreateInfo buf_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL
	};

	buf->size = size;
	buf->is_mapped = 0;

	result = vkCreateBuffer(vulkan_globals.device, &buf_create_info, NULL, &buf->buffer);
	if (result != VK_SUCCESS) {
		goto fail_buffer;
	}
	assert(buf->buffer != VK_NULL_HANDLE);

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(vulkan_globals.device, buf->buffer, &mem_reqs);

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = get_memory_type(mem_reqs.memoryTypeBits, mem_properties)
	};

	VkMemoryAllocateFlagsInfo mem_alloc_flags = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0,
		.deviceMask = 0
	};

	//#ifdef VKPT_DEVICE_GROUPS
	//	if (qvk.device_count > 1 && !(mem_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
	//		mem_alloc_flags.flags |= VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT;
	//		mem_alloc_flags.deviceMask = (1 << qvk.device_count) - 1;
	//	}
	//#endif

	mem_alloc_info.pNext = &mem_alloc_flags;

	result = vkAllocateMemory(vulkan_globals.device, &mem_alloc_info, NULL, &buf->memory);
	if (result != VK_SUCCESS) {
		goto fail_mem_alloc;
	}

	assert(buf->memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan_globals.device, buf->buffer, buf->memory, 0);
	if (result != VK_SUCCESS) {
		goto fail_bind_buf_memory;
	}

	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		buf->address = get_buffer_device_address(buf->buffer);
		assert(buf->address);
	}
	else
	{
		buf->address = 0;
	}

	return VK_SUCCESS;

fail_bind_buf_memory:
	vkFreeMemory(vulkan_globals.device, buf->memory, NULL);
fail_mem_alloc:
	vkDestroyBuffer(vulkan_globals.device, buf->buffer, NULL);
fail_buffer:
	buf->buffer = VK_NULL_HANDLE;
	buf->memory = VK_NULL_HANDLE;
	buf->size = 0;
	return result;
}