$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_clip, v_strength

// The particle_warp technique's vertex half (Particle.fxo, vs_1_1 at 2344):
// the clip position is passed on for the screen lookup, and the warp
// strength is saturate((z/w - 0.96) * 32) - nothing within a couple of metres
// of the eye, full past ten. Particles.md, "The warp sprites".
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0 = a_color0;
	v_texcoord0 = a_texcoord0;
	v_clip = gl_Position;
	float z01 = gl_Position.z / gl_Position.w;
#if BGFX_SHADER_LANGUAGE_GLSL
	z01 = z01 * 0.5 + 0.5;
#endif
	v_strength = clamp((z01 - 0.96) * 32.0, 0.0, 1.0);
}
