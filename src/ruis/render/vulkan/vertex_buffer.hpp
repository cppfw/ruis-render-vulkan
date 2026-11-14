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

#include <r4/vector.hpp>
#include <ruis/render/vertex_buffer.hpp>
#include <utki/span.hpp>
#include <vulkan/vulkan.h>

#include "vulkan.hpp"

namespace ruis::render::vulkan {

/**
 * @brief A vertex buffer.
 *
 * All vertex data is stored as vec4 (4 float components) on the GPU. Inputs with
 * fewer components (vec2, vec3 or a single float) are padded to vec4:
 *   - vec2 (x, y)       -> (x, y, 0, 1)
 *   - vec3 (x, y, z)    -> (x, y, z, 1)
 *   - float (f)         -> (f, 0, 0, 1)
 *   - vec4 (x, y, z, w) -> (x, y, z, w)
 *
 * This makes the vertex input layout uniform for all buffers: every attribute
 * location is a 16-byte R32G32B32A32_SFLOAT stream.
 */
class vertex_buffer : public ruis::render::vertex_buffer
{
public:
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	uint32_t byte_size = 0;

	vertex_buffer(
		const utki::shared_ref<const ruis::render::context>& rendering_context, //
		std::vector<float>&& padded_data
	);

	vertex_buffer(const vertex_buffer&) = delete;
	vertex_buffer& operator=(const vertex_buffer&) = delete;
	vertex_buffer(vertex_buffer&&) = delete;
	vertex_buffer& operator=(vertex_buffer&&) = delete;

	~vertex_buffer() override;

private:
	const utki::shared_ref<const ruis::render::context> ctx_ref;
	void create_vk_buffer(std::vector<float>&& data);
};

} // namespace ruis::render::vulkan
