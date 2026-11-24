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

#include "shader_common.hpp"

#include <cstdlib>
#include <utility>

#include "../context.hpp"
#include "../texture_2d.hpp"
#include "../vertex_array.hpp"

namespace ruis::render::vulkan {

void vulkan_shader_render(
	shader_kind kind,
	const void* vertex_spirv,
	size_t vertex_spirv_len,
	const void* fragment_spirv,
	size_t fragment_spirv_len,
	const texture_2d* texture, // nullptr for non-textured shaders
	const r4::matrix4<float>& m,
	const r4::vector4<float>& color,
	const ruis::render::vertex_array& va
)
{
	// use the currently active context (set by ruis::render::context::apply()),
	// not the context stored in the shader (which may be the shared resource-loader context)
	auto& cur = ruis::render::context::current();
	utki::assert(dynamic_cast<const context*>(&cur) != nullptr, SL);
	auto& ctx =
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done above")
		static_cast<context&>(cur);

	const bool textured = shader_has_texture(kind);

	// skip drawing if the viewport is zero-sized (e.g. resource loader context)
	{
		const auto& vp = ctx.get_viewport();
		if (vp.d.x() == 0 || vp.d.y() == 0) {
			return;
		}
	}

	// make sure the texture data is uploaded to the GPU. The texture is owned by
	// the context that created it (e.g. the shared resource-loader context),
	ctx.ensure_frame_began();

	// map the VAO's rendering mode to a Vulkan primitive topology
	const auto to_vk_topology = [](ruis::render::vertex_array::mode m) -> VkPrimitiveTopology {
		switch (m) {
			case ruis::render::vertex_array::mode::triangles:
				return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case ruis::render::vertex_array::mode::triangle_fan:
				return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			case ruis::render::vertex_array::mode::line_loop:
				return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case ruis::render::vertex_array::mode::triangle_strip:
				return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			default:
				return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	};

	const auto pipeline = ctx.get_or_create_pipeline(
		kind,
		textured,
		to_vk_topology(va.rendering_mode),
		vertex_spirv,
		vertex_spirv_len,
		fragment_spirv,
		fragment_spirv_len
	);

	const auto ubo_buffer = ctx.update_uniforms(m, color);

	const auto descriptor_set = ctx.allocate_descriptor_set(
		textured,
		ubo_buffer,
		texture ? texture->image_view : VK_NULL_HANDLE,
		texture ? texture->sampler : VK_NULL_HANDLE
	);

	ctx.draw_indexed(
		pipeline,
		textured ? ctx.factory.get().get_pipeline_layout_with_tex() : ctx.factory.get().get_pipeline_layout_no_tex(),
		va,
		descriptor_set
	);
}
} // namespace ruis::render::vulkan
