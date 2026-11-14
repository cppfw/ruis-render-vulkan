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

#include <rasterimage/image_variant.hpp>
#include <ruis/render/context.hpp>
#include <ruis/render/texture_2d.hpp>
#include <vulkan/vulkan.h>

#include "vulkan.hpp"

namespace ruis::render::vulkan {

class texture_2d : public ruis::render::texture_2d
{
public:
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView image_view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	rasterimage::format ri_format = rasterimage::format::rgba;

	texture_2d(
		const utki::shared_ref<const ruis::render::context>& rendering_context, //
		rasterimage::format type,
		rasterimage::dimensioned::dimensions_type dims,
		const uint8_t* data,
		size_t data_size,
		ruis::render::context::texture_2d_parameters params
	);

	// upload the data after the swapchain is ready
	void ensure_uploaded();

	texture_2d(const texture_2d&) = delete;
	texture_2d& operator=(const texture_2d&) = delete;
	texture_2d(texture_2d&&) = delete;
	texture_2d& operator=(texture_2d&&) = delete;

	~texture_2d() override;

private:
	const utki::shared_ref<const ruis::render::context> ctx_ref;
	void create_image(rasterimage::dimensioned::dimensions_type dims);
	void upload_data(const uint8_t* data, size_t data_size);
	void create_sampler(ruis::render::context::texture_2d_parameters params);

	// deferred upload (public for batched upload in context)
public:
	bool uploaded = false;
	rasterimage::format deferred_format = rasterimage::format::rgba;
	rasterimage::dimensioned::dimensions_type deferred_dims{};
	std::vector<uint8_t> deferred_data;
	void record_upload_commands(VkCommandBuffer cmd, VkBuffer staging_buffer, VkDeviceSize offset);
	VkDeviceSize staging_size() const;
private:
};

} // namespace ruis::render::vulkan
