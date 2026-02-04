#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Push constants for RmlUI
layout (push_constant) uniform PushConsts
{
    mat4  mvp;
    vec2  translate;
    vec2  padding;
} push_constants;

// RmlUI vertex format: position (vec2), color (u8vec4 normalized), texcoord (vec2)
layout (location = 0) in vec2 in_position;
layout (location = 1) in vec4 in_color;  // normalized u8vec4
layout (location = 2) in vec2 in_texcoord;

layout (location = 0) out vec4 out_color;
layout (location = 1) out vec2 out_texcoord;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    vec2 translated_pos = in_position + push_constants.translate;
    gl_Position = push_constants.mvp * vec4(translated_pos, 0.0, 1.0);
    out_color = in_color;
    out_texcoord = in_texcoord;
}
