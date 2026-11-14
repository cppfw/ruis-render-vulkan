// ruis-render-vulkan - shader_color vertex shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(location = 0) in vec4 a0;

void main(void) {
	gl_Position = draw.matrix * a0;
}
