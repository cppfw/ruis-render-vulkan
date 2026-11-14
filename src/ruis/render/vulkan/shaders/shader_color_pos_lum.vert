// ruis-render-vulkan - shader_color_pos_lum vertex shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(location = 0) in vec4 a0; // position
layout(location = 1) in float a1; // luminance

layout(location = 0) out float lum;

void main(void) {
	gl_Position = draw.matrix * a0;
	lum = a1;
}
