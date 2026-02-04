#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	float gamma;
	float contrast;
	float warp_strength;
	float chromatic_strength;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D game_texture;
layout (set = 1, binding = 0) uniform sampler2D ui_texture;

layout (location = 0) in vec2 in_uv;

layout (location = 0) out vec4 out_frag_color;

vec2 barrel_distort (vec2 uv, float strength)
{
	vec2 centered = uv - 0.5;
	float r2 = dot (centered, centered);
	vec2 distorted = centered * (1.0 + strength * r2);
	return distorted + 0.5;
}

void main ()
{
	// Sample game at straight UV (no distortion)
	vec3 game = texture (game_texture, in_uv).rgb;

	// Sample UI with warp + optional chromatic aberration
	float warp = push_constants.warp_strength;
	float chroma = push_constants.chromatic_strength;

	// Scale down UI slightly when warped - gives a "curved glass" feel
	float ui_scale = 1.0 + abs (warp) * 0.5;
	vec2 ui_uv = (in_uv - 0.5) * ui_scale + 0.5;

	vec4 ui;

	if (chroma > 0.0)
	{
		// Chromatic aberration: sample R/G/B at slightly different warp strengths
		vec2 uv_r = barrel_distort (ui_uv, warp + chroma);
		vec2 uv_g = barrel_distort (ui_uv, warp);
		vec2 uv_b = barrel_distort (ui_uv, warp - chroma);

		ui.r = texture (ui_texture, uv_r).r;
		ui.g = texture (ui_texture, uv_g).g;
		ui.b = texture (ui_texture, uv_b).b;
		ui.a = texture (ui_texture, uv_g).a;
	}
	else if (warp != 0.0)
	{
		vec2 uv_warped = barrel_distort (ui_uv, warp);
		ui = texture (ui_texture, uv_warped);
	}
	else
	{
		ui = texture (ui_texture, ui_uv);
	}

	// Composite: UI buffer is premultiplied alpha (blended onto transparent black)
	vec3 frag = game * (1.0 - ui.a) + ui.rgb;

	// Gamma and contrast
	frag = frag * push_constants.contrast;
	out_frag_color = vec4 (pow (frag, vec3 (push_constants.gamma)), 1.0);
}
