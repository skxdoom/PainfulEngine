$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_viewdist

// Models. Unlike the world mesh these carry no lightmap - PainEngine lit them
// at runtime instead, from an ambient, one directional light and up to four
// dynamic ones (see World/Lighting.h). The vertex shader's job is only to hand
// the fragment shader a world-space normal; the lighting itself is per pixel,
// where the original was per vertex, because a per-vertex N.L on a 500-triangle
// monk reads as faceting rather than as lighting.
#include <bgfx_shader.sh>

void main()
{
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	gl_Position  = mul(u_proj, viewPos);

	v_viewdist  = length(viewPos.xyz);
	v_normal    = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	v_texcoord0 = a_texcoord0;
}
