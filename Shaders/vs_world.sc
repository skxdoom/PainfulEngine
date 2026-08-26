$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_normal, v_viewdist

// Static world geometry. UV set 0 is the diffuse map, UV set 1 the baked
// lightmap; PainEngine authored both, so lighting is mostly a texture lookup.
#include <bgfx_shader.sh>

void main()
{
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	gl_Position  = mul(u_proj, viewPos);

	v_viewdist  = length(viewPos.xyz);
	v_normal    = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
}
