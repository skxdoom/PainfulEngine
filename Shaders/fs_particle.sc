$input v_color0, v_texcoord0, v_viewdist

// The original modulates the sprite texture by a single per-particle D3DCOLOR
// (colour from the Color ramp, alpha from the fade curve), then D3D fogs the
// colour - not the alpha - before the blend: the sprite passes set the world
// fog block (device slot 0xb0) and the `simple` vertex shader writes oFog.
// Under an additive blend that pulls a far corona toward the fog colour, which
// for a dark fog is a dimming with distance. Particles.md, "Fog reaches the sprites".
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
uniform vec4 u_fogColor;  // rgb: level fog colour
uniform vec4 u_fog;       // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density

void main()
{
	vec4 color = texture2D(s_diffuse, v_texcoord0) * v_color0;
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
	color.rgb = mix(u_fogColor.rgb, color.rgb, clamp(fog, 0.0, 1.0));
	gl_FragColor = color;
}
