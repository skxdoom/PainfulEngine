$input a_position, a_normal, a_texcoord0, a_texcoord2, a_texcoord3, a_texcoord4
$output v_texcoord0, v_normal, v_viewdist, v_wpos, v_bone0, v_bone1, v_bone2

// vs_entity plus the second stream the normal-mapped parts carry: the rows of
// each vertex's first bone rotation, the images of the model's bind axes. Skin.fxo's
// FXSkinBump turns the LIGHT into that bone's space instead; carrying the axes
// out to world space is the same rotation run the other way.
// Docs/Reference/Lighting.md, "Weapon normal maps"
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
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
	v_texcoord0 = a_texcoord0;
	v_bone0 = mul(u_model[0], vec4(a_texcoord2, 0.0)).xyz;
	v_bone1 = mul(u_model[0], vec4(a_texcoord3, 0.0)).xyz;
	v_bone2 = mul(u_model[0], vec4(a_texcoord4, 0.0)).xyz;
}
