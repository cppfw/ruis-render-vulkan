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

#include "shader_color_pos_tex.hpp"

#include "../texture_2d.hpp"
#include "shader_spirv.hpp"

using namespace ruis::render::vulkan;

shader_color_pos_tex::shader_color_pos_tex(
	const utki::shared_ref<const ruis::render::context>& rendering_context
) :
	ruis::render::coloring_texturing_shader(rendering_context),
	ctx_ref(rendering_context)
{}

void shader_color_pos_tex::render(
	const r4::matrix4<float>& m,
	const ruis::render::vertex_array& va,
	const r4::vector4<float>& color,
	const ruis::render::texture_2d& tex
) const
{
	utki::assert(dynamic_cast<const texture_2d*>(&tex), SL);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done")
	const auto& vk_tex = static_cast<const texture_2d&>(tex);

	vulkan_shader_render(
		this->ctx_ref, //
		shader_kind::color_pos_tex,
		spirv_shader_color_pos_tex_vertex,
		sizeof(spirv_shader_color_pos_tex_vertex) / sizeof(uint32_t),
		spirv_shader_color_pos_tex_fragment,
		sizeof(spirv_shader_color_pos_tex_fragment) / sizeof(uint32_t),
		&vk_tex, //
		m,
		color,
		va
	);
}
