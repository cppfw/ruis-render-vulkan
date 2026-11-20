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

#include <functional>
#include <vector>

#include <rasterimage/image_variant.hpp>
#include <ruis/render/factory.hpp>
#include <vulkan/vulkan.h>

#include "surface.hpp"
#include "vulkan.hpp"

namespace ruis::render::vulkan {

// the vulkan derived classes intentionally share names with the ruis::render base classes
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class texture_2d;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class texture_depth;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class texture_cube;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class vertex_array;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class vertex_buffer;
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace, "derived class shadows the ruis::render base class by design")
class index_buffer;

/**
 * @brief Vulkan render factory.
 *
 * Owns the shared Vulkan resources (the logical device, the one-shot command
 * pool used for uploads, the shared pipeline/descriptor set layouts and the
 * pipeline cache) and is the only place that creates the shareable rendering
 * objects (textures, vertex/index buffers, vertex arrays, shaders).
 *
 * A single factory is shared across all windows of an application: it wraps the
 * shared @ref device (via the surface) so that GPU objects created
 * for one window (e.g. the common white texture and quad vertex buffers) can be
 * used by any other window.
 */
class factory : public ruis::render::factory
{
public:
	static constexpr uint32_t vertex_component_count = 4;

	// ---- shared Vulkan handles (public: used by derived GPU resource classes) ----
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;

	factory(const utki::shared_ref<const surface>& surface);
	~factory() override;

	// noncopyable: owns the shared GPU resources
	factory(const factory&) = delete;
	factory& operator=(const factory&) = delete;
	factory(factory&&) = delete;
	factory& operator=(factory&&) = delete;

	// ---- shared pipeline / descriptor resources (used by the per-window context) ----
	VkPipelineLayout get_pipeline_layout_no_tex() const
	{
		return this->pipeline_layout_no_tex;
	}

	VkPipelineLayout get_pipeline_layout_with_tex() const
	{
		return this->pipeline_layout_with_tex;
	}

	VkDescriptorSetLayout get_dsl_no_tex() const
	{
		return this->dsl_no_tex;
	}

	VkDescriptorSetLayout get_dsl_with_tex() const
	{
		return this->dsl_with_tex;
	}

	VkPipelineCache get_pipeline_cache() const
	{
		return this->pipeline_cache;
	}

	// ===============================
	// ====== factory functions ======

	utki::shared_ref<shaders> make_shaders() const override;

	utki::shared_ref<ruis::render::texture_2d> make_texture_2d(
		rasterimage::format format,
		rasterimage::dimensioned::dimensions_type dims,
		texture_2d_parameters params
	) const override;

	utki::shared_ref<ruis::render::texture_2d> make_texture_2d(
		const rasterimage::image_variant& imvar,
		texture_2d_parameters params
	) const override;

	utki::shared_ref<ruis::render::texture_2d> make_texture_2d(
		rasterimage::image_variant&& imvar,
		texture_2d_parameters params
	) const override;

	utki::shared_ref<ruis::render::texture_depth> make_texture_depth( //
		rasterimage::dimensioned::dimensions_type dims
	) const override;

	utki::shared_ref<ruis::render::texture_cube> make_texture_cube(
		rasterimage::image_variant&& positive_x,
		rasterimage::image_variant&& negative_x,
		rasterimage::image_variant&& positive_y,
		rasterimage::image_variant&& negative_y,
		rasterimage::image_variant&& positive_z,
		rasterimage::image_variant&& negative_z
	) const override;

	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const r4::vector4<float>> vertices
	) const override;
	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const r4::vector3<float>> vertices
	) const override;
	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const r4::vector2<float>> vertices
	) const override;
	utki::shared_ref<ruis::render::vertex_buffer> make_vertex_buffer( //
		utki::span<const float> vertices
	) const override;

	utki::shared_ref<ruis::render::index_buffer> make_index_buffer( //
		utki::span<const uint16_t> indices
	) const override;
	utki::shared_ref<ruis::render::index_buffer> make_index_buffer( //
		utki::span<const uint32_t> indices
	) const override;

	utki::shared_ref<ruis::render::vertex_array> make_vertex_array(
		std::vector<utki::shared_ref<const ruis::render::vertex_buffer>> buffers, //
		utki::shared_ref<const ruis::render::index_buffer> indices,
		ruis::render::vertex_array::mode rendering_mode
	) const override;

	// ===============================
	// = vulkan-specific interface =

	// run a one-shot command buffer (used for texture uploads); blocks until complete.
	// const: it only submits GPU work, it does not mutate the factory's C++ state.
	void one_shot(std::function<void(VkCommandBuffer)> cmd) const;

	// upload all deferred textures to the GPU (idempotent). const: it only mutates
	// the texture objects (their upload state), not the factory's C++ state.
	void ensure_textures_uploaded() const;

	// register / unregister a texture with the factory's deferred-upload tracking.
	// const: they only mutate the (mutable) tracking list, not the factory's C++ state.
	void register_texture(texture_2d* t) const
	{
		this->all_textures.push_back(t);
	}

	void unregister_texture(texture_2d* t) const;

private:
	// ---- texture tracking for deferred upload (mutable: guarded by const register/unregister) ----
	mutable std::vector<texture_2d*> all_textures;

	// ---- one-shot command submission (texture uploads) ----
	VkCommandPool one_shot_pool = VK_NULL_HANDLE;
	VkCommandBuffer one_shot_command_buffer = VK_NULL_HANDLE;
	VkFence one_shot_fence = VK_NULL_HANDLE;

	// ---- shared descriptor / pipeline layout ----
	VkPipelineLayout pipeline_layout_no_tex = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout_with_tex = VK_NULL_HANDLE;
	VkDescriptorSetLayout dsl_no_tex = VK_NULL_HANDLE; // binding 0: UBO
	VkDescriptorSetLayout dsl_with_tex = VK_NULL_HANDLE; // binding 0: UBO, binding 1: sampled image
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
};

} // namespace ruis::render::vulkan
