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

#include <stdexcept>
#include <string>
#include <vector>

#include <r4/vector.hpp>
#include <rasterimage/image_variant.hpp>
#include <ruis/render/context.hpp>
#include <utki/debug.hpp>
#include <vulkan/vulkan.h>

namespace ruis::render::vulkan {

inline void vk_error(VkResult res, const char* where)
{
	throw std::runtime_error(std::string("vulkan error (") + std::to_string(int(res)) + ") at " + where);
}

#define VU_CHECK(x) \
	do { \
		auto vu_check_res = (x); \
		if (vu_check_res != VK_SUCCESS) { \
			ruis::render::vulkan::vk_error(vu_check_res, #x); \
		} \
	} while (false)

// helper macro: call vkCreate<TypeName> and check the result
// Usage: VU_CREATE(VkFence, device, &info)
#define VU_CREATE(type_name, ...) \
	{ \
		type_name _vu_h{}; \
		VU_CHECK(vkCreate##type_name(__VA_ARGS__, nullptr, &_vu_h)); \
		_vu_h \
	}

// converts a ruis render blend factor to a VkBlendFactor
inline VkBlendFactor to_vk_blend_factor(ruis::render::context::blend_factor f)
{
	using bf = ruis::render::context::blend_factor;
	static constexpr VkBlendFactor table[] = {
		VK_BLEND_FACTOR_ZERO, // zero
		VK_BLEND_FACTOR_ONE, // one
		VK_BLEND_FACTOR_SRC_COLOR, // src_color
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // one_minus_src_color
		VK_BLEND_FACTOR_DST_COLOR, // dst_color
		VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, // one_minus_dst_color
		VK_BLEND_FACTOR_SRC_ALPHA, // src_alpha
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // one_minus_src_alpha
		VK_BLEND_FACTOR_DST_ALPHA, // dst_alpha
		VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, // one_minus_dst_alpha
		VK_BLEND_FACTOR_CONSTANT_COLOR, // constant_color
		VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, // one_minus_constant_color
		VK_BLEND_FACTOR_CONSTANT_ALPHA, // constant_alpha
		VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA, // one_minus_constant_alpha
		VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, // src_alpha_saturate
	};
	static_assert(sizeof(table) / sizeof(table[0]) == size_t(bf::enum_size));
	return table[size_t(f)];
}

/**
 * @brief Maps a rasterimage pixel format to the corresponding 8-bit Vulkan format.
 * Returns VK_FORMAT_UNDEFINED if the format cannot be represented.
 */
inline VkFormat to_vk_format_8bit(rasterimage::format f)
{
	// Use only formats that are universally supported.
	// R8G8B8_UNORM is not supported on some AMD drivers, so map everything to RGBA.
	switch (f) {
		case rasterimage::format::grey:
		case rasterimage::format::greya:
		case rasterimage::format::rgb:
		case rasterimage::format::rgba:
			return VK_FORMAT_R8G8B8A8_UNORM;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

// number of components per pixel for a given 8-bit format
inline size_t vk_format_component_count(VkFormat f)
{
	switch (f) {
		case VK_FORMAT_R8_UNORM:
			return 1;
		case VK_FORMAT_R8G8_UNORM:
			return 2;
		case VK_FORMAT_R8G8B8_UNORM:
			return 3;
		case VK_FORMAT_R8G8B8A8_UNORM:
			return 4;
		default:
			throw std::logic_error("unhandled vk format in vk_format_component_count()");
	}
}

/**
 * @brief Layout of the per-draw uniform buffer shared by all shaders.
 *
 * The matrix is written in column-major order (as required by the GLSL std140
 * layout), the color is the per-draw tint.
 */
// pick a device-local memory type index satisfying the given requirements
inline uint32_t pick_device_local_memory_type(VkPhysicalDevice phys, const VkMemoryRequirements& mem_reqs)
{
	VkPhysicalDeviceMemoryProperties props{};
	vkGetPhysicalDeviceMemoryProperties(phys, &props);
	for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
		if ((mem_reqs.memoryTypeBits & (1 << i)) &&
			(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			return i;
		}
	}
	throw std::runtime_error("no suitable device-local memory type found");
}

// pick a memory type that is both device-local and host-visible (for mappable buffers)
inline uint32_t pick_host_visible_memory_type(VkPhysicalDevice phys, const VkMemoryRequirements& mem_reqs)
{
	VkPhysicalDeviceMemoryProperties props{};
	vkGetPhysicalDeviceMemoryProperties(phys, &props);
	// first try: device-local + host-visible
	for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
		if ((mem_reqs.memoryTypeBits & (1 << i)) &&
			(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
			(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			return i;
		}
	}
	// fallback: host-visible only
	for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
		if ((mem_reqs.memoryTypeBits & (1 << i)) &&
			(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			return i;
		}
	}
	throw std::runtime_error("no suitable host-visible memory type found");
}

struct draw_uniforms {
	float matrix[16];
	float color[4];
};

static_assert(sizeof(draw_uniforms) == 80, "draw_uniforms size mismatch");

/**
 * @brief Convert a row-major r4 4x4 matrix (as stored by r4::matrix4) to the
 * column-major layout expected by a GLSL mat4 uniform.
 */
inline void write_matrix_column_major(float out[16], const r4::matrix4<float>& m)
{
	// r4 stores the matrix row-major: m(r, c) at index r*4 + c
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			// column-major: element (r, c) goes to index c*4 + r
			out[c * 4 + r] = m[r][c];
		}
	}
}

} // namespace ruis::render::vulkan
