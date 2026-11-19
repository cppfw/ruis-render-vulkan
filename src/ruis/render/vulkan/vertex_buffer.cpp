/*
ruis-render-vulkan - Vulkan renderer

Copyright (C) 2012-2026  Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* ================ LICENSE END ================ */

#include "vertex_buffer.hpp"

#include <cstring>

#include "context.hpp"

using namespace ruis::render::vulkan;

vertex_buffer::vertex_buffer(
	const utki::shared_ref<const ruis::render::factory>& rendering_context, //
	std::vector<float>&& padded_data
) :
	ruis::render::vertex_buffer(rendering_context, padded_data.size() / factory::vertex_component_count),
	ctx_ref(rendering_context)
{
	this->create_vk_buffer(std::move(padded_data));
}

vertex_buffer::~vertex_buffer()
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
	auto& ctx = static_cast<const ruis::render::vulkan::factory&>(this->ctx_ref.get());
	if (this->buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(ctx.device, this->buffer, nullptr);
		vkFreeMemory(ctx.device, this->memory, nullptr);
	}
}

void vertex_buffer::create_vk_buffer(
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "consumed by value: the copy is destroyed at end of scope")
	std::vector<float>&& data
)
{
	if (data.empty()) {
		return;
	}

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
	auto& ctx = static_cast<const ruis::render::vulkan::factory&>(this->ctx_ref.get());

	this->byte_size = uint32_t(data.size() * sizeof(float));

	const auto buffer_info = VkBufferCreateInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = this->byte_size,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VU_CHECK(vkCreateBuffer(ctx.device, &buffer_info, nullptr, &this->buffer));

	VkMemoryRequirements mem_reqs{};
	vkGetBufferMemoryRequirements(ctx.device, this->buffer, &mem_reqs);

	const auto alloc_info = VkMemoryAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = pick_host_visible_memory_type(ctx.physical_device, mem_reqs)
	};
	VU_CHECK(vkAllocateMemory(ctx.device, &alloc_info, nullptr, &this->memory));
	VU_CHECK(vkBindBufferMemory(ctx.device, this->buffer, this->memory, 0));

	void* mapped = nullptr;
	VU_CHECK(vkMapMemory(ctx.device, this->memory, 0, this->byte_size, 0, &mapped));
	std::memcpy(mapped, data.data(), data.size() * sizeof(float));
	vkUnmapMemory(ctx.device, this->memory);
}
