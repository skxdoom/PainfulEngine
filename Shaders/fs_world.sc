$input v_texcoord0, v_texcoord1, v_normal, v_viewdist

// Deliberately NOT physically based: these assets are diffuse maps plus baked
// lightmaps authored in 2004, so the shading model stays albedo * lightmap.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse,  0);
SAMPLER2D(s_lightmap, 1);

uniform vec4 u_params;    // x: has lightmap, y: alpha-test, z: fog density, w: fog start
uniform vec4 u_ambient;   // rgb: level ambient
uniform vec4 u_fogColor;  // rgb: level fog colour

void main()
{
	vec4 base = texture2D(s_diffuse, v_texcoord0);

	if (u_params.y > 0.5 && base.a < 0.5)
	{
		discard;
	}

	// Lightmaps are stored at half intensity, hence the x2 on decode.
	vec3 light = vec3_splat(1.0);
	if (u_params.x > 0.5)
	{
		light = texture2D(s_lightmap, v_texcoord1).rgb * 2.0;
	}

	vec3 color = base.rgb * (light + u_ambient.rgb);

	// Exponential distance fog. The level supplies both a density and a start
	// distance; fog must not begin until beyond that start.
	float fogDist = max(v_viewdist - u_params.w, 0.0);
	float fog = clamp(exp(-u_params.z * fogDist), 0.0, 1.0);
	color = mix(u_fogColor.rgb, color, fog);

	gl_FragColor = vec4(color, base.a);
}
