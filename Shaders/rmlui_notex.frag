#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 in_color;
layout (location = 1) in vec2 in_texcoord;

layout (location = 0) out vec4 out_frag_color;

void main()
{
    // Output vertex color directly (untextured geometry)
    out_frag_color = in_color;
}
