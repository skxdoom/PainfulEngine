$input v_color0, v_texcoord0, v_viewdist

// A multiplying decal's fog, as a second pass (Render/DecalRenderer.cpp). The
// first pass darkens or tints the wall with the decal unfogged, which darkens the
// fog the wall already took as well; this adds the decal's share of that fog
// back, additively, or takes it off where modulate2x brightened past it.
// Docs/Reference/Decals.md, "Fog under a multiplying blend".
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
uniform vec4 u_fogColor; // rgb: level fog colour
uniform vec4 u_fog; // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density
uniform vec4 u_decalFog; // x: 0 invmodulate, 1 modulate or filter, 2 modulate2x's share to add, 3 its share to take

void main()
{
	vec3 c = (texture2D(s_diffuse, v_texcoord0) * v_color0).rgb;
	float fog = 1.0;
	if (u_fog.x > 2.5)
		fog = (u_fog.z - v_viewdist) / max(u_fog.z - u_fog.y, 0.001);
	else if (u_fog.x > 1.5)
	{
		float fd = u_fog.w * v_viewdist;
		fog = exp(-fd * fd);
	}
	else if (u_fog.x > 0.5)
		fog = exp(-u_fog.w * v_viewdist);
	vec3 fogged = u_fogColor.rgb * (1.0 - clamp(fog, 0.0, 1.0));
	vec3 share;
	if (u_decalFog.x < 0.5) share = c;
	else if (u_decalFog.x < 1.5) share = vec3_splat(1.0) - c;
	else if (u_decalFog.x < 2.5) share = max(vec3_splat(1.0) - 2.0 * c, vec3_splat(0.0));
	else share = max(2.0 * c - vec3_splat(1.0), vec3_splat(0.0));
	gl_FragColor = vec4(share * fogged, 1.0);
}
