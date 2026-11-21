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

#include "factory.hpp"

#include <cstring>

#include "shaders/shader_color.hpp"
#include "shaders/shader_color_pos_lum.hpp"
#include "shaders/shader_color_pos_tex.hpp"
#include "shaders/shader_color_pos_tex_alpha.hpp"
#include "shaders/shader_pos_clr.hpp"
#include "shaders/shader_pos_tex.hpp"

#include "index_buffer.hpp"
#include "texture_2d.hpp"
#include "texture_depth.hpp"
#include "vertex_array.hpp"
#include "vertex_buffer.hpp"

using namespace ruis::render::vulkan;

factory::factory(const utki::shared_ref<const surface>& surface) : //
	device(surface.get().get_device()), //
	physical_device(surface.get().get_physical_device()), //
	queue(surface.get().get_queue()), //
	instance(surface.get().get_instance())
{
	// ---- one-shot command submission resources (shared: used for texture uploads) ----
	{
		const auto pool_info = VkCommandPoolCreateInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = 0
		};
		{
			check_vk_result(
				vkCreateCommandPool(this->device, &pool_info, nullptr, &this->one_shot_pool),
				"vkCreateCommandPool"
			);
		}

		const auto alloc_info = VkCommandBufferAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = this->one_shot_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};
		check_vk_result(
			vkAllocateCommandBuffers(this->device, &alloc_info, &this->one_shot_command_buffer),
			"vkAllocateCommandBuffers"
		);
	}
	{
		const auto fence_info = VkFenceCreateInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		check_vk_result(vkCreateFence(this->device, &fence_info, nullptr, &this->one_shot_fence), "vkCreateFence");
	}

	// ---- descriptor set layouts + pipeline layouts (shared) ----
	{
		// the UBO is read from both the vertex shader (matrix) and the fragment
		// shader (color), so both stages must be in the stage flags
		const auto ubo_binding = VkDescriptorSetLayoutBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
		};
		{
			const auto dsl_info = VkDescriptorSetLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 1,
				.pBindings = &ubo_binding
			};
			check_vk_result(
				vkCreateDescriptorSetLayout(this->device, &dsl_info, nullptr, &this->dsl_no_tex),
				"vkCreateDescriptorSetLayout"
			);
		}

		const auto with_tex_bindings = std::array<VkDescriptorSetLayoutBinding, 2>{
			{ubo_binding,
			 {.binding = 1,
			  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .descriptorCount = 1,
			  .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}}
		};
		{
			const auto dsl_info = VkDescriptorSetLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 2,
				.pBindings = with_tex_bindings.data()
			};
			check_vk_result(
				vkCreateDescriptorSetLayout(this->device, &dsl_info, nullptr, &this->dsl_with_tex),
				"vkCreateDescriptorSetLayout"
			);
		}
	}
	{
		{
			const auto pl_info = VkPipelineLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &this->dsl_no_tex
			};
			check_vk_result(
				vkCreatePipelineLayout(this->device, &pl_info, nullptr, &this->pipeline_layout_no_tex),
				"vkCreatePipelineLayout"
			);
		}
		{
			const auto pl_info = VkPipelineLayoutCreateInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = 1,
				.pSetLayouts = &this->dsl_with_tex
			};
			check_vk_result(
				vkCreatePipelineLayout(this->device, &pl_info, nullptr, &this->pipeline_layout_with_tex),
				"vkCreatePipelineLayout"
			);
		}
	}
	{
		const auto pc_info = VkPipelineCacheCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
		check_vk_result(
			vkCreatePipelineCache(this->device, &pc_info, nullptr, &this->pipeline_cache),
			"vkCreatePipelineCache"
		);
	}
}

factory::~factory()
{
	// ---- shared descriptor / pipeline resources ----
	if (this->pipeline_cache != VK_NULL_HANDLE) {
		vkDestroyPipelineCache(this->device, this->pipeline_cache, nullptr);
	}
	if (this->pipeline_layout_no_tex != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(this->device, this->pipeline_layout_no_tex, nullptr);
	}
	if (this->pipeline_layout_with_tex != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(this->device, this->pipeline_layout_with_tex, nullptr);
	}
	if (this->dsl_no_tex != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(this->device, this->dsl_no_tex, nullptr);
	}
	if (this->dsl_with_tex != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(this->device, this->dsl_with_tex, nullptr);
	}

	// ---- one-shot command submission resources ----
	if (this->one_shot_fence != VK_NULL_HANDLE) {
		vkDestroyFence(this->device, this->one_shot_fence, nullptr);
	}
	if (this->one_shot_command_buffer != VK_NULL_HANDLE) {
		vkFreeCommandBuffers(this->device, this->one_shot_pool, 1, &this->one_shot_command_buffer);
	}
	if (this->one_shot_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(this->device, this->one_shot_pool, nullptr);
	}
}

utki::shared_ref<ruis::render::factory::shaders> factory::make_shaders() const
{
	auto ret = utki::make_shared<ruis::render::factory::shaders>();
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().pos_tex = std::make_unique<shader_pos_tex>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos = std::make_unique<shader_color>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().pos_clr = std::make_unique<shader_pos_clr>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos_tex = std::make_unique<shader_color_pos_tex>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos_tex_alpha = std::make_unique<shader_color_pos_tex_alpha>(this->get_shared_ref());
	// NOLINTNEXTLINE(bugprone-unused-return-value, "false positive")
	ret.get().color_pos_lum = std::make_unique<shader_color_pos_lum>(this->get_shared_ref());
	return ret;
}

utki::shared_ref<ruis::render::texture_2d> factory::make_texture_2d(
	rasterimage::format format,
	rasterimage::dimensioned::dimensions_type dims,
	texture_2d_parameters params
) const
{
	return utki::make_shared<texture_2d>(this->get_shared_ref(), format, dims, nullptr, 0, params);
}

utki::shared_ref<ruis::render::texture_2d> factory::make_texture_2d(
	const rasterimage::image_variant& imvar,
	texture_2d_parameters params
) const
{
	(void)imvar;
	(void)params;
	// Not supported (need rvalue reference to move the data)
	throw std::logic_error("vulkan make_texture_2d: const image_variant not supported");
}

utki::shared_ref<ruis::render::texture_2d> factory::make_texture_2d(
	rasterimage::image_variant&& imvar, //
	texture_2d_parameters params
) const
{
	auto iv = std::move(imvar);
	return std::visit(
		[this, &imvar = iv, &params](auto&& im) -> utki::shared_ref<ruis::render::texture_2d> {
			if constexpr (sizeof(im.pixels().front().front()) != 1) {
				throw std::logic_error("vulkan make_texture_2d: non-8bit images are not supported");
			} else {
				// NOTE: do NOT flip_vertical() here. Unlike OpenGL (texture origin at
				// bottom-left), Vulkan's image row 0 is the top of the texture, so the
				// top-to-bottom image data must be uploaded as-is.
				auto data = im.pixels();
				return utki::make_shared<texture_2d>(
					this->get_shared_ref(), //
					imvar.get_format(),
					im.dims(),
					data.front().data(),
					data.size_bytes(),
					std::move(params)
				);
			}
		},
		iv.variant
	);
}

utki::shared_ref<ruis::render::texture_depth> factory::make_texture_depth(rasterimage::dimensioned::dimensions_type dims
) const
{
	return utki::make_shared<texture_depth>(this->get_shared_ref(), dims);
}

utki::shared_ref<ruis::render::texture_cube> factory::make_texture_cube(
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& positive_x, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& negative_x, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& positive_y, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& negative_y, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& positive_z, //
	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved, "unimplemented stub: signature must match the base class")
	rasterimage::image_variant&& negative_z //
) const
{
	(void)positive_x;
	(void)negative_x;
	(void)positive_y;
	(void)negative_y;
	(void)positive_z;
	(void)negative_z;
	throw std::runtime_error("vulkan make_texture_cube: not implemented");
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer(utki::span<const r4::vector4<float>> vertices
) const
{
	std::vector<float> padded;
	padded.reserve(vertices.size() * 4);
	for (const auto& v : vertices) {
		padded.push_back(v.x());
		padded.push_back(v.y());
		padded.push_back(v.z());
		padded.push_back(v.w());
	}
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer(utki::span<const r4::vector3<float>> vertices
) const
{
	std::vector<float> padded;
	padded.reserve(vertices.size() * 4);
	for (const auto& v : vertices) {
		padded.push_back(v.x());
		padded.push_back(v.y());
		padded.push_back(v.z());
		padded.push_back(1.0f);
	}
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer(utki::span<const r4::vector2<float>> vertices
) const
{
	std::vector<float> padded;
	padded.reserve(vertices.size() * 4);
	for (const auto& v : vertices) {
		padded.push_back(v.x());
		padded.push_back(v.y());
		padded.push_back(0.0f);
		padded.push_back(1.0f);
	}
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::vertex_buffer> factory::make_vertex_buffer(utki::span<const float> vertices) const
{
	// a flat list of floats: interpreted as 4-component vertices
	std::vector<float> padded(vertices.begin(), vertices.end());
	return utki::make_shared<vertex_buffer>(this->get_shared_ref(), std::move(padded));
}

utki::shared_ref<ruis::render::index_buffer> factory::make_index_buffer(utki::span<const uint16_t> indices) const
{
	return utki::make_shared<index_buffer>(this->get_shared_ref(), indices);
}

utki::shared_ref<ruis::render::index_buffer> factory::make_index_buffer(utki::span<const uint32_t> indices) const
{
	return utki::make_shared<index_buffer>(this->get_shared_ref(), indices);
}

utki::shared_ref<ruis::render::vertex_array> factory::make_vertex_array(
	std::vector<utki::shared_ref<const ruis::render::vertex_buffer>> buffers, //
	utki::shared_ref<const ruis::render::index_buffer> indices,
	ruis::render::vertex_array::mode rendering_mode
) const
{
	return utki::make_shared<vertex_array>(
		this->get_shared_ref(),
		std::move(buffers),
		std::move(indices),
		rendering_mode
	);
}

void factory::one_shot(std::function<void(VkCommandBuffer)> cmd) const
{
	// wait for all previously submitted work on the queue to complete. vkQueueWaitIdle is
	// used instead of vkWaitForFences because the AMD driver does not respect the
	// timeout parameter of vkWaitForFences, which deadlocks when waiting for a fence
	// that was never submitted (e.g. the first call).
	vkQueueWaitIdle(this->queue);

	check_vk_result(vkResetCommandBuffer(this->one_shot_command_buffer, 0), "vkResetCommandBuffer");
	const auto begin_info = VkCommandBufferBeginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	check_vk_result(vkBeginCommandBuffer(this->one_shot_command_buffer, &begin_info), "vkBeginCommandBuffer");

	cmd(this->one_shot_command_buffer);

	check_vk_result(vkEndCommandBuffer(this->one_shot_command_buffer), "vkEndCommandBuffer");

	const auto submit_info = VkSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &this->one_shot_command_buffer,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = nullptr
	};
	// VK_NULL_HANDLE instead of nullptr: on 32-bit targets the Vulkan headers
	// define non-dispatchable handles (VkFence, VkSemaphore, ...) as 64-bit
	// integers, not pointers
	check_vk_result(vkQueueSubmit(this->queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");

	// wait for the command buffer to complete, so the caller can safely use the
	// uploaded resources right away (e.g. sample the texture in the current frame)
	vkQueueWaitIdle(this->queue);
}

void factory::ensure_textures_uploaded() const
{
	// find textures that need uploading (no global flag: new textures can be created at any time)
	std::vector<texture_2d*> pending;
	for (auto* tex : this->all_textures) {
		if (!tex->uploaded && !tex->deferred_data.empty()) {
			pending.push_back(tex);
		}
	}

	if (pending.empty()) {
		// mark empty textures as uploaded
		for (auto* tex : this->all_textures) {
			if (!tex->uploaded && tex->deferred_data.empty()) {
				tex->uploaded = true;
			}
		}
		return;
	}

	// calculate total staging size
	VkDeviceSize total_size = 0;
	for (auto* tex : pending) {
		total_size += tex->staging_size();
	}

	if (total_size == 0) {
		return;
	}

	// create a single staging buffer for all textures
	VkBuffer staging_buffer = VK_NULL_HANDLE;
	VkDeviceMemory staging_memory = VK_NULL_HANDLE;
	{
		const auto buffer_info = VkBufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = total_size,
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};
		check_vk_result(vkCreateBuffer(this->device, &buffer_info, nullptr, &staging_buffer), "vkCreateBuffer");

		VkMemoryRequirements mem_reqs{};
		vkGetBufferMemoryRequirements(this->device, staging_buffer, &mem_reqs);

		const auto alloc_info = VkMemoryAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = mem_reqs.size,
			.memoryTypeIndex = pick_host_visible_memory_type(this->physical_device, mem_reqs)
		};
		check_vk_result(vkAllocateMemory(this->device, &alloc_info, nullptr, &staging_memory), "vkAllocateMemory");
		check_vk_result(vkBindBufferMemory(this->device, staging_buffer, staging_memory, 0), "vkBindBufferMemory");
	}

	// copy all texture data into the staging buffer
	void* mapped = nullptr;
	check_vk_result(vkMapMemory(this->device, staging_memory, 0, total_size, 0, &mapped), "vkMapMemory");
	const auto mapped_span = utki::span<uint8_t>(static_cast<uint8_t*>(mapped), total_size);
	VkDeviceSize offset = 0;
	for (auto* tex : pending) {
		const auto sz = tex->staging_size();
		std::memcpy(mapped_span.subspan(offset, sz).data(), tex->deferred_data.data(), sz);
		offset += sz;
	}
	vkUnmapMemory(this->device, staging_memory);

	// record all upload commands into a single one-shot command buffer
	VkDeviceSize cmd_offset = 0;
	this->one_shot([&](VkCommandBuffer cmd) {
		for (auto* tex : pending) {
			const auto sz = tex->staging_size();
			tex->record_upload_commands(cmd, staging_buffer, cmd_offset);
			cmd_offset += sz;
		}
	});

	// clean up
	vkFreeMemory(this->device, staging_memory, nullptr);
	vkDestroyBuffer(this->device, staging_buffer, nullptr);

	// mark uploaded textures
	for (auto* tex : pending) {
		tex->deferred_data.clear();
		tex->uploaded = true;
	}
}

void factory::unregister_texture(texture_2d* t) const
{
	auto& tex_list = this->all_textures;
	tex_list.erase(std::ranges::find(tex_list, t));
}
