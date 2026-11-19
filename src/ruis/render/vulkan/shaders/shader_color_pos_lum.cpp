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

#include "shader_color_pos_lum.hpp"

#include <iterator>

#include "shader_spirv.hpp"

using namespace ruis::render::vulkan;

shader_color_pos_lum::shader_color_pos_lum(utki::shared_ref<const ruis::render::factory> factory) :
	ruis::render::coloring_shader(std::move(factory))
{}

void shader_color_pos_lum::render(
	const r4::matrix4<float>& m,
	const ruis::render::vertex_array& va,
	const r4::vector4<float>& color
) const
{
	vulkan_shader_render(
		this->factory, //
		shader_kind::color_pos_lum,
		std::data(spirv_shader_color_pos_lum_vertex),
		spirv_shader_color_pos_lum_vertex_size,
		std::data(spirv_shader_color_pos_lum_fragment),
		spirv_shader_color_pos_lum_fragment_size,
		nullptr, // no texture
		m,
		color,
		va
	);
}
