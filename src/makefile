include prorab.mk
include prorab-license.mk
include prorab-clang-format.mk
include prorab-install-dbgsrc.mk

$(eval $(call prorab-config, ../config))

this_src_dir := ruis

this_name := ruis-render-vulkan$(this_dbg)

this_soname := $(shell cat $(d)soname.txt)

this_srcs += $(call prorab-src-dir, $(this_src_dir))

# NOTE: the GLSL sources (*.vert/*.frag) are compiled to SPIR-V and embedded
# in shader_spirv.hpp, so they are not part of this_srcs (prorab-src-dir only
# picks up *.c/*.S/*$(this_dot_cxx)).

ifeq ($(os), linux)
    this_ldlibs += -l vulkan
else ifeq ($(os), windows)
    this_ldlibs += -l vulkan-1
else ifeq ($(os), macosx)
    this_ldlibs += -l MoltenVK
endif

this_ldlibs += -l ruis$(this_dbg)
this_ldlibs += -l utki$(this_dbg)

$(eval $(prorab-build-lib))

this_license_file := ../LICENSE
$(eval $(prorab-license))

$(eval $(prorab-clang-format))

$(eval $(prorab-install-dbgsrc))
