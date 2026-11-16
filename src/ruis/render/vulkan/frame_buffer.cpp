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

#include "frame_buffer.hpp"

#include "context.hpp"

using namespace ruis::render::vulkan;

frame_buffer::frame_buffer(
	const utki::shared_ref<ruis::render::context>& rendering_context, //
	std::shared_ptr<ruis::render::texture_2d> color,
	std::shared_ptr<ruis::render::texture_depth> depth,
	std::shared_ptr<ruis::render::texture_stencil> stencil
) :
	ruis::render::frame_buffer(rendering_context, std::move(color), std::move(depth), std::move(stencil)),
	ctx_ref(rendering_context)
{
	// validate that the color and depth attachments are Vulkan textures
	const auto& vcolor = this->vulkan_color();
	const auto* vdepth = this->vulkan_depth();

	// create the VkFramebuffer
	auto& ctx = static_cast<const context&>(this->ctx_ref.get());
	const auto w = uint32_t(vcolor.dims().x());
	const auto h = uint32_t(vcolor.dims().y());

	if (vdepth != nullptr) {
		const VkImageView attachments[2] = {vcolor.image_view, vdepth->image_view};
		const auto fb_info = VkFramebufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = ctx.fbo_render_pass_clear_depth, // same attachment layout for clear/load
			.attachmentCount = 2,
			.pAttachments = attachments,
			.width = w,
			.height = h,
			.layers = 1
		};
		VU_CHECK(vkCreateFramebuffer(ctx.device, &fb_info, nullptr, &this->framebuffer));
	} else {
		const VkImageView attachments[1] = {vcolor.image_view};
		const auto fb_info = VkFramebufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = ctx.fbo_render_pass_clear_nod,
			.attachmentCount = 1,
			.pAttachments = attachments,
			.width = w,
			.height = h,
			.layers = 1
		};
		VU_CHECK(vkCreateFramebuffer(ctx.device, &fb_info, nullptr, &this->framebuffer));
	}
}

frame_buffer::~frame_buffer()
{
	if (this->framebuffer != VK_NULL_HANDLE) {
		auto& ctx = static_cast<const context&>(this->ctx_ref.get());
		vkDestroyFramebuffer(ctx.device, this->framebuffer, nullptr);
	}
}

VkFramebuffer frame_buffer::vulkan_framebuffer() const
{
	return this->framebuffer;
}

rasterimage::image_variant frame_buffer::get_color_pixels()
{
	// read the color attachment back to CPU RAM. the FBO render was recorded in the
	// frame's command buffer (deferred), so the readback is appended to it, then the
	// frame is submitted and waited on.
	auto& ctx = // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		const_cast<context&>(static_cast<const context&>(this->ctx_ref.get()));
	return ctx.readback_framebuffer_color(*this);
}

const texture_2d& frame_buffer::vulkan_color() const
{
	utki::assert(this->color, SL);
	utki::assert(dynamic_cast<const texture_2d*>(this->color.get()), SL);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done")
	return static_cast<const texture_2d&>(*this->color);
}

const texture_depth* frame_buffer::vulkan_depth() const
{
	if (!this->depth) {
		return nullptr;
	}
	utki::assert(dynamic_cast<const texture_depth*>(this->depth.get()), SL);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done")
	return static_cast<const texture_depth*>(this->depth.get());
}
