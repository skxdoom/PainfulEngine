$input v_color0, v_texcoord0, v_clip, v_strength

// The particle_warp technique's pixel half (Particle.fxo, ps_2_0 at 1832):
// the scene is read at the sprite's own screen position, pushed by the warp
// texture (0..1 mapped to -1..1) times strength times 0.1, and the result is
// blended in by (1 - diffuse alpha) times the particle alpha - so the atten
// texture's clear centre warps and its opaque rim does not.
// Particles.md, "The warp sprites".
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
SAMPLER2D(s_scene, 1);
SAMPLER2D(s_warp, 2);

void main()
{
	vec2 warp = texture2D(s_warp, v_texcoord0).xy * 2.0 - 1.0;
	vec2 ndc = v_clip.xy / v_clip.w;
	vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
#if BGFX_SHADER_LANGUAGE_GLSL
	uv.y = 1.0 - uv.y;
#endif
	// Added before the divide by w in the original (mad into oT1, then texldp), so
	// the push shrinks with distance: a tenth of the screen at 1 m, a hundredth at 10.
	uv += warp * (v_strength * 0.1) / v_clip.w;
	vec3 scene = texture2D(s_scene, uv).rgb;
	float atten = texture2D(s_diffuse, v_texcoord0).a;
	gl_FragColor = vec4(scene, (1.0 - atten) * v_color0.a);
}
