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

#include "shader_common.hpp"

#include <cstdlib>
#include <cstdio>
#include <utility>

#include "../context.hpp"
#include "../texture_2d.hpp"
#include "../vertex_array.hpp"

namespace ruis::render::vulkan {

void vulkan_shader_render(
	const utki::shared_ref<const ruis::render::context>& rendering_context, //
	shader_kind kind,
	const void* vertex_spirv,
	size_t vertex_spirv_len,
	const void* fragment_spirv,
	size_t fragment_spirv_len,
	const texture_2d* texture, // nullptr for non-textured shaders
	const r4::matrix4<float>& m,
	const r4::vector4<float>& color,
	const ruis::render::vertex_array& va
)
{
	// use the currently active context (set by ruis::render::context::apply()),
	// not the context stored in the shader (which may be the shared resource-loader context).
	auto& ctx =
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast, "assert(dynamic_cast) done")
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		const_cast<context&>(static_cast<const context&>(ruis::render::context::current()));

	const bool textured = shader_has_texture(kind);

	// skip drawing if the viewport is zero-sized (e.g. resource loader context)
	{
		const auto& vp = ctx.get_viewport();
		if (vp.d.x() == 0 || vp.d.y() == 0) {
			return;
		}
	}

	// make sure the texture data is uploaded to the GPU. The texture is owned by
	// the context that created it (e.g. the shared resource-loader context),
	// which may be different from the context that is currently rendering, so
	// the upload must be driven from the texture itself (it uses its own
	// context's one-shot command path).
	if (texture != nullptr) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast, "ensure_uploaded mutates the upload state")
		const_cast<texture_2d*>(texture)->ensure_uploaded();
	}

	ctx.ensure_frame_began();

	// map the VAO's rendering mode to a Vulkan primitive topology
	const auto to_vk_topology = [](ruis::render::vertex_array::mode m) -> VkPrimitiveTopology {
		switch (m) {
		case ruis::render::vertex_array::mode::triangles:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case ruis::render::vertex_array::mode::triangle_fan:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		case ruis::render::vertex_array::mode::line_loop:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case ruis::render::vertex_array::mode::triangle_strip:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		default:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	};

	const auto pipeline =
		ctx.get_or_create_pipeline(kind, textured, to_vk_topology(va.rendering_mode), vertex_spirv, vertex_spirv_len, fragment_spirv, fragment_spirv_len);

	const auto ubo_buffer = ctx.update_uniforms(m, color);

	const auto descriptor_set =
		ctx.allocate_descriptor_set(textured, ubo_buffer, texture ? texture->image_view : VK_NULL_HANDLE, texture ? texture->sampler : VK_NULL_HANDLE);

	if (getenv("VU_DEBUG_DRAW") != nullptr) { // NOLINT(concurrency-mt-unsafe)
		const auto vp = ctx.get_viewport();
		const float W = float(vp.d.x());
		const float H = float(vp.d.y());
		auto ndc2scr = [&](float px, float py) { // NOLINT(concurrency-mt-unsafe)
			float nx = m[0][0]*px + m[0][1]*py + m[0][3];
			float ny = m[1][0]*px + m[1][1]*py + m[1][3];
			return std::make_pair((nx+1)/2*W, (1-ny)/2*H);
		};
		auto a = ndc2scr(0,0); auto c = ndc2scr(1,1);
		const bool sc_en = ctx.is_scissor_enabled();
		const auto sc = ctx.get_scissor();
		fprintf(stderr, "[VU-QUAD] W=%d H=%d kind=%d tex=%p fb=%d sciss=%d rect=(%d,%d,%d,%d) TL=(%.0f,%.0f) BR=(%.0f,%.0f)\n", // NOLINT(concurrency-mt-unsafe)
				int(vp.d.x()), int(vp.d.y()), (int)kind, (void*)texture, ctx.current_fb_debug(), // NOLINT(concurrency-mt-unsafe)
				int(sc_en), int(sc.p.x()), int(sc.p.y()), int(sc.d.x()), int(sc.d.y()), // NOLINT(concurrency-mt-unsafe)
				a.first, a.second, c.first, c.second); // NOLINT(concurrency-mt-unsafe)
	}

	ctx.draw_indexed(pipeline, textured ? ctx.pipeline_layout_with_tex : ctx.pipeline_layout_no_tex, va, descriptor_set);
}
} // namespace ruis::render::vulkan
