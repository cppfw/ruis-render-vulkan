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

#include <ruis/render/frame_buffer.hpp>

#include "texture_2d.hpp"
#include "texture_depth.hpp"

namespace ruis::render::vulkan {

class frame_buffer : public ruis::render::frame_buffer
{
public:
	frame_buffer(
		const utki::shared_ref<ruis::render::context>& rendering_context, //
		std::shared_ptr<ruis::render::texture_2d> color,
		std::shared_ptr<ruis::render::texture_depth> depth,
		std::shared_ptr<ruis::render::texture_stencil> stencil
	);

	frame_buffer(const frame_buffer&) = delete;
	frame_buffer& operator=(const frame_buffer&) = delete;
	frame_buffer(frame_buffer&&) = delete;
	frame_buffer& operator=(frame_buffer&&) = delete;

	~frame_buffer() override;

	rasterimage::image_variant get_color_pixels() override;

	// the Vulkan color attachment image view (used by the render pass)
	const texture_2d& vulkan_color() const;
	// the Vulkan depth attachment image view (or nullptr if no depth)
	const texture_depth* vulkan_depth() const;
	// the VkFramebuffer for this FBO
	VkFramebuffer vulkan_framebuffer() const;

private:
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

} // namespace ruis::render::vulkan
