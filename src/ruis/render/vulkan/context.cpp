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

#include "context.hpp"

#include <cstring>
#include <limits>

#include "shaders/shader_color.hpp"
#include "shaders/shader_color_pos_lum.hpp"
#include "shaders/shader_color_pos_tex.hpp"
#include "shaders/shader_color_pos_tex_alpha.hpp"
#include "shaders/shader_pos_clr.hpp"
#include "shaders/shader_pos_tex.hpp"

#include "frame_buffer.hpp"
#include "index_buffer.hpp"
#include "texture_2d.hpp"
#include "texture_depth.hpp"
#include "vertex_array.hpp"
#include "vertex_buffer.hpp"

using namespace ruis::render::vulkan;

namespace {

// maps NDC (-1 .. 1) to the unit range (0 .. 1)
constexpr float ndc_to_unit_scale = 0.5f;

// number of bits per blend factor in the packed blend key (15 values fit in 4,
// use 5 for simplicity)
constexpr uint32_t blend_key_factor_bits = 5;

VkSurfaceFormatKHR pick_surface_format(VkPhysicalDevice phys, VkSurfaceKHR surface)
{
	// Check if the surface is supported by the physical device
	VkBool32 supported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(phys, 0, surface, &supported);

	uint32_t count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(count);
	if (count > 0) {
		vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, formats.data());
	}

	if (count == 0) {
		throw std::runtime_error("no surface formats reported");
	}

	// prefer an 8-bit RGBA format
	for (const auto& f : formats) {
		if (f.format == VK_FORMAT_R8G8B8A8_UNORM) {
			return f;
		}
	}
	for (const auto& f : formats) {
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
			return f;
		}
	}
	// fall back to the first format
	return formats[0];
}

VkPresentModeKHR pick_present_mode(VkPhysicalDevice phys, VkSurfaceKHR surface, bool vsync_enabled)
{
	if (vsync_enabled) {
		return VK_PRESENT_MODE_FIFO_KHR; // capped to monitor refresh rate
	}
	uint32_t count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, nullptr);
	std::vector<VkPresentModeKHR> modes(count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, modes.data());

	for (const auto m : modes) {
		if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
			return m;
		}
		if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			return m;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}
} // namespace

context::context(
	utki::shared_ref<ruis::render::native_window> native_window, //
	utki::shared_ref<const surface> surface, //
	const utki::shared_ref<const ruis::render::vulkan::factory>& factory
) :
	ruis::render::context(
		std::move(native_window),
		factory,
		[] {
			r4::matrix4<float> m{};
			// maps the framework's normalized viewport coordinates (x right, y down,
			// zero at the top-left, right/bottom edges at (1, 1)) to the Vulkan NDC.
			//
			// In the Vulkan NDC (with a positive-height viewport) -1 -1 corresponds
			// to the top-left corner, 1 1 to the bottom-right corner, and z points
			// away from the viewer. This is the same orientation as the framework
			// (y down, z away), so no axis flip is needed here.
			//
			// (In contrast, OpenGL's viewport has y up, so its initial_matrix flips
			// y and also flips z, to keep the coordinate system right-handed.)
			m.set_identity().translate(-1, -1).scale(2, 2);
			return parameters{m};
		}()
	),
	factory(factory), //
	physical_device(surface.get().get_physical_device()), //
	device(surface.get().get_device()), //
	surface_ref(surface), //
	instance(surface.get().get_instance()), //
	queue(surface.get().get_queue()), //
	vk_surface(surface.get().get_surface())
{
	vkGetPhysicalDeviceProperties(this->physical_device, &this->device_properties);

	// ---- command submission resources ----
	{
		const auto pool_info = VkCommandPoolCreateInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = 0
		};
		{
			check_vk_result(
				vkCreateCommandPool(this->device, &pool_info, nullptr, &this->command_pool),
				"vkCreateCommandPool"
			);
		}

		const auto alloc_info = VkCommandBufferAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = this->command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};
		check_vk_result(
			vkAllocateCommandBuffers(this->device, &alloc_info, &this->command_buffer),
			"vkAllocateCommandBuffers"
		);
	}
	{
		const auto fence_info = VkFenceCreateInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		check_vk_result(vkCreateFence(this->device, &fence_info, nullptr, &this->frame_fence), "vkCreateFence");
	}
	{
		const auto sem_info = VkSemaphoreCreateInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		{
			check_vk_result(
				vkCreateSemaphore(this->device, &sem_info, nullptr, &this->image_available_sem),
				"vkCreateSemaphore"
			);
		}
		{
			check_vk_result(
				vkCreateSemaphore(this->device, &sem_info, nullptr, &this->render_finished_sem),
				"vkCreateSemaphore"
			);
		}
	}
	{
		const auto pool_sizes = std::array<VkDescriptorPoolSize, 2>{
			{{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 16384},
			 {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 16384}}
		};
		const auto pool_info = VkDescriptorPoolCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = 16384,
			.poolSizeCount = 2,
			.pPoolSizes = pool_sizes.data()
		};
		{
			check_vk_result(
				vkCreateDescriptorPool(this->device, &pool_info, nullptr, &this->descriptor_pool),
				"vkCreateDescriptorPool"
			);
		}
	}

	// ---- per-draw uniform buffer ring ----
	// the ring starts empty; slots are allocated on demand in allocate_ubo_slot()

	// ---- swapchain (only if we have a surface; headless contexts skip this) ----
	// must come before create_render_passes() which uses swapchain_format
	if (this->surface_ref.get().has_surface()) {
		this->create_swapchain();
	}

	// ---- render passes (needed for both screen and FBO rendering) ----
	this->create_render_passes();

	// ---- swapchain framebuffers ----
	if (this->surface_ref.get().has_surface()) {
		this->create_swapchain_framebuffers();
	}
}

void context::create_swapchain()
{
	utki::assert(this->vk_surface != VK_NULL_HANDLE, SL);
	const auto dims = this->native_window.get().get_dims();
	this->window_dims = r4::vector2<uint32_t>(std::max<uint32_t>(1, dims.x()), std::max<uint32_t>(1, dims.y()));

	const auto surface_format = pick_surface_format(this->physical_device, this->vk_surface);
	this->swapchain_format = surface_format.format;

	VkSurfaceCapabilitiesKHR caps{};
	check_vk_result(
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->physical_device, this->vk_surface, &caps),
		"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"
	);

	// clamp the dimensions to the supported range
	uint32_t width = this->window_dims.x();
	uint32_t height = this->window_dims.y();
	if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		width = caps.currentExtent.width;
		height = caps.currentExtent.height;
	}
	if (width < caps.minImageExtent.width)
		width = caps.minImageExtent.width;
	if (width > caps.maxImageExtent.width)
		width = caps.maxImageExtent.width;
	if (height < caps.minImageExtent.height)
		height = caps.minImageExtent.height;
	if (height > caps.maxImageExtent.height)
		height = caps.maxImageExtent.height;

	uint32_t image_count = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
		image_count = caps.maxImageCount;
	}

	const auto vsync_enabled = this->is_vsync_enabled();
	this->swapchain_vsync = vsync_enabled;
	const auto present_mode = pick_present_mode(this->physical_device, this->vk_surface, vsync_enabled);

	const uint32_t queue_family = 0;

	const auto info = VkSwapchainCreateInfoKHR{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = this->vk_surface,
		.minImageCount = image_count,
		.imageFormat = this->swapchain_format,
		.imageColorSpace = surface_format.colorSpace,
		.imageExtent = {width, height},
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &queue_family,
		.preTransform = caps.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = present_mode,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE
	};

	if (this->swapchain != VK_NULL_HANDLE) {
		// NOTE: do NOT destroy swapchain images with vkDestroyImage - they are managed by the swapchain
		// Destroy the image views first
		for (auto view : this->swapchain_image_views) {
			if (view != VK_NULL_HANDLE) {
				vkDestroyImageView(this->device, view, nullptr);
			}
		}
		vkDestroySwapchainKHR(this->device, this->swapchain, nullptr);
		this->swapchain_images.clear();
		this->swapchain_image_views.clear();
	}

	{
		check_vk_result(vkCreateSwapchainKHR(this->device, &info, nullptr, &this->swapchain), "vkCreateSwapchainKHR");
	}

	uint32_t count = 0;
	check_vk_result(vkGetSwapchainImagesKHR(this->device, this->swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
	this->swapchain_images.resize(count);
	check_vk_result(
		vkGetSwapchainImagesKHR(this->device, this->swapchain, &count, this->swapchain_images.data()),
		"vkGetSwapchainImagesKHR"
	);
	this->swapchain_image_count = count;

	this->window_dims = r4::vector2<uint32_t>(width, height);

	// recreate the depth image to match the new size
	this->create_depth_image();

	// recreate framebuffers for the new swapchain (only if render passes already exist)
	if (this->swapchain_render_pass_clear != VK_NULL_HANDLE) {
		this->create_swapchain_framebuffers();
	}
}

void context::ensure_swapchain_valid()
{
	// headless contexts (e.g. resource loading) have no surface; nothing to do
	if (!this->surface_ref.get().has_surface()) {
		return;
	}
	// use get_dims() which accounts for the HiDPI scale factor
	// (caps.currentExtent may report logical pixels on Wayland)
	const auto native_dims = this->native_window.get().get_dims();
	const auto w = std::max<uint32_t>(1, native_dims.x());
	const auto h = std::max<uint32_t>(1, native_dims.y());
	// also check if vsync changed (requires swapchain recreation)
	const auto vsync_now = this->is_vsync_enabled();
	const bool vsync_changed = (vsync_now != this->swapchain_vsync);
	if (w == this->window_dims.x() && h == this->window_dims.y() && !this->swapchain_suboptimal && !vsync_changed) {
		return;
	}
	// the window was resized (or the swapchain is suboptimal); wait for the GPU to
	// finish before recreating the swapchain
	check_vk_result(vkQueueWaitIdle(this->queue), "vkQueueWaitIdle");
	// reset the frame state (the in-flight frame is discarded)
	this->frame_begun = false;
	this->render_pass_in_progress = false;
	this->frame_has_swapchain = false;
	this->current_swapchain_image = -1;
	this->create_swapchain();
	this->swapchain_suboptimal = false;
}

void context::create_depth_image()
{
	utki::assert(this->window_dims.x() > 0 && this->window_dims.y() > 0, SL);
	// destroy the old depth image if it exists
	if (this->depth_image_view != VK_NULL_HANDLE) {
		vkDestroyImageView(this->device, this->depth_image_view, nullptr);
		this->depth_image_view = VK_NULL_HANDLE;
	}
	if (this->depth_image != VK_NULL_HANDLE) {
		vkDestroyImage(this->device, this->depth_image, nullptr);
		this->depth_image = VK_NULL_HANDLE;
	}
	if (this->depth_image_memory != VK_NULL_HANDLE) {
		vkFreeMemory(this->device, this->depth_image_memory, nullptr);
		this->depth_image_memory = VK_NULL_HANDLE;
	}

	const auto image_info = VkImageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent = {this->window_dims.x(), this->window_dims.y(), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	{
		check_vk_result(vkCreateImage(this->device, &image_info, nullptr, &this->depth_image), "vkCreateImage");
	}

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(this->device, this->depth_image, &mem_reqs);
	const auto alloc_info = VkMemoryAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = pick_device_local_memory_type(this->physical_device, mem_reqs)
	};
	check_vk_result(
		vkAllocateMemory(this->device, &alloc_info, nullptr, &this->depth_image_memory),
		"vkAllocateMemory"
	);
	check_vk_result(
		vkBindImageMemory(this->device, this->depth_image, this->depth_image_memory, 0),
		"vkBindImageMemory"
	);

	{
		const auto view_info = VkImageViewCreateInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = this->depth_image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_D32_SFLOAT,
			.subresourceRange =
				{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
								   .baseMipLevel = 0,
								   .levelCount = 1,
								   .baseArrayLayer = 0,
								   .layerCount = 1}
		};
		check_vk_result(
			vkCreateImageView(this->device, &view_info, nullptr, &this->depth_image_view),
			"vkCreateImageView"
		);
	}
}

void context::create_render_passes()
{
	auto make_render_pass = [this](
								VkFormat color_format, //
								VkImageLayout color_final_layout, //
								VkImageLayout color_load_initial_layout, //
								bool has_depth, //
								VkAttachmentLoadOp load_op
							) {
		std::vector<VkAttachmentDescription> attachments;
		attachments.push_back(VkAttachmentDescription{
			.format = color_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = load_op,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			// when loadOp is LOAD the initialLayout must equal the image's current
			// layout: swapchain images are in PRESENT_SRC_KHR after present, while
			// FBO color images (created in SHADER_READ_ONLY_OPTIMAL) stay there
			.initialLayout =
				(load_op == VK_ATTACHMENT_LOAD_OP_LOAD) ? color_load_initial_layout : VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = color_final_layout
		});
		if (has_depth) {
			attachments.push_back(VkAttachmentDescription{
				.format = VK_FORMAT_D32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = load_op,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = (load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
					? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
					: VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			});
		}

		const auto color_ref =
			VkAttachmentReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		std::vector<VkAttachmentReference> refs = {color_ref};
		if (has_depth) {
			refs.push_back(
				VkAttachmentReference{.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
			);
		}

		const auto subpass = VkSubpassDescription{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_ref,
			.pResolveAttachments = nullptr,
			.pDepthStencilAttachment = has_depth ? &refs[1] : nullptr,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments = nullptr
		};

		const auto info = VkRenderPassCreateInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 0,
			.pDependencies = nullptr
		};
		{
			VkRenderPass render_pass{};
			check_vk_result(vkCreateRenderPass(this->device, &info, nullptr, &render_pass), "vkCreateRenderPass");
			return render_pass;
		}
	};

	this->swapchain_render_pass_clear = make_render_pass(
		this->swapchain_format,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		true,
		VK_ATTACHMENT_LOAD_OP_CLEAR
	);
	this->swapchain_render_pass_load = make_render_pass(
		this->swapchain_format,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		true,
		VK_ATTACHMENT_LOAD_OP_LOAD
	);
	this->fbo_render_pass_clear_depth = make_render_pass(
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		true,
		VK_ATTACHMENT_LOAD_OP_CLEAR
	);
	this->fbo_render_pass_load_depth = make_render_pass(
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		true,
		VK_ATTACHMENT_LOAD_OP_LOAD
	);
	this->fbo_render_pass_clear_nod = make_render_pass(
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		false,
		VK_ATTACHMENT_LOAD_OP_CLEAR
	);
	this->fbo_render_pass_load_nod = make_render_pass(
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		false,
		VK_ATTACHMENT_LOAD_OP_LOAD
	);
}

void context::create_swapchain_framebuffers()
{
	// destroy the old framebuffers (they reference the old swapchain images)
	for (auto fb : this->swapchain_framebuffers) {
		if (fb != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(this->device, fb, nullptr);
		}
	}
	this->swapchain_framebuffers.clear();
	this->swapchain_image_views.resize(this->swapchain_images.size());
	this->swapchain_framebuffers.resize(this->swapchain_images.size());

	for (uint32_t i = 0; i < this->swapchain_image_count; ++i) {
		// create image view
		{
			const auto view_info = VkImageViewCreateInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = this->swapchain_images[i],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = this->swapchain_format,
				.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
			};
			{
				VkImageView image_view{};
				check_vk_result(vkCreateImageView(this->device, &view_info, nullptr, &image_view), "vkCreateImageView");
				this->swapchain_image_views[i] = image_view;
			}
		}

		// create framebuffer (color + depth, matching the swapchain render pass)
		const auto attachments = std::array<VkImageView, 2>{
			{this->swapchain_image_views[i], this->depth_image_view}
		};
		const auto fb_info = VkFramebufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = this->swapchain_render_pass_clear, // same attachment layout for clear/load
			.attachmentCount = 2,
			.pAttachments = attachments.data(),
			.width = this->window_dims.x(),
			.height = this->window_dims.y(),
			.layers = 1
		};
		{
			VkFramebuffer fb{};
			check_vk_result(vkCreateFramebuffer(this->device, &fb_info, nullptr, &fb), "vkCreateFramebuffer");
			this->swapchain_framebuffers[i] = fb;
		}
	}
}

context::~context()
{
	vkDeviceWaitIdle(this->device);

	// destroy frame state
	if (this->frame_fence != VK_NULL_HANDLE) {
		vkDestroyFence(this->device, this->frame_fence, nullptr);
	}
	if (this->image_available_sem != VK_NULL_HANDLE) {
		vkDestroySemaphore(this->device, this->image_available_sem, nullptr);
	}
	if (this->render_finished_sem != VK_NULL_HANDLE) {
		vkDestroySemaphore(this->device, this->render_finished_sem, nullptr);
	}
	if (this->command_buffer != VK_NULL_HANDLE) {
		vkFreeCommandBuffers(this->device, this->command_pool, 1, &this->command_buffer);
	}
	if (this->command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(this->device, this->command_pool, nullptr);
	}
	// destroy pipelines
	for (auto& [key, pipeline] : this->pipelines) {
		vkDestroyPipeline(this->device, pipeline, nullptr);
	}

	if (this->descriptor_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(this->device, this->descriptor_pool, nullptr);
	}

	// free the per-draw UBO ring
	for (size_t i = 0; i < this->ubo_ring.size(); ++i) {
		if (this->ubo_ring_map[i] != nullptr) {
			vkUnmapMemory(this->device, this->ubo_ring_mem[i]);
		}
		if (this->ubo_ring[i] != VK_NULL_HANDLE) {
			vkDestroyBuffer(this->device, this->ubo_ring[i], nullptr);
		}
		if (this->ubo_ring_mem[i] != VK_NULL_HANDLE) {
			vkFreeMemory(this->device, this->ubo_ring_mem[i], nullptr);
		}
	}

	// destroy render passes
	if (this->swapchain_render_pass_clear != VK_NULL_HANDLE) {
		vkDestroyRenderPass(this->device, this->swapchain_render_pass_clear, nullptr);
	}
	if (this->swapchain_render_pass_load != VK_NULL_HANDLE) {
		vkDestroyRenderPass(this->device, this->swapchain_render_pass_load, nullptr);
	}
	if (this->fbo_render_pass_clear_depth != VK_NULL_HANDLE) {
		vkDestroyRenderPass(this->device, this->fbo_render_pass_clear_depth, nullptr);
	}
	if (this->fbo_render_pass_load_depth != VK_NULL_HANDLE) {
		vkDestroyRenderPass(this->device, this->fbo_render_pass_load_depth, nullptr);
	}
	if (this->fbo_render_pass_clear_nod != VK_NULL_HANDLE) {
		vkDestroyRenderPass(this->device, this->fbo_render_pass_clear_nod, nullptr);
	}
	if (this->fbo_render_pass_load_nod != VK_NULL_HANDLE) {
		vkDestroyRenderPass(this->device, this->fbo_render_pass_load_nod, nullptr);
	}

	// destroy swapchain framebuffers
	for (auto fb : this->swapchain_framebuffers) {
		if (fb != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(this->device, fb, nullptr);
		}
	}

	// destroy swapchain image views
	for (auto view : this->swapchain_image_views) {
		if (view != VK_NULL_HANDLE) {
			vkDestroyImageView(this->device, view, nullptr);
		}
	}

	// destroy depth image
	if (this->depth_image_view != VK_NULL_HANDLE) {
		vkDestroyImageView(this->device, this->depth_image_view, nullptr);
	}
	if (this->depth_image != VK_NULL_HANDLE) {
		vkDestroyImage(this->device, this->depth_image, nullptr);
		vkFreeMemory(this->device, this->depth_image_memory, nullptr);
	}

	// destroy swapchain (do NOT destroy swapchain images with vkDestroyImage - they are managed by the swapchain)
	if (this->swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(this->device, this->swapchain, nullptr);
	}
	this->swapchain_images.clear();
}

utki::shared_ref<ruis::render::frame_buffer> context::make_framebuffer( //
	std::shared_ptr<ruis::render::texture_2d> color,
	std::shared_ptr<ruis::render::texture_depth> depth,
	std::shared_ptr<ruis::render::texture_stencil> stencil
)
{
	return utki::make_shared<frame_buffer>(
		this->get_shared_ref(), //
		std::move(color),
		std::move(depth),
		std::move(stencil)
	);
}

// ===============================
// = state control functions =

void context::set_framebuffer_internal(ruis::render::frame_buffer* fb)
{
	// if a render pass is in progress, end it before switching the framebuffer
	if (this->render_pass_in_progress) {
		this->end_render_pass_internal();
	}

	this->current_fb = fb;
}

void context::clear_framebuffer_color()
{
	this->pending_clear = true;
}

void context::clear_framebuffer_depth()
{
	this->pending_clear = true;
}

void context::clear_framebuffer_stencil()
{
	// no stencil support, no-op
}

r4::vector2<uint32_t> context::to_window_coords(const ruis::vec2& point) const
{
	// NDC -> window space using the swapchain (physical) dimensions
	const auto p = r4::vector2<float>(point.x(), point.y());
	const auto w = this->window_dims.x();
	const auto h = this->window_dims.y();
	r4::vector2<uint32_t> result;
	result.x() = static_cast<uint32_t>((p.x() + 1.0f) * ndc_to_unit_scale * static_cast<float>(w));
	result.y() = static_cast<uint32_t>((p.y() + 1.0f) * ndc_to_unit_scale * static_cast<float>(h));
	return result;
}

bool context::is_scissor_enabled() const noexcept
{
	return this->scissor_enabled;
}

void context::enable_scissor(bool enable)
{
	this->scissor_enabled = enable;
}

r4::rectangle<uint32_t> context::get_scissor() const
{
	return this->scissor;
}

void context::set_scissor(const r4::rectangle<uint32_t>& r)
{
	this->scissor = r;
}

r4::rectangle<uint32_t> context::get_viewport() const
{
	// when rendering into a framebuffer (e.g. render-to-texture / get_render),
	// the viewport is the framebuffer's dimensions, not the window's viewport.
	if (this->current_fb != nullptr) {
		const auto d = this->current_fb->dims();
		return {0, 0, d.x(), d.y()};
	}
	// when rendering to the screen, use the swapchain (physical) dimensions
	return {0, 0, this->window_dims.x(), this->window_dims.y()};
}

void context::set_viewport(const r4::rectangle<uint32_t>& r)
{
	this->viewport = r;
}

void context::enable_blend(bool enable)
{
	this->blend_enabled = enable;
}

void context::set_blend_func(
	blend_factor src_color,
	blend_factor dst_color,
	blend_factor src_alpha,
	blend_factor dst_alpha
)
{
	this->blend_src_color = to_vk_blend_factor(src_color);
	this->blend_dst_color = to_vk_blend_factor(dst_color);
	this->blend_src_alpha = to_vk_blend_factor(src_alpha);
	this->blend_dst_alpha = to_vk_blend_factor(dst_alpha);
}

bool context::is_depth_enabled() const noexcept
{
	return this->depth_enabled;
}

void context::enable_depth(bool enable)
{
	this->depth_enabled = enable;
}

// ===============================
// = vulkan-specific interface =

uint32_t context::get_blend_key() const
{
	// pack the blend state into a 32-bit key
	auto pack = [](VkBlendFactor f) {
		return uint32_t(f);
	};
	uint32_t key = 0;
	key |= pack(this->blend_src_color) << 0;
	key |= pack(this->blend_dst_color) << blend_key_factor_bits;
	key |= pack(this->blend_src_alpha) << (blend_key_factor_bits * 2);
	key |= pack(this->blend_dst_alpha) << (blend_key_factor_bits * 3);
	key |= (this->blend_enabled ? 1u : 0u) << (blend_key_factor_bits * 4);
	return key;
}

VkRenderPass context::get_current_render_pass(bool has_depth, bool clear) const
{
	if (this->current_fb == nullptr) {
		// screen
		return clear ? this->swapchain_render_pass_clear : this->swapchain_render_pass_load;
	}
	if (has_depth) {
		return clear ? this->fbo_render_pass_clear_depth : this->fbo_render_pass_load_depth;
	}
	return clear ? this->fbo_render_pass_clear_nod : this->fbo_render_pass_load_nod;
}

void context::ensure_frame_began()
{
	if (this->frame_begun) {
		return;
	}

	// recreate the swapchain if the window was resized (must happen before rendering)
	this->ensure_swapchain_valid();

	// wait for the previous frame to complete
	{
		// only wait if a frame has been submitted before
		if (this->frame_submitted) {
			const auto wait_res =
				vkWaitForFences(this->device, 1, &this->frame_fence, VK_TRUE, 2000000000ull); // 2s timeout
			if (wait_res == VK_SUCCESS) {
				vkResetFences(this->device, 1, &this->frame_fence);
			}
			// if the wait timed out, the frame is still in progress; proceed anyway
			// (the command buffer reset will implicitly wait for the GPU)
			this->frame_submitted = false;
		}
	}

	// reset and begin the command buffer
	check_vk_result(vkResetCommandBuffer(this->command_buffer, 0), "vkResetCommandBuffer");
	const auto begin_info = VkCommandBufferBeginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	check_vk_result(vkBeginCommandBuffer(this->command_buffer, &begin_info), "vkBeginCommandBuffer");

	this->frame_begun = true;
	this->frame_has_swapchain = false;
	this->current_swapchain_image = -1;
	// reset the UBO ring index: the previous frame's GPU work is complete (we waited
	// on the frame fence above), so its UBO slots are safe to reuse
	this->ubo_ring_index = 0;
	// free any leftover descriptor sets from a skipped frame
	this->free_descriptor_sets();
	this->frame_descriptor_sets.clear();
}

void context::allocate_ubo_slot()
{
	const auto buffer_info = VkBufferCreateInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(draw_uniforms),
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VkBuffer buf = VK_NULL_HANDLE;
	{
		VkBuffer buffer{};
		check_vk_result(vkCreateBuffer(this->device, &buffer_info, nullptr, &buffer), "vkCreateBuffer");
		buf = buffer;
	}
	VkMemoryRequirements mem_reqs{};
	vkGetBufferMemoryRequirements(this->device, buf, &mem_reqs);
	const auto alloc_info = VkMemoryAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = pick_host_visible_memory_type(this->physical_device, mem_reqs)
	};
	VkDeviceMemory mem = VK_NULL_HANDLE;
	check_vk_result(vkAllocateMemory(this->device, &alloc_info, nullptr, &mem), "vkAllocateMemory");
	check_vk_result(vkBindBufferMemory(this->device, buf, mem, 0), "vkBindBufferMemory");
	void* mapped = nullptr;
	check_vk_result(vkMapMemory(this->device, mem, 0, sizeof(draw_uniforms), 0, &mapped), "vkMapMemory");
	this->ubo_ring.push_back(buf);
	this->ubo_ring_mem.push_back(mem);
	this->ubo_ring_map.push_back(mapped);
}

VkBuffer context::update_uniforms(const r4::matrix4<float>& m, const r4::vector4<float>& color)
{
	// take the next slot in the UBO ring (growing it if needed) and write the uniforms.
	// each draw gets its own slot so the GPU reads the right value at execution time.
	const size_t idx = this->ubo_ring_index++;
	if (idx >= this->ubo_ring.size()) {
		this->allocate_ubo_slot();
	}
	const size_t slot = idx < this->ubo_ring.size() ? idx : (this->ubo_ring.size() - 1);
	auto* u = static_cast<draw_uniforms*>(this->ubo_ring_map[slot]);
	write_matrix_column_major(u->matrix, m);
	u->color[0] = color.x();
	u->color[1] = color.y();
	u->color[2] = color.z();
	u->color[3] = color.w();
	return this->ubo_ring[slot];
}

VkDescriptorSet context::allocate_descriptor_set(
	bool textured,
	VkBuffer ubo_buffer,
	VkImageView image_view,
	VkSampler sampler
)
{
	const auto dsl = textured ? this->factory.get().get_dsl_with_tex() : this->factory.get().get_dsl_no_tex();

	VkDescriptorSet set = VK_NULL_HANDLE;
	const auto alloc_info = VkDescriptorSetAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = this->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &dsl
	};
	check_vk_result(vkAllocateDescriptorSets(this->device, &alloc_info, &set), "vkAllocateDescriptorSets");
	this->frame_descriptor_sets.push_back(set);

	std::vector<VkWriteDescriptorSet> writes;

	// binding 0: UBO (the per-draw slot written by update_uniforms)
	const auto buffer_info = VkDescriptorBufferInfo{.buffer = ubo_buffer, .offset = 0, .range = sizeof(draw_uniforms)};
	const auto ubo_write = VkWriteDescriptorSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pImageInfo = nullptr,
		.pBufferInfo = &buffer_info
	};
	writes.push_back(ubo_write);

	if (textured) {
		const auto image_info = VkDescriptorImageInfo{
			.sampler = sampler,
			.imageView = image_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};
		const auto tex_write = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &image_info,
			.pBufferInfo = nullptr
		};
		writes.push_back(tex_write);
	}

	vkUpdateDescriptorSets(this->device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	return set;
}

void context::draw_indexed(
	VkPipeline pipeline,
	VkPipelineLayout layout,
	const ruis::render::vertex_array& va,
	VkDescriptorSet descriptor_set
)
{
	// skip drawing if the viewport is zero-sized (e.g. resource loader context)
	const auto vp = this->get_viewport();
	if (vp.d.x() == 0 || vp.d.y() == 0) {
		return;
	}

	// if no render pass is in progress, begin one
	if (!this->render_pass_in_progress) {
		this->begin_render_pass_internal(this->depth_enabled);
	}

	vkCmdBindPipeline(this->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(
		this->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		layout,
		0,
		1,
		&descriptor_set,
		0,
		nullptr
	);

	// set the dynamic viewport and scissor.
	// In Vulkan the top-left corner of the viewport is at the top-left corner of
	// the window and the y-axis points down (positive height), which matches the
	// framework's coordinate convention (y down).
	const auto viewport_vk = VkViewport{
		.x = float(vp.p.x()),
		.y = float(vp.p.y()),
		.width = float(vp.d.x()),
		.height = float(vp.d.y()),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(this->command_buffer, 0, 1, &viewport_vk);

	// The scissor rectangle is in window-system coordinates (origin top-left, y
	// down), which is the same coordinate space as the viewport. No flip needed.
	VkRect2D scissor{};
	if (this->scissor_enabled) {
		const auto& s = this->scissor;
		scissor = VkRect2D{
			.offset = { int32_t(s.p.x()),  int32_t(s.p.y())},
			.extent = {uint32_t(s.d.x()), uint32_t(s.d.y())}
		};
	} else {
		// use the effective viewport size (framebuffer dims when rendering to a texture)
		const auto evp = this->get_viewport();
		scissor = VkRect2D{
			.offset = {        0,         0},
			.extent = {evp.d.x(), evp.d.y()}
		};
	}
	vkCmdSetScissor(this->command_buffer, 0, 1, &scissor);

	// bind the vertex buffers
	std::vector<VkBuffer> buffers;
	std::vector<VkDeviceSize> offsets;
	for (const auto& b : va.buffers) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the buffer is a vulkan vertex_buffer")
		const auto& vkb = static_cast<const vertex_buffer&>(b.get());
		buffers.push_back(vkb.buffer);
		offsets.push_back(0);
	}
	if (!buffers.empty()) {
		vkCmdBindVertexBuffers(this->command_buffer, 0, uint32_t(buffers.size()), buffers.data(), offsets.data());
	}

	// bind the index buffer and draw
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "the indices is a vulkan index_buffer")
	const auto& vk_indices = static_cast<const index_buffer&>(va.indices.get());
	vkCmdBindIndexBuffer(this->command_buffer, vk_indices.buffer, 0, vk_indices.index_type);
	vkCmdDrawIndexed(this->command_buffer, uint32_t(vk_indices.elements_count), 1, 0, 0, 0);
}

rasterimage::image_variant context::readback_framebuffer_color(const ruis::render::frame_buffer& fb)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "fb is always created by this renderer")
	const auto& vulkan_fb = static_cast<const frame_buffer&>(fb);
	const auto& color = vulkan_fb.vulkan_color();

	const auto w = uint32_t(color.dims().x());
	const auto h = uint32_t(color.dims().y());
	const size_t row_bytes = size_t(w) * 4; // RGBA8
	const VkDeviceSize total_bytes = row_bytes * h;

	// create a host-visible staging buffer for the readback
	VkBuffer staging_buffer = VK_NULL_HANDLE;
	VkDeviceMemory staging_memory = VK_NULL_HANDLE;
	{
		const auto buffer_info = VkBufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = total_bytes,
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};
		check_vk_result(vkCreateBuffer(this->device, &buffer_info, nullptr, &staging_buffer), "vkCreateBuffer");
		VkMemoryRequirements mem_reqs{};
		vkGetBufferMemoryRequirements(this->device, staging_buffer, &mem_reqs);
		VkPhysicalDeviceMemoryProperties props{};
		vkGetPhysicalDeviceMemoryProperties(this->physical_device, &props);
		VkMemoryAllocateInfo alloc_info{};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.size;
		for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
			if ((mem_reqs.memoryTypeBits & (1 << i)) &&
				(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
			{
				alloc_info.memoryTypeIndex = i;
				break;
			}
		}
		check_vk_result(vkAllocateMemory(this->device, &alloc_info, nullptr, &staging_memory), "vkAllocateMemory");
		check_vk_result(vkBindBufferMemory(this->device, staging_buffer, staging_memory, 0), "vkBindBufferMemory");
	}

	// append the readback to the frame's command buffer (the FBO render was recorded there)
	// ensure the previous frame is complete before beginning a new one
	if (this->frame_submitted) {
		const auto wait_res =
			vkWaitForFences(this->device, 1, &this->frame_fence, VK_TRUE, 2000000000ull); // 2s timeout
		if (wait_res != VK_SUCCESS) {
			// the previous frame is not complete; skip the readback
			throw std::runtime_error("readback: previous frame not complete");
		}
		vkResetFences(this->device, 1, &this->frame_fence);
		this->frame_submitted = false;
	}
	this->ensure_frame_began();
	{
		const auto image_barrier = VkImageMemoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = color.image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
		};
		vkCmdPipelineBarrier(
			this->command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&image_barrier
		);

		const auto region = VkBufferImageCopy{
			.bufferOffset = 0,
			.bufferRowLength = 0, // 0 means tightly packed (use image width in texels)
			.bufferImageHeight = h,
			.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.imageOffset = {0, 0, 0},
			.imageExtent = {w, h, 1}
		};
		vkCmdCopyImageToBuffer(
			this->command_buffer, //
			color.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			staging_buffer,
			1,
			&region
		);
	}

	// submit the readback and wait for it to complete
	{
		const auto submit_info = VkSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 0,
			.pWaitSemaphores = nullptr,
			.pWaitDstStageMask = nullptr,
			.commandBufferCount = 1,
			.pCommandBuffers = &this->command_buffer,
			.signalSemaphoreCount = 0,
			.pSignalSemaphores = nullptr
		};
		check_vk_result(vkEndCommandBuffer(this->command_buffer), "vkEndCommandBuffer");
		check_vk_result(vkQueueSubmit(this->queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");
		// wait for the GPU to finish using vkQueueWaitIdle (works on AMD driver)
		check_vk_result(vkQueueWaitIdle(this->queue), "vkQueueWaitIdle");
		this->frame_submitted = false;
		this->frame_begun = false;
		this->frame_has_swapchain = false;
		this->current_swapchain_image = -1;
		this->free_descriptor_sets();
		this->frame_descriptor_sets.clear();
	}

	// read the data back
	std::vector<uint8_t> buf(total_bytes);
	void* mapped = nullptr;
	check_vk_result(vkMapMemory(this->device, staging_memory, 0, total_bytes, 0, &mapped), "vkMapMemory");
	std::memcpy(buf.data(), mapped, total_bytes);
	vkUnmapMemory(this->device, staging_memory);

	vkDestroyBuffer(this->device, staging_buffer, nullptr);
	vkFreeMemory(this->device, staging_memory, nullptr);

	// The Vulkan color attachment is stored top-to-bottom (row 0 = top), which is
	// already the conventional top-down order, so no flip is needed.
	auto im = rasterimage::image<uint8_t, 4>::make(r4::vector2<uint32_t>(w, h), buf.data());
	return {std::move(im)};
}

void context::swap_frame_buffers()
{
	this->present();
}

void context::present()
{
	// end the render pass if in progress
	if (this->render_pass_in_progress) {
		this->end_render_pass_internal();
	}

	if (!this->frame_begun) {
		// nothing was rendered this frame; submit a clear frame so the window shows the clear color
		this->ensure_frame_began();
		this->pending_clear = true;
		this->begin_render_pass_internal(this->depth_enabled);
		this->end_render_pass_internal();
	}

	this->submit_frame(true);
}

// ===============================
// = pipeline management =

VkPipeline context::get_or_create_pipeline(
	shader_kind kind, //
	bool textured,
	VkPrimitiveTopology topology,
	const void* vertex_spirv,
	size_t vertex_spirv_size,
	const void* fragment_spirv,
	size_t fragment_spirv_size
)
{
	// determine the render pass this draw will use (the pipeline is tied to it)
	VkRenderPass render_pass = VK_NULL_HANDLE;
	if (this->render_pass_in_progress) {
		render_pass = this->active_render_pass;
	} else {
		render_pass = this->get_current_render_pass(this->depth_enabled, this->pending_clear);
	}

	const auto key = pipeline_key{
		.shader_kind = uint32_t(kind),
		.blend_key = this->get_blend_key(),
		.depth_enabled = this->depth_enabled ? 1u : 0u,
		.textured = textured ? 1u : 0u,
		.topology = uint32_t(topology),
		.render_pass = render_pass
	};

	{
		auto it = this->pipelines.find(key);
		if (it != this->pipelines.end()) {
			return it->second;
		}
	}

	const auto pipeline = this->build_pipeline(
		key,
		shader_has_a1(kind),
		vertex_spirv,
		vertex_spirv_size,
		fragment_spirv,
		fragment_spirv_size
	);
	this->pipelines[key] = pipeline;
	return pipeline;
}

VkPipeline context::build_pipeline(
	const pipeline_key& key, //
	bool has_a1,
	const void* vertex_spirv,
	size_t vertex_spirv_size,
	const void* fragment_spirv,
	size_t fragment_spirv_size
) const
{
	// ---- shader stages ----
	const auto vertex_stage = VkPipelineShaderStageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = this->create_shader_module(vertex_spirv, vertex_spirv_size),
		.pName = "main"
	};
	const auto fragment_stage = VkPipelineShaderStageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = this->create_shader_module(fragment_spirv, fragment_spirv_size),
		.pName = "main"
	};
	const auto stages = std::array<VkPipelineShaderStageCreateInfo, 2>{
		{vertex_stage, fragment_stage}
	};

	// ---- vertex input layout ----
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	bindings.push_back(VkVertexInputBindingDescription{
		.binding = 0,
		.stride = vec4_float_count * sizeof(float),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	});
	attributes.push_back(VkVertexInputAttributeDescription{
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = 0
	});
	if (has_a1) {
		bindings.push_back(VkVertexInputBindingDescription{
			.binding = 1,
			.stride = vec4_float_count * sizeof(float),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		});
		attributes.push_back(VkVertexInputAttributeDescription{
			.location = 1,
			.binding = 1,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset = 0
		});
	}
	const auto vertex_input_info = VkPipelineVertexInputStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = uint32_t(bindings.size()),
		.pVertexBindingDescriptions = bindings.data(),
		.vertexAttributeDescriptionCount = uint32_t(attributes.size()),
		.pVertexAttributeDescriptions = attributes.data()
	};

	// ---- input assembly ----
	const auto input_assembly = VkPipelineInputAssemblyStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		// the topology must match the VAO's rendering mode, which is baked into
		// the pipeline key (the VAO cannot be queried from the key)
		.topology = VkPrimitiveTopology(key.topology),
		.primitiveRestartEnable = VK_FALSE
	};

	// ---- rasterization ----
	const auto rasterization = VkPipelineRasterizationStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		// The initial_matrix does NOT flip y (the Vulkan viewport with positive
		// height maps NDC (-1,-1) to the top-left, so the framework's y-down is
		// preserved all the way to the framebuffer). This keeps the window-space
		// winding counter-clockwise for front-facing quads. frontFace defaults to
		// CCW (the Vulkan zero value), matching OpenGL, so only the cull mode
		// needs to be set: cull the back (CW) face.
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f
	};

	// ---- multiviewport ----
	const auto viewport_state = VkPipelineViewportStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};

	// ---- color blend ----
	VkPipelineColorBlendAttachmentState color_blend{};
	if (this->blend_enabled) {
		color_blend.blendEnable = VK_TRUE;
		color_blend.srcColorBlendFactor = this->blend_src_color;
		color_blend.dstColorBlendFactor = this->blend_dst_color;
		color_blend.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend.srcAlphaBlendFactor = this->blend_src_alpha;
		color_blend.dstAlphaBlendFactor = this->blend_dst_alpha;
		color_blend.alphaBlendOp = VK_BLEND_OP_ADD;
	} else {
		color_blend.blendEnable = VK_FALSE;
		color_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		color_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		color_blend.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		color_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		color_blend.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	color_blend.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	const auto color_blend_state = VkPipelineColorBlendStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &color_blend,
		.blendConstants = {1.0f, 1.0f, 1.0f, 1.0f}
	};

	// ---- depth / stencil ----
	const auto depth_stencil = VkPipelineDepthStencilStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = this->depth_enabled ? VK_TRUE : VK_FALSE,
		.depthWriteEnable = this->depth_enabled ? VK_TRUE : VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE
	};

	// ---- dynamic state ----
	const auto dynamic_states = std::array<VkDynamicState, 2>{
		{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}
	};
	const auto dynamic_state = VkPipelineDynamicStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states.data()
	};

	// ---- layout ----
	const auto layout = key.textured ? this->factory.get().get_pipeline_layout_with_tex()
									 : this->factory.get().get_pipeline_layout_no_tex();

	// ---- multisample state ----
	const auto multisample = VkPipelineMultisampleStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	// ---- pipeline ----
	const auto info = VkGraphicsPipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages.data(),
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &input_assembly,
		.pTessellationState = nullptr,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend_state,
		.pDynamicState = &dynamic_state,
		.layout = layout,
		.renderPass = key.render_pass,
		.subpass = 0
	};

	VkPipeline pipeline = VK_NULL_HANDLE;
	check_vk_result(
		vkCreateGraphicsPipelines(this->device, this->factory.get().get_pipeline_cache(), 1, &info, nullptr, &pipeline),
		"vkCreateGraphicsPipelines"
	);

	// destroy the shader modules (they are now baked into the pipeline)
	vkDestroyShaderModule(this->device, vertex_stage.module, nullptr);
	vkDestroyShaderModule(this->device, fragment_stage.module, nullptr);

	return pipeline;
}

// ===============================
// = render pass management =

void context::begin_render_pass_internal(bool has_depth)
{
	// the swapchain render passes always have a depth attachment, so force depth when rendering to screen
	if (this->current_fb == nullptr) {
		has_depth = true;
	}
	// determine the render pass (clear or load)
	const bool clear = this->pending_clear;
	const auto render_pass = this->get_current_render_pass(has_depth, clear);

	// acquire the swapchain image if we're rendering to the screen
	VkImageView color_view = VK_NULL_HANDLE;
	VkDeviceSize width = 0;
	VkDeviceSize height = 0;

	if (this->current_fb == nullptr) {
		// screen
		if (!this->frame_has_swapchain) {
			uint32_t index = 0;
			const auto res = vkAcquireNextImageKHR(
				this->device, //
				this->swapchain,
				2000000000ull, // 2s timeout
				this->image_available_sem,
				VK_NULL_HANDLE,
				&index
			);
			if (res != VK_SUCCESS && res != VK_TIMEOUT && res != VK_SUBOPTIMAL_KHR) {
				throw std::runtime_error("vkAcquireNextImageKHR failed: " + std::to_string(int(res)));
			}
			if (res == VK_TIMEOUT) {
				this->frame_begun = false;
				this->render_pass_in_progress = false;
				this->active_render_pass = VK_NULL_HANDLE;
				this->active_has_depth = false;
				this->pending_clear = false;
				return;
			}
			this->current_swapchain_image = int(index);
			this->frame_has_swapchain = true;
		}
		color_view = this->swapchain_image_views[uint32_t(this->current_swapchain_image)];
		width = this->window_dims.x();
		height = this->window_dims.y();
	} else {
		// FBO
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "current_fb is always created by this renderer")
		const auto& vulkan_fb = static_cast<const frame_buffer&>(*this->current_fb);
		color_view = vulkan_fb.vulkan_color().image_view;
		width = uint32_t(vulkan_fb.vulkan_color().dims().x());
		height = uint32_t(vulkan_fb.vulkan_color().dims().y());
	}

	// set up the render pass begin info
	std::vector<VkImageView> attachments;
	attachments.push_back(color_view);
	VkImageView depth_view = VK_NULL_HANDLE;
	if (has_depth) {
		if (this->current_fb == nullptr) {
			depth_view = this->depth_image_view;
		} else {
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "current_fb is always created by this renderer")
			const auto* vulkan_depth = static_cast<const frame_buffer&>(*this->current_fb).vulkan_depth();
			depth_view = vulkan_depth ? vulkan_depth->image_view : VK_NULL_HANDLE;
		}
		attachments.push_back(depth_view);
	}

	// clear values: color at attachment 0, depth at attachment 1.
	// always clear to transparent: for FBOs this ensures unclosed areas
	// don't cover the background; for the screen the swapchain alpha is
	// ignored by the compositor so it still appears as black.
	std::vector<VkClearValue> ordered_clear_values;
	ordered_clear_values.push_back(VkClearValue{.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}});
	if (has_depth) {
		ordered_clear_values.push_back(VkClearValue{
			.depthStencil = {1.0f, 0}
		});
	}

	const auto render_area = VkRect2D{
		.offset = {              0,                0},
		.extent = {uint32_t(width), uint32_t(height)}
	};

	// determine the framebuffer handle
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	if (this->current_fb == nullptr) {
		if (this->current_swapchain_image < 0 ||
			this->current_swapchain_image >= int(this->swapchain_framebuffers.size()))
		{
			throw std::runtime_error(
				"begin_render_pass_internal: invalid swapchain image index " +
				std::to_string(this->current_swapchain_image)
			);
		}
		framebuffer = this->swapchain_framebuffers[uint32_t(this->current_swapchain_image)];
	} else {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "current_fb is always created by this renderer")
		const auto& vulkan_fb = static_cast<const frame_buffer&>(*this->current_fb);
		framebuffer = vulkan_fb.vulkan_framebuffer();
	}

	const auto begin_info = VkRenderPassBeginInfo{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = render_pass,
		.framebuffer = framebuffer,
		.renderArea = render_area,
		.clearValueCount = uint32_t(ordered_clear_values.size()),
		.pClearValues = ordered_clear_values.data()
	};

	// NOTE: do NOT use pipeline barriers before vkCmdBeginRenderPass.
	// The render pass handles layout transitions via initialLayout/finalLayout.
	// Issuing barriers here conflicts with the render pass's expected initialLayout.

	vkCmdBeginRenderPass(this->command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	this->render_pass_in_progress = true;
	this->active_render_pass = render_pass;
	this->active_has_depth = has_depth;
	this->pending_clear = false;
}

void context::end_render_pass_internal()
{
	if (!this->render_pass_in_progress) {
		return;
	}

	vkCmdEndRenderPass(this->command_buffer);
	this->render_pass_in_progress = false;
}

void context::submit_frame(bool present)
{
	if (!this->frame_begun) {
		// no frame was begun (e.g. swapchain image acquisition failed), nothing to submit
		return;
	}
	// end the command buffer
	{
		check_vk_result(vkEndCommandBuffer(this->command_buffer), "vkEndCommandBuffer");
	}

	// build the submit info
	std::vector<VkSemaphore> wait_sems;
	std::vector<VkPipelineStageFlags> wait_stages;
	if (this->frame_has_swapchain) {
		wait_sems.push_back(this->image_available_sem);
		wait_stages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	}

	const auto submit_info = VkSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = uint32_t(wait_sems.size()),
		.pWaitSemaphores = wait_sems.data(),
		.pWaitDstStageMask = wait_stages.data(),
		.commandBufferCount = 1,
		.pCommandBuffers = &this->command_buffer,
		.signalSemaphoreCount = present ? 1u : 0u,
		.pSignalSemaphores = present ? &this->render_finished_sem : nullptr
	};
	check_vk_result(vkQueueSubmit(this->queue, 1, &submit_info, this->frame_fence), "vkQueueSubmit");
	this->frame_submitted = true;

	if (present) {
		// present the swapchain image
		const auto present_image_index = uint32_t(this->current_swapchain_image);
		const auto present_info = VkPresentInfoKHR{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &this->render_finished_sem,
			.swapchainCount = 1,
			.pSwapchains = &this->swapchain,
			.pImageIndices = &present_image_index,
			.pResults = nullptr
		};
		const auto pres_res = vkQueuePresentKHR(this->queue, &present_info);
		if (pres_res == VK_SUBOPTIMAL_KHR) {
			// the swapchain is no longer optimal; it will be recreated on the next frame
			this->swapchain_suboptimal = true;
		}
	}

	// wait for the frame to complete using vkQueueWaitIdle (safest for AMD driver)
	{
		check_vk_result(vkQueueWaitIdle(this->queue), "vkQueueWaitIdle");
		check_vk_result(
			vkWaitForFences(this->device, 1, &this->frame_fence, VK_TRUE, 1000000000ull),
			"vkWaitForFences"
		); // 1s timeout
		vkResetFences(this->device, 1, &this->frame_fence);
		this->frame_submitted = false;
	}

	// free the descriptor sets allocated this frame
	this->free_descriptor_sets();
	this->frame_descriptor_sets.clear();

	// reset the frame state
	this->frame_begun = false;
	this->frame_has_swapchain = false;
	this->current_swapchain_image = -1;
	this->render_pass_in_progress = false;
	this->active_render_pass = VK_NULL_HANDLE;
	this->active_has_depth = false;
	this->pending_clear = false;
}

void context::free_descriptor_sets()
{
	if (this->frame_descriptor_sets.empty()) {
		return;
	}
	vkFreeDescriptorSets(
		this->device,
		this->descriptor_pool,
		uint32_t(this->frame_descriptor_sets.size()),
		this->frame_descriptor_sets.data()
	);
}

VkShaderModule context::create_shader_module(const void* spirv, size_t size_words) const
{
	const auto* words = static_cast<const uint32_t*>(spirv);
	const auto info = VkShaderModuleCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size_words * sizeof(uint32_t),
		.pCode = words
	};
	{
		VkShaderModule shader_module{};
		check_vk_result(vkCreateShaderModule(this->device, &info, nullptr, &shader_module), "vkCreateShaderModule");
		return shader_module;
	}
}
