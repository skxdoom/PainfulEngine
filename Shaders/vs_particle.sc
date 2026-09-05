$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_viewdist

// Particle quads arrive already built in world space - the CPU side faces them
// at the camera - so there is no model matrix to apply. The view distance
// feeds the same fog term the world and entity shaders use.
#include <bgfx_shader.sh>

void main()
{
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0    = a_color0;
	v_texcoord0 = a_texcoord0;
	v_viewdist  = length(viewPos.xyz);
}
