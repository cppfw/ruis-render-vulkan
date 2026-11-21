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

#include "surface.hpp"

#include "device.hpp"

namespace ruis::render::vulkan {

surface::surface(const device& device, VkSurfaceKHR surface) :
	vu_device(&device),
	vk_surface(surface)
{}

surface::~surface()
{
	if (this->vk_surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(this->vu_device->get_instance(), this->vk_surface, nullptr);
		this->vk_surface = VK_NULL_HANDLE;
	}
}

VkInstance surface::get_instance() const
{
	return this->vu_device->get_instance();
}

VkPhysicalDevice surface::get_physical_device() const
{
	return this->vu_device->get_physical_device();
}

VkDevice surface::get_device() const
{
	return this->vu_device->get_device();
}

VkQueue surface::get_queue() const
{
	return this->vu_device->get_queue();
}

} // namespace ruis::render::vulkan
