// ruis-render-vulkan - shader_pos_clr fragment shader
#version 450

layout(location = 0) in vec4 color_in;
layout(location = 0) out vec4 frag_color;

void main(void) {
	frag_color = color_in;
}
