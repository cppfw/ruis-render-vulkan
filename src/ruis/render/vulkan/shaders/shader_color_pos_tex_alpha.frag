// ruis-render-vulkan - shader_color_pos_tex_alpha fragment shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(set = 0, binding = 1) uniform sampler2D texture0;

layout(location = 0) in vec2 tc0;
layout(location = 0) out vec4 frag_color;

void main(void) {
	frag_color = vec4(
		draw.color.x,
		draw.color.y,
		draw.color.z,
		draw.color.w * texture(texture0, tc0).x
	);
}
