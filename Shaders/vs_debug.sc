$input a_position, a_color0
$output v_color0

// Debug lines arrive in world space with their colour already chosen, so there
// is nothing to transform but the view projection.
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0    = a_color0;
}
