// ruis-render-vulkan - shader_color fragment shader
#version 450

layout(set = 0, binding = 0) uniform DrawUniforms {
	mat4 matrix;
	vec4 color;
} draw;

layout(location = 0) out vec4 frag_color;

void main(void) {
	frag_color = draw.color;
}
