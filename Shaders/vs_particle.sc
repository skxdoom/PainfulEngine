$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

// Particle quads arrive already built in world space - the CPU side faces them
// at the camera - so there is no model matrix to apply.
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0    = a_color0;
	v_texcoord0 = a_texcoord0;
}
