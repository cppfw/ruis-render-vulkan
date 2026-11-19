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

#pragma once

#include <vector>

#include <ruis/render/index_buffer.hpp>
#include <utki/span.hpp>
#include <vulkan/vulkan.h>

namespace ruis::render::vulkan {

class index_buffer : public ruis::render::index_buffer
{
public:
	VkBuffer buffer = VK_NULL_HANDLE;
	const utki::shared_ref<const ruis::render::factory> ctx_ref;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkIndexType index_type = VK_INDEX_TYPE_UINT16;
	uint32_t elements_count = 0;
	uint32_t byte_size = 0;

	index_buffer(
		const utki::shared_ref<const ruis::render::factory>& rendering_context, //
		const void* data,
		size_t size_bytes,
		size_t size,
		VkIndexType index_type
	);

	index_buffer(
		const utki::shared_ref<const ruis::render::factory>& rendering_context, //
		utki::span<const uint16_t> indices
	);
	index_buffer(
		const utki::shared_ref<const ruis::render::factory>& rendering_context, //
		utki::span<const uint32_t> indices
	);

	index_buffer(const index_buffer&) = delete;
	index_buffer& operator=(const index_buffer&) = delete;
	index_buffer(index_buffer&&) = delete;
	index_buffer& operator=(index_buffer&&) = delete;

	~index_buffer() override;

private:
	void create_vk_buffer(const void* data, size_t size_bytes);
};

} // namespace ruis::render::vulkan
