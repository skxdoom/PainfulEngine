$input v_texcoord0, v_texcoord1, v_normal, v_viewdist

// Deliberately NOT physically based: these assets are diffuse maps plus baked
// lightmaps authored in 2004, so the shading model stays albedo * lightmap.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse,  0);
SAMPLER2D(s_lightmap, 1);
SAMPLER2D(s_detail,   2);
SAMPLER2D(s_blend2,   3);
SAMPLER2D(s_mask2,    4);

uniform vec4 u_params;    // x: has lightmap, y: alpha-test ref (<0 off), z: terrain blend, w: unused
uniform vec4 u_uvanim;    // xy: stage-0 scroll offset, zw: stage-1 scroll offset
uniform vec4 u_detail;    // xy: detail tiling, z: detail on/off
uniform vec4 u_uv0;       // diffuse slot UV transform: scale xy, offset zw
uniform vec4 u_uv1;       // blend slot UV transform: scale xy, offset zw
uniform vec4 u_ambient;   // rgb: level ambient, w: lightmap scale (2 when Overbright)
uniform vec4 u_fogColor;  // rgb: level fog colour
uniform vec4 u_fog;       // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density

void main()
{
	// Each material slot carries its own UV transform in the .mpk (scale in
	// xy, offset in zw), the same job the engine's oT0 = v1 * c24/c25 texture
	// matrix does. pan[N] then scrolls that result over time.
	vec2 uvDiffuse = v_texcoord0 * u_uv0.xy + u_uv0.zw + u_uvanim.xy;
	vec4 base = texture2D(s_diffuse, uvDiffuse);

	// Terrain blending: two TILED textures mixed by a mask that maps once
	// across the surface (so the mask rides the second UV set). The mask
	// value is colour times alpha, as in the sky compositor.
	if (u_params.z > 0.5)
	{
		vec2 uvBlend = v_texcoord0 * u_uv1.xy + u_uv1.zw + u_uvanim.xy;
		vec4 t2 = texture2D(s_blend2, uvBlend);
		vec4 mk = texture2D(s_mask2, v_texcoord1);
		base.rgb = mix(base.rgb, t2.rgb, mk.r * mk.a);
	}

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
		light = texture2D(s_lightmap, v_texcoord1 + u_uvanim.zw).rgb * u_ambient.w;
	}

	// Detail map, combined the way defaultTU2detail does: "texture addsigned
	// previous" - grey-centred grain added before the lightmap modulate.
	vec3 albedo = base.rgb;
	if (u_detail.z > 0.5)
	{
		// The tu2_detail vertex shader derives the detail coordinates from
		// the DIFFUSE UVs times the level's DetailMap tiling.
		vec3 grain = texture2D(s_detail, v_texcoord0 * u_detail.xy).rgb;
		albedo = clamp(albedo + grain - vec3_splat(0.5), 0.0, 1.0);
	}

	vec3 color = albedo * (light + u_ambient.rgb);

	// Fog modes match CLevel.lua: 0=none, 1=exp, 2=exp2, 3=linear. As in D3D
	// fixed function, only linear fog uses the start/end range; the
	// exponential modes use density alone.
	float fog = 1.0;
	if (u_fog.x > 2.5)
	{
		fog = (u_fog.z - v_viewdist) / max(u_fog.z - u_fog.y, 0.001);
	}
	else if (u_fog.x > 1.5)
	{
		float fd = u_fog.w * v_viewdist;
		fog = exp(-fd * fd);
	}
	else if (u_fog.x > 0.5)
	{
		fog = exp(-u_fog.w * v_viewdist);
	}
	color = mix(u_fogColor.rgb, color, clamp(fog, 0.0, 1.0));

	gl_FragColor = vec4(color, base.a);
}
