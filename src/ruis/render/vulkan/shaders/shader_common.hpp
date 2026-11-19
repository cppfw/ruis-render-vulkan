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

#include <cstddef>
#include <cstdint>

#include <r4/matrix.hpp>
#include <r4/vector.hpp>
#include <ruis/render/vertex_array.hpp>
#include <vulkan/vulkan.h>

namespace ruis::render::vulkan {

// identifies the shader (determines the number of attributes and the uniforms used)
enum class shader_kind : uint32_t {
	color = 0, // a0 = position, uniform color
	pos_clr = 1, // a0 = position, a1 = color
	pos_tex = 2, // a0 = position, a1 = texcoord, texture
	color_pos_tex = 3, // a0 = position, a1 = texcoord, uniform color, texture
	color_pos_tex_alpha = 4, // a0 = position, a1 = texcoord, uniform color, texture alpha
	color_pos_lum = 5 // a0 = position, a1 = luminance, uniform color
};

// does the shader use the a1 attribute?
constexpr bool shader_has_a1(shader_kind k)
{
	return k != shader_kind::color;
}

// does the shader use a texture?
constexpr bool shader_has_texture(shader_kind k)
{
	return k == shader_kind::pos_tex || k == shader_kind::color_pos_tex || k == shader_kind::color_pos_tex_alpha;
}

// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "this is the vulkan-specific context, not ruis::render::context")
class context;
class texture_2d;

/**
 * @brief Common render implementation shared by all Vulkan shaders.
 *
 * Downcasts the rendering context to the Vulkan context, ensures the frame is
 * begun, obtains/creates the pipeline, updates the shared uniform buffer,
 * allocates the descriptor set and issues the draw.
 *
 * The texture (if any) is passed as a pointer to the Vulkan texture_2d so the
 * texture's data can be uploaded (on the context that owns the texture, which
 * may be different from the context currently rendering) before the draw.
 */
void vulkan_shader_render(
	const utki::shared_ref<const ruis::render::factory>& factory, // NOLINT (unused: render uses context::current())
	shader_kind kind,
	const void* vertex_spirv,
	size_t vertex_spirv_len,
	const void* fragment_spirv,
	size_t fragment_spirv_len,
	const texture_2d* texture, // nullptr for non-textured shaders
	const r4::matrix4<float>& m,
	const r4::vector4<float>& color,
	const ruis::render::vertex_array& va
);

} // namespace ruis::render::vulkan
