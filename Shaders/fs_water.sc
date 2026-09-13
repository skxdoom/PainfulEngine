$input v_texcoord0, v_texcoord1, v_wpos, v_viewdist, v_clip

// Water on the nv30 tier, four techniques out of Water.fxo chosen by the
// map's .EMesh material: FXWater_20 ("water", ps @23096), SimpleWaterNTU
// ("water_ntu", ps @8436), FXWaterNTU_Refl ("water_ntu_refl", ps @13292) and
// FXWaterNTU_RR ("water_ntu_rr", ps @10720), with RenderWater's constant
// uploads (0x101d8bb0). Docs/Reference/Water.md, "The four techniques".
#include <bgfx_shader.sh>

SAMPLER2D(s_normal, 0);
SAMPLERCUBE(s_cube, 1);
SAMPLER2D(s_lightmap, 2);
SAMPLER2D(s_refl, 3);
SAMPLER2D(s_refr, 4);

uniform vec4 u_tile; // xy: layer 0 tile, zw: layer 1 tile
uniform vec4 u_uvanim; // xy: layer 0 pan * t, zw: layer 1 pan * t
uniform vec4 u_eye; // xyz: camera position in world space
uniform vec4 u_fogColor;
uniform vec4 u_fog; // x: mode, y: start, z: end, w: density
uniform vec4 u_water; // x BumpHeight (the rest is the vertex wave)
// GFresBias and GRefParams: x 1 - FresnelBias, y FresnelBias, z FresnelExponent,
// w ReflectionAmount.
uniform vec4 u_waterFres;
uniform vec4 u_waterDeep; // rgb WaterAmount * DeepWaterColor
uniform vec4 u_waterShallow; // rgb WaterAmount * ShallowWaterColor
// x family (0 water, 1 ntu, 2 ntu_refl, 3 ntu_rr), y ReflectScale, z 1 when
// the targets' origin is bottom-left, w RefractScale.
uniform vec4 u_waterMode;

void main()
{
	// Two layers of the same world-space normal map, each (uv + pan t) * tile,
	// summed and biased: n0 + n1 - 1, NOT normalised - the effects never do.
	vec2 uv0 = (v_texcoord0 + u_uvanim.xy) * u_tile.xy;
	vec2 uv1 = (v_texcoord0 + u_uvanim.zw) * u_tile.zw;
	vec3 raw = texture2D(s_normal, uv0).xyz + texture2D(s_normal, uv1).xyz - 1.0;
	vec3 n = raw;
	// GBumpSpace's diagonal: BumpHeight on the two horizontal components.
	n *= vec3(u_water.x, 1.0, u_water.x);

	vec3 toEye = u_eye.xyz - v_wpos;
	vec3 eyeN = normalize(toEye);
	// f = (1 - bias) * (1 - n.e)^exp + bias, with n unnormalised, as shipped.
	float facing = max(1.0 - dot(n, eyeN), 0.0);
	float f = u_waterFres.x * pow(facing, u_waterFres.z) + u_waterFres.y;
	float family = u_waterMode.x;

	vec3 color;
	if (family < 1.5)
	{
		// The cube map through R = 2 (n.E) n - E; lerp(shallow, cube * amount
		// + (1 - amount) * deep, f), and FXWater_20 alone multiplies the
		// lightmap in, twice over.
		vec3 r = 2.0 * dot(n, toEye) * n - toEye;
		vec3 refl = textureCube(s_cube, r).rgb;
		vec3 mixed = refl * u_waterFres.w + (1.0 - u_waterFres.w) * u_waterDeep.rgb;
		color = mix(u_waterShallow.rgb, mixed, f);
		if (family < 0.5) color *= texture2D(s_lightmap, v_texcoord1).rgb * 2.0;
	}
	else
	{
		// The reflection target, projected from the water's own clip position
		// and bent by the normal's horizontal components (GRefScales.x): the
		// mirrored camera's image runs top to bottom the other way, so v is
		// 0.5 + 0.5 ndc.y here - the effect's oT5, not the flipped oT6.
		vec2 ndc = v_clip.xy / v_clip.w;
		// The bend takes the RAW sum, before BumpHeight - ps @13292 keeps r0 for
		// it and scales a copy for the fresnel.
		vec2 bend = vec2(raw.x, raw.z) / v_clip.w;
		vec2 uvRefl = vec2(0.5 + 0.5 * ndc.x, 0.5 + 0.5 * ndc.y) + bend * u_waterMode.y;
		if (u_waterMode.z > 0.5) uvRefl.y = 1.0 - uvRefl.y;
		vec3 refl = texture2D(s_refl, uvRefl).rgb;
		// sat((f - 0.75) * 4 * refl): the glint the effect adds at grazing angles.
		vec3 boost = clamp((f - 0.75) * 4.0 * refl, 0.0, 1.0);
		vec3 mixed = refl * u_waterFres.w + (1.0 - u_waterFres.w) * u_waterDeep.rgb + boost;
		vec3 above = f * mixed * u_waterDeep.rgb;
		if (family < 2.5)
		{
			color = above + (1.0 - f) * u_waterShallow.rgb;
		}
		else
		{
			// The refraction target is the main camera's own picture of what
			// lies under the plane, so its v runs the usual way.
			vec2 uvRefr = vec2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y) + bend * u_waterMode.w;
			if (u_waterMode.z > 0.5) uvRefr.y = 1.0 - uvRefr.y;
			vec3 refr = texture2D(s_refr, uvRefr).rgb;
			color = above + (1.0 - f) * u_waterShallow.rgb * refr;
		}
	}

	// Fog matches the world pass; modes are CLevel.lua's.
	if (u_fog.x > 0.5)
	{
		float fog = 1.0;
		if (u_fog.x < 1.5) fog = exp(-u_fog.w * v_viewdist);
		else if (u_fog.x < 2.5) fog = exp(-u_fog.w * u_fog.w * v_viewdist * v_viewdist);
		else fog = (u_fog.z - v_viewdist) / max(u_fog.z - u_fog.y, 0.001);
		color = mix(u_fogColor.rgb, color, clamp(fog, 0.0, 1.0));
	}

	gl_FragColor = vec4(color, 1.0);
}
