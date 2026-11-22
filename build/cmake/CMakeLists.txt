cmake_minimum_required(VERSION 3.30)

project(ruis-render-vulkan)

set(dir_name ruis)

# !!! find_package must go after project() declaration !!!
# Otherwise VCPKG does not set the CMAKE_PREFIX_PATH to find packages.
find_package(myci CONFIG REQUIRED)

set(srcs)
myci_add_source_files(srcs
    DIRECTORY
        ../../src/${dir_name}
    RECURSIVE
)

# exclude the GLSL sources (they are compiled to SPIR-V and embedded in shader_spirv.hpp)
list(FILTER srcs EXCLUDE REGEX "\\.vert$")
list(FILTER srcs EXCLUDE REGEX "\\.frag$")

myci_declare_library(${PROJECT_NAME}
    SOURCES
        ${srcs}
    PUBLIC_INCLUDE_DIRECTORIES
        ../../src
    INSTALL_INCLUDE_DIRECTORIES
        ../../src/${dir_name}
    DEPENDENCIES
        rasterimage
        r4
        utki
        ruis
        Vulkan::Vulkan
)
