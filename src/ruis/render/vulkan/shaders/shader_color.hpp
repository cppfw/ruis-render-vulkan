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

#include <ruis/render/shaders/coloring_shader.hpp>

#include "shader_common.hpp"

namespace ruis::render::vulkan {

// renders a full-screen color quad: position in a0, color as a uniform
class shader_color : public ruis::render::coloring_shader
{
public:
	shader_color(const utki::shared_ref<const ruis::render::factory>& rendering_context);

	void render(const r4::matrix4<float>& m, const ruis::render::vertex_array& va, const r4::vector4<float>& color)
		const override;

private:
	const utki::shared_ref<const ruis::render::factory> ctx_ref;
};

} // namespace ruis::render::vulkan
