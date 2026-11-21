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

#include "shader_pos_clr.hpp"

#include <iterator>

#include "shader_spirv.hpp"

using namespace ruis::render::vulkan;

shader_pos_clr::shader_pos_clr(utki::shared_ref<const ruis::render::factory> factory) :
	ruis::render::shader(std::move(factory))
{}

void shader_pos_clr::render(const r4::matrix4<float>& m, const ruis::render::vertex_array& va) const
{
	vulkan_shader_render(
		shader_kind::pos_clr,
		std::data(spirv_shader_pos_clr_vertex),
		spirv_shader_pos_clr_vertex_size,
		std::data(spirv_shader_pos_clr_fragment),
		spirv_shader_pos_clr_fragment_size,
		nullptr, // no texture
		m,
		r4::vector4<float>(0, 0, 0, 0),
		va
	);
}
