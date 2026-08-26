$input v_texcoord0, v_texcoord1, v_normal, v_viewdist

// Deliberately NOT physically based: these assets are diffuse maps plus baked
// lightmaps authored in 2004, so the shading model stays albedo * lightmap.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse,  0);
SAMPLER2D(s_lightmap, 1);

uniform vec4 u_params;    // x: has lightmap, y: alpha-test ref (<0 off), z: fog density, w: fog start
uniform vec4 u_ambient;   // rgb: level ambient, w: lightmap scale (2 when Overbright)
uniform vec4 u_fogColor;  // rgb: level fog colour

void main()
{
	vec4 base = texture2D(s_diffuse, v_texcoord0);

	// Fixed-function alpha test ("alphafunc greater" in the material scripts).
	if (u_params.y >= 0.0 && base.a <= u_params.y)
	{
		discard;
	}

	// The material scripts modulate by the lightmap either x1 (defaultTU2) or
	// x2 (defaultTU2x2, chosen when the level sets Overbright).
	vec3 light = vec3_splat(1.0);
	if (u_params.x > 0.5)
	{
		light = texture2D(s_lightmap, v_texcoord1).rgb * u_ambient.w;
	}

	vec3 color = base.rgb * (light + u_ambient.rgb);

	// Exponential distance fog. The level supplies both a density and a start
	// distance; fog must not begin until beyond that start.
	float fogDist = max(v_viewdist - u_params.w, 0.0);
	float fog = clamp(exp(-u_params.z * fogDist), 0.0, 1.0);
	color = mix(u_fogColor.rgb, color, fog);

	gl_FragColor = vec4(color, base.a);
}
