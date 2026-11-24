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

#include "shader_pos_tex.hpp"

#include <iterator>

#include "../texture_2d.hpp"

#include "shader_spirv.hpp"

using namespace ruis::render::vulkan;

shader_pos_tex::shader_pos_tex(utki::shared_ref<const ruis::render::factory> factory) :
	ruis::render::texturing_shader(std::move(factory))
{}

void shader_pos_tex::render(
	const r4::matrix4<float>& m,
	const ruis::render::vertex_array& va,
	const ruis::render::texture_2d& tex
) const
{
	utki::assert(dynamic_cast<const texture_2d*>(&tex), SL);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done")
	const auto& vk_tex = static_cast<const texture_2d&>(tex);

	// textured quads always use alpha blending so that transparent/empty
	// textures do not overwrite the background
	{
		auto& ctx = ruis::render::context::current();
		ctx.enable_blend(true);
		ctx.set_blend_func(
			ruis::render::context::blend_factor::src_alpha, //
			ruis::render::context::blend_factor::one_minus_src_alpha, //
			ruis::render::context::blend_factor::one, //
			ruis::render::context::blend_factor::one_minus_src_alpha //
		);
	}

	vulkan_shader_render(
		shader_kind::pos_tex,
		std::data(spirv_shader_pos_tex_vertex),
		spirv_shader_pos_tex_vertex_size,
		std::data(spirv_shader_pos_tex_fragment),
		spirv_shader_pos_tex_fragment_size,
		&vk_tex, //
		m,
		r4::vector4<float>(1, 1, 1, 1),
		va
	);
}
