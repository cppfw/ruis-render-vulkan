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

#include "device.hpp"

namespace ruis::render::vulkan {

namespace {
// create a VkInstance and the physical device
void create_instance_and_physical_device(
	const std::vector<std::string>& instance_extensions,
	VkInstance& instance,
	VkPhysicalDevice& physical_device,
	bool& owns_instance
)
{
	std::vector<const char*> c_extensions;
	c_extensions.reserve(instance_extensions.size());
	for (const auto& e : instance_extensions) {
		c_extensions.push_back(e.c_str());
	}

	const auto app_info = VkApplicationInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "ruisapp",
		.apiVersion = VK_MAKE_VERSION(1, 2, 0)
	};

	VkInstanceCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo = &app_info;
	if (!c_extensions.empty()) {
		info.enabledExtensionCount = uint32_t(c_extensions.size());
		info.ppEnabledExtensionNames = c_extensions.data();
	}

	check_vk_result(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
	owns_instance = true;

	uint32_t count = 0;
	check_vk_result(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
	std::vector<VkPhysicalDevice> devices(count);
	check_vk_result(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
	if (count == 0) {
		throw std::runtime_error("no Vulkan physical devices found");
	}
	physical_device = devices[0];
}
} // namespace

device::~device()
{
	if (this->vk_device != VK_NULL_HANDLE) {
		vkDestroyDevice(this->vk_device, nullptr);
		this->vk_device = VK_NULL_HANDLE;
	}
	if (this->owns_instance && this->instance != VK_NULL_HANDLE) {
		vkDestroyInstance(this->instance, nullptr);
		this->instance = VK_NULL_HANDLE;
	}
}

device::device(std::vector<std::string> instance_extensions, std::vector<std::string> device_extensions)
{
	if (device_extensions.empty()) {
		device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	}

	create_instance_and_physical_device(
		instance_extensions,
		this->instance,
		this->physical_device,
		this->owns_instance
	);

	// find the graphics queue family
	std::vector<VkQueueFamilyProperties> families;
	uint32_t family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(this->physical_device, &family_count, nullptr);
	families.resize(family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(this->physical_device, &family_count, families.data());

	uint32_t graphics_family = 0;
	for (uint32_t i = 0; i < family_count; ++i) {
		if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			this->graphics_queue_family = i;
			break;
		}
	}

	const float queue_priority = 1.0f;
	const auto queue_info = VkDeviceQueueCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = graphics_family,
		.queueCount = 1,
		.pQueuePriorities = &queue_priority
	};

	std::vector<const char*> c_device_extensions;
	c_device_extensions.reserve(device_extensions.size());
	for (const auto& e : device_extensions) {
		c_device_extensions.push_back(e.c_str());
	}

	VkDeviceCreateInfo device_info{};
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	if (!c_device_extensions.empty()) {
		device_info.enabledExtensionCount = uint32_t(c_device_extensions.size());
		device_info.ppEnabledExtensionNames = c_device_extensions.data();
	}

	check_vk_result(vkCreateDevice(this->physical_device, &device_info, nullptr, &this->vk_device), "vkCreateDevice");

	vkGetDeviceQueue(this->vk_device, graphics_family, 0, &this->queue);
	this->graphics_queue_family = graphics_family;
}

} // namespace ruis::render::vulkan
