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

#include "surface_provider.hpp"

#include "vulkan_device.hpp"

namespace ruis::render::vulkan {

surface_provider::surface_provider(const vulkan_device& device, VkSurfaceKHR surface) :
	device(&device),
	surface(surface)
{}

surface_provider::~surface_provider()
{
	if (this->surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(this->device->get_instance(), this->surface, nullptr);
		this->surface = VK_NULL_HANDLE;
	}
}

VkInstance surface_provider::get_instance() const
{
	return this->device->get_instance();
}

VkPhysicalDevice surface_provider::get_physical_device() const
{
	return this->device->get_physical_device();
}

VkDevice surface_provider::get_device() const
{
	return this->device->get_device();
}

VkQueue surface_provider::get_queue() const
{
	return this->device->get_queue();
}

} // namespace ruis::render::vulkan
