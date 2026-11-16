// this file is to test basic usage of the library,
// mainly test that linking to the library works, so just reference any
// non-inline function of the library to test it.

#include <ruis/render/vulkan/context.hpp>
#include <ruis/render/vulkan/vulkan_device.hpp>

#include <functional>
#include <iostream>

int main(int argc, const char** argv){
	// reference a non-inline function of the library to test that linking works.
	// do not call it (would require a real Vulkan device).
	std::function<void()> f = [](){
		(void)ruis::render::vulkan::vulkan_device::make({}, {});
	};

	if(!f){
		return 1;
	}

	std::cout << "hello ruis-render-vulkan!" << std::endl;

	return 0;
}
