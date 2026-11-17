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

#include "texture_depth.hpp"

#include "context.hpp"

using namespace ruis::render::vulkan;

texture_depth::texture_depth(
	const utki::shared_ref<const ruis::render::context>& rendering_context, //
	r4::vector2<uint32_t> dims
) :
	ruis::render::texture_depth(rendering_context, dims),
	ctx_ref(rendering_context)
{
	this->create_image();
}

texture_depth::~texture_depth()
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
	auto& ctx = static_cast<const context&>(this->ctx_ref.get());
	vkDestroyImageView(ctx.device, this->image_view, nullptr);
	vkDestroyImage(ctx.device, this->image, nullptr);
	if (this->memory != VK_NULL_HANDLE) {
		vkFreeMemory(ctx.device, this->memory, nullptr);
	}
}

void texture_depth::create_image()
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the renderer owns the object; the base-class reference comes from the ruis API")
	auto& ctx = static_cast<const context&>(this->ctx_ref.get());

	const auto image_info = VkImageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent = {static_cast<uint32_t>(this->dims().x()), static_cast<uint32_t>(this->dims().y()), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	VU_CHECK(vkCreateImage(ctx.device, &image_info, nullptr, &this->image));

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(ctx.device, this->image, &mem_reqs);
	const auto alloc_info = VkMemoryAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = pick_device_local_memory_type(ctx.physical_device, mem_reqs)
	};
	VU_CHECK(vkAllocateMemory(ctx.device, &alloc_info, nullptr, &this->memory));
	VU_CHECK(vkBindImageMemory(ctx.device, this->image, this->memory, 0));

	const auto view_info = VkImageViewCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = this->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.subresourceRange =
			{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
							   .baseMipLevel = 0,
							   .levelCount = 1,
							   .baseArrayLayer = 0,
							   .layerCount = 1}
	};
	VU_CHECK(vkCreateImageView(ctx.device, &view_info, nullptr, &this->image_view));
}
