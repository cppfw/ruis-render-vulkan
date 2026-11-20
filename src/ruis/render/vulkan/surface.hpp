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

#include <vulkan/vulkan.h>

namespace ruis::render::vulkan {

// forward declaration
class vulkan_device;

/**
 * @brief Provides the Vulkan resources the renderer needs for a single window.
 *
 * A single vulkan_device (instance, physical device, logical device and
 * graphics queue) is shared across all windows of an application. Sharing one
 * logical device lets GPU objects created for one window (e.g. the common
 * white texture and quad vertex buffers) be used by any other window.
 *
 * The per-window VkSurfaceKHR is created by the platform layer (e.g. via the
 * VK_KHR_xlib_surface or VK_KHR_wayland_surface extension). This surface
 * provider holds a reference to the shared vulkan_device and the window's
 * surface. It owns the surface (destroying it in the destructor).
 */
class surface
{
public:
	surface(const vulkan_device& device, VkSurfaceKHR surface);
	~surface();

	surface(const surface&) = delete;
	surface& operator=(const surface&) = delete;
	surface(surface&&) = delete;
	surface& operator=(surface&&) = delete;

	/**
	 * @brief The shared Vulkan instance.
	 */
	VkInstance get_instance() const;

	/**
	 * @brief The shared physical device.
	 */
	VkPhysicalDevice get_physical_device() const;

	/**
	 * @brief The shared logical device.
	 */
	VkDevice get_device() const;

	/**
	 * @brief The shared graphics queue.
	 */
	VkQueue get_queue() const;

	/**
	 * @brief The platform surface associated with this window.
	 * May be VK_NULL_HANDLE for headless contexts (e.g. resource loading).
	 */
	VkSurfaceKHR get_surface() const
	{
		return this->vk_surface;
	}

	/**
	 * @brief True if this provider has a valid platform surface.
	 */
	bool has_surface() const
	{
		return this->vk_surface != VK_NULL_HANDLE;
	}

private:
	const vulkan_device* device;
	VkSurfaceKHR vk_surface;
};

} // namespace ruis::render::vulkan
