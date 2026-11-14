// ruis-render-vulkan - shader_pos_clr vertex shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(location = 0) in vec4 a0; // position
layout(location = 1) in vec4 a1; // color

layout(location = 0) out vec4 color_out;

void main(void) {
	gl_Position = draw.matrix * a0;
	color_out = a1;
}
