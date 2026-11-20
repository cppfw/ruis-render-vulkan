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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "vulkan.hpp"

namespace ruis::render::vulkan {

/**
 * @brief Owns the Vulkan instance, physical device and logical device.
 *
 * A single vulkan_device is shared across all windows of an application.
 * Sharing one logical device lets GPU objects created for one window (e.g. the
 * common white texture and quad vertex buffers) be used by any other window.
 *
 * The platform layer provides the instance extensions (e.g. VK_KHR_xlib_surface
 * or VK_KHR_wayland_surface) and the device extensions (e.g. VK_KHR_swapchain).
 *
 * The vulkan_device must outlive all surfaces that reference it.
 */
class vulkan_device
{
public:
	~vulkan_device();

	vulkan_device(const vulkan_device&) = delete;
	vulkan_device& operator=(const vulkan_device&) = delete;
	vulkan_device(vulkan_device&&) = delete;
	vulkan_device& operator=(vulkan_device&&) = delete;

	// create the Vulkan device
	vulkan_device(std::vector<std::string> instance_extensions, std::vector<std::string> device_extensions);

	VkInstance get_instance() const
	{
		return this->instance;
	}

	VkPhysicalDevice get_physical_device() const
	{
		return this->physical_device;
	}

	VkDevice get_device() const
	{
		return this->device;
	}

	VkQueue get_queue() const
	{
		return this->queue;
	}

	uint32_t get_graphics_queue_family() const
	{
		return this->graphics_queue_family;
	}

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t graphics_queue_family = 0;
	bool owns_instance = false;
};

} // namespace ruis::render::vulkan
