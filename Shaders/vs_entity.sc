$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_viewdist, v_wpos

// Models. Unlike the world mesh these carry no lightmap - PainEngine lit them
// at runtime instead, from an ambient, one directional light and up to four
// dynamic ones (see World/Lighting.h). The vertex shader's job is only to hand
// the fragment shader a world-space normal; the lighting itself is per pixel,
// where the original was per vertex, because a per-vertex N.L on a 500-triangle
// monk reads as faceting rather than as lighting.
#include <bgfx_shader.sh>

uniform vec4 u_viewDepth; // x: depth scale (1 = none), y: clip z runs -w..w

void main()
{
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	gl_Position = mul(u_proj, viewPos);

	// The view model's depth, squeezed into the front of the range so the world
	// never covers the weapon and it still sorts against itself. The image does
	// not move, and lighting reads v_wpos. Lighting.md, "The view model's depth".
	if (u_viewDepth.x < 1.0) {
		if (u_viewDepth.y > 0.5) gl_Position.z = (gl_Position.z + gl_Position.w) * u_viewDepth.x - gl_Position.w;
		else gl_Position.z *= u_viewDepth.x;
	}

	v_viewdist = length(viewPos.xyz);
	// The world position: the positional lights are measured from it per pixel.
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	v_texcoord0 = a_texcoord0;
}
