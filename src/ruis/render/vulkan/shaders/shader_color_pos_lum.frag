// ruis-render-vulkan - shader_color_pos_lum fragment shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(location = 0) in float lum;
layout(location = 0) out vec4 frag_color;

void main(void) {
	frag_color = vec4(
		draw.color.x,
		draw.color.y,
		draw.color.z,
		draw.color.w * lum
	);
}
