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

#include "texture_2d.hpp"

#include <cstring>
#include <vector>

#include "context.hpp"

using namespace ruis::render::vulkan;

namespace {
// create a host-visible staging buffer with the given contents
void create_staging_buffer(
	VkDevice device, //
	VkPhysicalDevice physical_device,
	const void* data,
	VkDeviceSize size,
	VkBuffer& buffer,
	VkDeviceMemory& memory
)
{
	VkBufferCreateInfo buffer_info{};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VU_CHECK(vkCreateBuffer(device, &buffer_info, nullptr, &buffer));

	VkMemoryRequirements mem_reqs{};
	vkGetBufferMemoryRequirements(device, buffer, &mem_reqs);

	VkPhysicalDeviceMemoryProperties props{};
	vkGetPhysicalDeviceMemoryProperties(physical_device, &props);
	VkMemoryAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = 0;
	bool found = false;
	for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
		if ((mem_reqs.memoryTypeBits & (1 << i)) &&
			(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			alloc_info.memoryTypeIndex = i;
			found = true;
			break;
		}
	}
	if (!found) {
		throw std::runtime_error("create_staging_buffer: no host-visible memory type found");
	}
	VU_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &memory));
	VU_CHECK(vkBindBufferMemory(device, buffer, memory, 0));

	void* mapped = nullptr;
	VU_CHECK(vkMapMemory(device, memory, 0, size, 0, &mapped));
	std::memcpy(mapped, data, size);
	vkUnmapMemory(device, memory);
}

void destroy_staging_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
	if (buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(device, buffer, nullptr);
		vkFreeMemory(device, memory, nullptr);
	}
}
} // namespace

texture_2d::texture_2d(
	const utki::shared_ref<const ruis::render::context>& rendering_context, //
	rasterimage::format type,
	rasterimage::dimensioned::dimensions_type dims,
	const uint8_t* data,
	size_t data_size,
	ruis::render::context::texture_2d_parameters params
) :
	ruis::render::texture_2d(
		rendering_context, //
		dims
	),
	ctx_ref(rendering_context)
{
	auto& ctx = static_cast<const context&>(rendering_context.get());
	(void)ctx;

	this->ri_format = type;
	this->format = to_vk_format_8bit(type);

	this->create_image(dims);
	this->create_sampler(params);

	// register with the context for deferred upload tracking
	auto& vk_ctx = const_cast<context&>(static_cast<const context&>(rendering_context.get()));
	vk_ctx.all_textures.push_back(this);

	if (data != nullptr && data_size != 0) {
		// defer the upload until the swapchain is ready
		// convert the data to RGBA (4 bytes per pixel) for Vulkan
		const auto w = dims.x();
		const auto h = dims.y();
		const auto pixel_count = size_t(w) * size_t(h);
		this->deferred_data.reserve(pixel_count * 4);
		switch (type) {
			case rasterimage::format::rgba:
				this->deferred_data.assign(data, data + data_size);
				break;
			case rasterimage::format::rgb:
				for (size_t i = 0; i < pixel_count; ++i) {
					this->deferred_data.push_back(data[i * 3 + 0]);
					this->deferred_data.push_back(data[i * 3 + 1]);
					this->deferred_data.push_back(data[i * 3 + 2]);
					this->deferred_data.push_back(255);
				}
				break;
			case rasterimage::format::greya:
				for (size_t i = 0; i < pixel_count; ++i) {
					auto v = data[i * 2 + 0];
					auto a = data[i * 2 + 1];
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(a);
				}
				break;
			case rasterimage::format::grey:
				for (size_t i = 0; i < pixel_count; ++i) {
					auto v = data[i];
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(v);
					this->deferred_data.push_back(255);
				}
				break;
			default:
				this->deferred_data.assign(data, data + data_size);
				break;
		}
	}
}

VkDeviceSize texture_2d::staging_size() const
{
	if (this->deferred_data.empty()) {
		return 0;
	}
	const auto sz = this->deferred_data.size();
	return sz;
}

void texture_2d::record_upload_commands(VkCommandBuffer cmd, VkBuffer staging_buffer, VkDeviceSize offset)
{
	const uint32_t w = static_cast<uint32_t>(this->dims().x());
	const uint32_t h = static_cast<uint32_t>(this->dims().y());

	// transition to transfer dst
	const auto dst_barrier = VkImageMemoryBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = this->image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
	};
	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&dst_barrier
	);

	// all textures use RGBA format (4 bytes per pixel), tightly packed
	const auto region = VkBufferImageCopy{
		.bufferOffset = offset,
		.bufferRowLength = 0, // 0 means tightly packed (use image width)
		.bufferImageHeight = h,
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.imageOffset = {0, 0, 0},
		.imageExtent = {w, h, 1}
	};
	vkCmdCopyBufferToImage(cmd, staging_buffer, this->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	// transition to shader read-only
	const auto img_barrier = VkImageMemoryBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = this->image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
	};
	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&img_barrier
	);
}

void texture_2d::ensure_uploaded()
{
	if (this->uploaded) {
		return;
	}
	if (this->deferred_data.empty()) {
		// no data to upload: clear the image to transparent so that
		// undefined GPU memory does not produce garbage pixels
		auto& ctx = // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			const_cast<context&>(static_cast<const context&>(this->ctx_ref.get()));
		ctx.one_shot([&](VkCommandBuffer cmd) {
			const auto range = VkImageSubresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			};
			const auto barrier = VkImageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = this->image,
				.subresourceRange = range
			};
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0,
				nullptr,
				0,
				nullptr,
				1,
				&barrier
			);
			const auto clear_color = VkClearColorValue{
				.float32 = {0.0f, 0.0f, 0.0f, 0.0f}
			};
			vkCmdClearColorImage(cmd, this->image, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1, &range);
			const auto barrier2 = VkImageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = this->image,
				.subresourceRange = range
			};
			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0,
				0,
				nullptr,
				0,
				nullptr,
				1,
				&barrier2
			);
		});
		this->uploaded = true;
		return;
	}
	this->upload_data(this->deferred_data.data(), this->deferred_data.size());
	this->deferred_data.clear();
	this->uploaded = true;
}

texture_2d::~texture_2d()
{
	auto& ctx = // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		const_cast<context&>(static_cast<const context&>(this->ctx_ref.get()));
	// remove from the context's texture list
	auto& tex_list = ctx.all_textures;
	for (auto it = tex_list.begin(); it != tex_list.end(); ++it) {
		if (*it == this) {
			tex_list.erase(it);
			break;
		}
	}
	vkDestroySampler(ctx.device, this->sampler, nullptr);
	vkDestroyImageView(ctx.device, this->image_view, nullptr);
	vkDestroyImage(ctx.device, this->image, nullptr);
	if (this->memory != VK_NULL_HANDLE) {
		vkFreeMemory(ctx.device, this->memory, nullptr);
	}
}

void texture_2d::create_image(rasterimage::dimensioned::dimensions_type dims)
{
	auto& ctx = // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		const_cast<context&>(static_cast<const context&>(this->ctx_ref.get()));

	VkImageCreateInfo image_info{};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = this->format;
	image_info.extent = {static_cast<uint32_t>(dims.x()), static_cast<uint32_t>(dims.y()), 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage =
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VU_CHECK(vkCreateImage(ctx.device, &image_info, nullptr, &this->image));

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(ctx.device, this->image, &mem_reqs);
	const auto alloc_info = VkMemoryAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = pick_host_visible_memory_type(ctx.physical_device, mem_reqs)
	};
	VU_CHECK(vkAllocateMemory(ctx.device, &alloc_info, nullptr, &this->memory));
	VU_CHECK(vkBindImageMemory(ctx.device, this->image, this->memory, 0));

	const auto view_info = VkImageViewCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = this->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = this->format,
		.components =
			{.r = VK_COMPONENT_SWIZZLE_IDENTITY,
						 .g = VK_COMPONENT_SWIZZLE_IDENTITY,
						 .b = VK_COMPONENT_SWIZZLE_IDENTITY,
						 .a = VK_COMPONENT_SWIZZLE_IDENTITY},
		.subresourceRange =
			{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						 .baseMipLevel = 0,
						 .levelCount = 1,
						 .baseArrayLayer = 0,
						 .layerCount = 1}
	};
	VU_CHECK(vkCreateImageView(ctx.device, &view_info, nullptr, &this->image_view));
}

void texture_2d::create_sampler(ruis::render::context::texture_2d_parameters params)
{
	auto& ctx = // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		const_cast<context&>(static_cast<const context&>(this->ctx_ref.get()));
	(void)ctx;

	const auto sampler_info = VkSamplerCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter =
			params.mag_filter == ruis::render::texture_2d::filter::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
		.minFilter =
			params.min_filter == ruis::render::texture_2d::filter::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_NEVER,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = VK_FALSE
	};
	VU_CHECK(vkCreateSampler(ctx.device, &sampler_info, nullptr, &this->sampler));
}

void texture_2d::upload_data(const uint8_t* data, size_t data_size)
{
	auto& ctx = // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		const_cast<context&>(static_cast<const context&>(this->ctx_ref.get()));

	VkBuffer staging_buffer = VK_NULL_HANDLE;
	VkDeviceMemory staging_memory = VK_NULL_HANDLE;
	create_staging_buffer(ctx.device, ctx.physical_device, data, data_size, staging_buffer, staging_memory);

	ctx.one_shot([&](VkCommandBuffer cmd) {
		const uint32_t w = static_cast<uint32_t>(this->dims().x());
		const uint32_t h = static_cast<uint32_t>(this->dims().y());

		// transition to transfer dst
		const auto dst_barrier = VkImageMemoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = this->image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
		};
		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&dst_barrier
		);

		const auto region = VkBufferImageCopy{
			.bufferOffset = 0,
			.bufferRowLength = 0, // 0 means tightly packed (use image width); data is always RGBA
			.bufferImageHeight = h,
			.imageSubresource =
				{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
			.imageOffset = {0, 0, 0},
			.imageExtent = {w, h, 1}
		};
		vkCmdCopyBufferToImage(
			cmd, //
			staging_buffer,
			this->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region
		);

		// transition to shader read-only
		const auto img_barrier = VkImageMemoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = this->image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
		};
		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&img_barrier
		);
	});

	destroy_staging_buffer(ctx.device, staging_buffer, staging_memory);
}
