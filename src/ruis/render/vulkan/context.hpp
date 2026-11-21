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

#include <map>
#include <vector>

#include <rasterimage/image_variant.hpp>
#include <ruis/render/context.hpp>
#include <ruis/render/frame_buffer.hpp>
#include <utki/shared.hpp>

#include "shaders/shader_common.hpp"

#include "factory.hpp"
#include "surface.hpp"
#include "vulkan.hpp"

namespace ruis::render::vulkan {

// a key identifying a unique VkPipeline. The pipeline is independent of the
// particular vertex array, since the vertex input layout depends only on the
// number of attributes (determined by the shader) and the actual VkBuffer
// handles are bound at draw time.
struct pipeline_key {
	uint32_t shader_kind = 0;
	uint32_t blend_key = 0;
	uint32_t depth_enabled = 0;
	uint32_t textured = 0;
	uint32_t topology = 0; // VkPrimitiveTopology
	VkRenderPass render_pass = VK_NULL_HANDLE;

	bool operator==(const pipeline_key& o) const
	{
		return this->shader_kind == o.shader_kind && this->blend_key == o.blend_key &&
			this->depth_enabled == o.depth_enabled && this->textured == o.textured && this->topology == o.topology &&
			this->render_pass == o.render_pass;
	}
};

// boost-style hash combine
inline void hash_combine(size_t& h, size_t v)
{
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers, "boost hash_combine constants")
	h ^= v + 0x9e3779b97f4a7c15 + (h << 6) + (h >> 2);
}

struct pipeline_key_hash {
	size_t operator()(const pipeline_key& k) const noexcept
	{
		size_t h = 0;
		hash_combine(h, std::hash<uint32_t>()(k.shader_kind));
		hash_combine(h, std::hash<uint32_t>()(k.blend_key));
		hash_combine(h, std::hash<uint32_t>()(k.depth_enabled));
		hash_combine(h, std::hash<uint32_t>()(k.textured));
		hash_combine(h, std::hash<uint32_t>()(k.topology));
		hash_combine(
			h,
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, "Vulkan handles are opaque 64-bit values")
			std::hash<uint64_t>()(reinterpret_cast<uint64_t>(k.render_pass))
		);
		return h;
	}
};

class texture_2d;

class context : public ruis::render::context
{
public:
	context(
		utki::shared_ref<ruis::render::native_window> native_window, //
		utki::shared_ref<const surface> surface, //
		const utki::shared_ref<const ruis::render::vulkan::factory>& factory
	);
	~context() override;

	// noncopyable: owns the GPU resources of a single window
	context(const context&) = delete;
	context& operator=(const context&) = delete;
	context(context&&) = delete;
	context& operator=(context&&) = delete;

	// ===============================
	// ====== factory functions ======
	// NOTE: the shareable render objects (textures, buffers, vertex arrays, shaders)
	// are created by the injected render factory (see factory()). The only object a
	// context creates is the (per-window) frame buffer.

	utki::shared_ref<ruis::render::frame_buffer> make_framebuffer( //
		std::shared_ptr<ruis::render::texture_2d> color,
		std::shared_ptr<ruis::render::texture_depth> depth,
		std::shared_ptr<ruis::render::texture_stencil> stencil
	) override;

	// =====================================
	// ====== state control functions ======

	void set_framebuffer_internal(ruis::render::frame_buffer* fb) override;

	void clear_framebuffer_color() override;
	void clear_framebuffer_depth() override;
	void clear_framebuffer_stencil() override;

	r4::vector2<uint32_t> to_window_coords(const ruis::vec2& point) const override;

	bool is_scissor_enabled() const noexcept override;
	void enable_scissor(bool enable) override;
	r4::rectangle<uint32_t> get_scissor() const override;
	void set_scissor(const r4::rectangle<uint32_t>& r) override;

	r4::rectangle<uint32_t> get_viewport() const override;
	void set_viewport(const r4::rectangle<uint32_t>& r) override;

	void enable_blend(bool enable) override;
	void set_blend_func(blend_factor src_color, blend_factor dst_color, blend_factor src_alpha, blend_factor dst_alpha)
		override;

	bool is_depth_enabled() const noexcept override;
	void enable_depth(bool enable) override;

	void swap_frame_buffers() override;

	// ===============================
	// = vulkan-specific interface =

	// present the current frame
	void present();

	// the packed key of the current blend state (for pipeline selection)
	uint32_t get_blend_key() const;

	// the render pass matching the current framebuffer and clear state
	VkRenderPass get_current_render_pass(bool has_depth, bool clear) const;

	// get or create a pipeline for the given shader + current state
	VkPipeline get_or_create_pipeline(
		shader_kind kind, //
		bool textured,
		VkPrimitiveTopology topology,
		const void* vertex_spirv,
		size_t vertex_spirv_size,
		const void* fragment_spirv,
		size_t fragment_spirv_size
	);

	// begin a frame (begin the command buffer). Idempotent within a frame.
	void ensure_frame_began();

	// update the per-draw uniform buffer with the current draw's uniforms (matrix + color).
	// Returns the VkBuffer that was written; pass it to allocate_descriptor_set().
	VkBuffer update_uniforms(const r4::matrix4<float>& m, const r4::vector4<float>& color);

	// allocate a descriptor set for the current draw (the given UBO + optional texture image)
	VkDescriptorSet allocate_descriptor_set(
		bool textured,
		VkBuffer ubo_buffer,
		VkImageView image_view,
		VkSampler sampler
	);

	// bind the pipeline, descriptor set and vertex/index buffers and issue an indexed draw
	void draw_indexed(
		VkPipeline pipeline,
		VkPipelineLayout layout,
		const ruis::render::vertex_array& va,
		VkDescriptorSet descriptor_set
	);

	// read back the color attachment of the given frame buffer to CPU RAM (screenshot)
	rasterimage::image_variant readback_framebuffer_color(const ruis::render::frame_buffer& fb);

public:
	// used by frame_buffer to read back its color pixels
	friend class frame_buffer;

	// the render factory (shared across all windows): provides the device, the
	// one-shot upload path, and the shared pipeline / descriptor set layouts
	const utki::shared_ref<const ruis::render::vulkan::factory> factory;

	// ---- vulkan core (used internally by this per-window context) ----
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;

private:
	// keep the surface provider alive for the lifetime of the context
	// (the surface must outlive the swapchain)
	utki::shared_ref<const surface> surface_ref;

	VkInstance instance = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties device_properties{};
	uint32_t graphics_queue_family = 0;

	// ---- swapchain ----
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
	uint32_t swapchain_image_count = 0;
	r4::vector2<uint32_t> window_dims{0, 0};
	bool swapchain_suboptimal = false; // set when a present returns VK_SUBOPTIMAL_KHR
	bool swapchain_vsync = false; // vsync state used when swapchain was created

	// depth image used by the swapchain render passes
	VkImage depth_image = VK_NULL_HANDLE;
	VkImageView depth_image_view = VK_NULL_HANDLE;
	VkDeviceMemory depth_image_memory = VK_NULL_HANDLE;

	// ---- render passes ----
	VkRenderPass swapchain_render_pass_clear = VK_NULL_HANDLE; // loadOp = CLEAR
	VkRenderPass swapchain_render_pass_load = VK_NULL_HANDLE; // loadOp = LOAD
	VkRenderPass fbo_render_pass_clear_depth = VK_NULL_HANDLE;
	VkRenderPass fbo_render_pass_load_depth = VK_NULL_HANDLE;
	VkRenderPass fbo_render_pass_clear_nod = VK_NULL_HANDLE;
	VkRenderPass fbo_render_pass_load_nod = VK_NULL_HANDLE;

	// ---- command submission ----
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkFence frame_fence = VK_NULL_HANDLE;
	bool frame_submitted = false; // tracks whether a frame has been submitted (fence is pending)
	VkSemaphore image_available_sem = VK_NULL_HANDLE;
	VkSemaphore render_finished_sem = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	// ---- per-draw uniform buffer ring ----
	// each draw in a frame needs its own UBO, because the GPU reads the UBO contents at
	// execution time (after the whole frame has been recorded), so a single shared UBO
	// would be read by every draw with only the last written value.
	std::vector<VkBuffer> ubo_ring;
	std::vector<VkDeviceMemory> ubo_ring_mem;
	std::vector<void*> ubo_ring_map; // kept mapped for fast per-draw writes
	size_t ubo_ring_index = 0; // reset at the start of each frame

	// DEBUG: 0 = screen (swapchain), 1 = offscreen framebuffer

private:
	// ---- cached pipelines ----
	std::unordered_map<pipeline_key, VkPipeline, pipeline_key_hash> pipelines;

	// ---- current frame state ----
	bool frame_begun = false;
	bool render_pass_in_progress = false;
	int current_swapchain_image = -1;
	bool pending_clear = false; // the next render pass should clear the attachments
	// descriptor sets allocated this frame, freed at the end of the frame
	std::vector<VkDescriptorSet> frame_descriptor_sets;
	// true if the current frame has acquired a swapchain image (i.e. rendered to screen)
	bool frame_has_swapchain = false;
	// the render pass of the in-progress render pass (the pipeline must match it)
	VkRenderPass active_render_pass = VK_NULL_HANDLE;
	bool active_has_depth = false;
	// cached image views for the swapchain images
	std::vector<VkImageView> swapchain_image_views;
	// framebuffers for the swapchain images (one per image)
	std::vector<VkFramebuffer> swapchain_framebuffers;

	// ---- renderer state ----
	ruis::render::frame_buffer* current_fb = nullptr; // the current framebuffer (nullptr = screen)
	r4::rectangle<uint32_t> viewport{0, 0, 0, 0};
	r4::rectangle<uint32_t> scissor{0, 0, 0, 0};
	bool scissor_enabled = false;
	bool blend_enabled = false;
	VkBlendFactor blend_src_color = VK_BLEND_FACTOR_ONE;
	VkBlendFactor blend_dst_color = VK_BLEND_FACTOR_ZERO;
	VkBlendFactor blend_src_alpha = VK_BLEND_FACTOR_ONE;
	VkBlendFactor blend_dst_alpha = VK_BLEND_FACTOR_ZERO;
	bool depth_enabled = false;

	// ---- helpers ----
	void create_swapchain();
	void create_render_passes();
	void create_depth_image();
	void create_swapchain_framebuffers();
	// recreate the swapchain + depth image + framebuffers if the window was resized
	void ensure_swapchain_valid();
	VkPipeline build_pipeline(
		const pipeline_key& key, //
		bool has_a1,
		const void* vertex_spirv,
		size_t vertex_spirv_size,
		const void* fragment_spirv,
		size_t fragment_spirv_size
	) const;
	void begin_render_pass_internal(bool has_depth);
	void end_render_pass_internal();
	void submit_frame(bool present);
	void free_descriptor_sets();
	// grow the UBO ring by one slot (buffer + memory + persistent mapping)
	void allocate_ubo_slot();
	VkShaderModule create_shader_module(const void* spirv, size_t size_words) const;
};

} // namespace ruis::render::vulkan
