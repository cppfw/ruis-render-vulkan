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

#include "vertex_array.hpp"

#include "context.hpp"
#include "index_buffer.hpp"
#include "vertex_buffer.hpp"

using namespace ruis::render::vulkan;

vertex_array::vertex_array(
	utki::shared_ref<const ruis::render::factory> factory, //
	buffers_type buffers,
	utki::shared_ref<const ruis::render::index_buffer> indices,
	mode rendering_mode
) :
	ruis::render::vertex_array(std::move(factory), std::move(buffers), std::move(indices), rendering_mode)
{
	// all buffers must be Vulkan vertex buffers and the index buffer a Vulkan index buffer
	for (const auto& b : this->buffers) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		(void)dynamic_cast<const vertex_buffer*>(&b.get());
	}
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
	(void)dynamic_cast<const index_buffer*>(&this->indices.get());
}
