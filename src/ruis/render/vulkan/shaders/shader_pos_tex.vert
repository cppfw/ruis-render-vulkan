// ruis-render-vulkan - shader_pos_tex vertex shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(location = 0) in vec4 a0; // position
layout(location = 1) in vec2 a1; // texture coordinates

layout(location = 0) out vec2 tc0;

void main(void) {
	gl_Position = draw.matrix * a0;
	tc0 = a1.xy;
}
