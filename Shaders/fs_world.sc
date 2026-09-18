$input v_texcoord0, v_texcoord1, v_normal, v_viewdist, v_wpos

// Deliberately NOT physically based: these assets are diffuse maps plus baked
// lightmaps authored in 2004, so the shading model stays albedo * lightmap.
//
// Dynamic lights are the one thing the lightmap cannot carry, and they are
// added on top of it here. The original drew them as extra `blend add` passes
// over the same geometry (WorldMesh::RenderLightPass, 0x101d9740, one per
// light off the mesh's own list); folding them into this pass reaches the same
// sum in one draw. Docs/Reference/Lighting.md
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
SAMPLER2D(s_lightmap, 1);
SAMPLER2D(s_detail, 2);
SAMPLER2D(s_blend2, 3);
SAMPLER2D(s_mask2, 4);
// The dynamic lights, the flashlight's two maps at stages 5 and 6, and its
// shadow map at 7.
#define PAINFUL_PROJ_STAGE 5
#define PAINFUL_PROJFALL_STAGE 6
#define PAINFUL_SHADOW_STAGE 7
#define PAINFUL_CHARSHADOW_STAGE 8
// The placed lights' atlas: what a model occludes of a baked light comes off
// the lightmap here.
#define PAINFUL_LIGHTSHADOW_STAGE 9
#include "shared_lights.sh"


uniform vec4 u_params; // x: has lightmap, y: alpha-test ref (<0 off), z: terrain blend, w: lighting-only view
uniform vec4 u_uvanim; // xy: stage-0 scroll offset, zw: stage-1 scroll offset
uniform vec4 u_detail; // xy: detail tiling, z: detail on/off
uniform vec4 u_uv0; // diffuse slot UV transform: scale xy, offset zw
uniform vec4 u_tile; // tile[N]: xy stage 0, zw stage 1
uniform vec4 u_uv1; // blend slot UV transform: scale xy, offset zw
uniform vec4 u_ambient; // rgb: level ambient, w: lightmap scale (2 when Overbright)
uniform vec4 u_fogColor; // rgb: level fog colour
uniform vec4 u_fog; // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density
// The water passes clip the world at the surface: y = w, keep y * sign above.
// (0, 0, 0, 0) is off. Water.md, "The planar reflection".
uniform vec4 u_clip;
// The gloss: the fake lights of tu2_fx_gloss and the uber pass's point lights, Phong
// off the gloss map (Lights.fxo FXTU2Gloss, FXUberPointPassTU2). Lighting.md, "World specular"
#define PAINFUL_GLOSS_LIGHTS 5
SAMPLER2D(s_gloss, 10);
uniform vec4 u_eye;
uniform vec4 u_gloss; // x: power, y: how many, z: on for this batch, w: PAINFUL_GLOSSVIEW
uniform vec4 u_glossPos[PAINFUL_GLOSS_LIGHTS]; // xyz: position, w: 1/range (0 = no falloff)
uniform vec4 u_glossColor[PAINFUL_GLOSS_LIGHTS]; // rgb: colour x intensity, w: gain
uniform vec4 u_glossMask[PAINFUL_GLOSS_LIGHTS]; // x: added to the lightmap that masks it

void main()
{
	if (u_clip.y != 0.0 && (v_wpos.y - u_clip.w) * u_clip.y < 0.0) discard;
	// Each material slot carries its own UV transform in the .mpk (scale in
	// xy, offset in zw), the same job the engine's oT0 = v1 * c24/c25 texture
	// matrix does. pan[N] then scrolls that result over time.
	// (uv * slotXform + pan * t) * tile - tile is applied last, so it scales
	// the scroll too (Engine.dll 0x1009e5a0).
	vec2 uvDiffuse = (v_texcoord0 * u_uv0.xy + u_uv0.zw + u_uvanim.xy) * u_tile.xy;
	vec4 base = texture2D(s_diffuse, uvDiffuse);

	// Terrain blending: two TILED textures mixed by a mask that maps once
	// across the surface (so the mask rides the second UV set). The mask
	// value is colour times alpha, as in the sky compositor.
	if (u_params.z > 0.5)
	{
		vec2 uvBlend = (v_texcoord0 * u_uv1.xy + u_uv1.zw + u_uvanim.zw) * u_tile.zw;
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
		// Stage 1 is the lightmap on tasmashape (map[1] = $lightmap) but the
		// blendmap on the lava shaders (map[3] = $lightmap there), so the
		// stage-1 pan only belongs here when the material is not blended.
		vec2 lightPan = (u_params.z > 0.5) ? vec2(0.0, 0.0) : u_uvanim.zw;
		light = texture2D(s_lightmap, v_texcoord1 + lightPan).rgb * u_ambient.w;
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
	// The M key's lighting-only view: grey albedo, the texture's alpha still tests.
	if (u_params.w > 0.5) albedo = vec3_splat(0.8);

	// No ambient: defaultTU2 is `lighting false`, so the lightmap is the only
	// light term. o.Ambient drives the vertex lighting the MODELS use (c11).
	//
	// Unlightmapped objects are defaultNTU - vertex-lit like a model - and are
	// left at full albedo rather than going black; only the dynamic lights
	// below reach them.
	// The dynamic lights, added over the lightmap - the same call the models
	// make, with no specular: the world's comes off the gloss map, below -
	// and, from the placed lights the lightmap already holds, what a model's
	// shadow takes away.
	vec3 lit = vec3_splat(0.0);
	vec3 unusedSpec = vec3_splat(0.0);
	float unusedShadow = 1.0;
	vec3 occluded = vec3_splat(0.0);
	DynamicLights(v_wpos, normalize(v_normal), vec3_splat(0.0), vec3_splat(0.0), vec4_splat(0.0),
			lit, unusedSpec, unusedShadow, occluded);
	vec3 lightShadowed = max(light - occluded, vec3_splat(0.0));

	// The character shadows over the baked light, each as strong as its own
	// caster's directional. The world's own shadows are in the lightmap
	// already, so this only darkens.
	float modelShadow = 1.0;
	if (u_charShadowInfo.x > 0.0) modelShadow = CharacterShadows(v_wpos, normalize(v_normal));
	// PAINFUL_SHADOWVIEW: the terms alone, as applied.
	if (u_charShadowInfo.w > 0.5)
	{
		float kept = dot(lightShadowed, vec3(0.299, 0.587, 0.114)) /
				max(dot(light, vec3(0.299, 0.587, 0.114)), 0.0001);
		gl_FragColor = vec4(vec3_splat(modelShadow * kept), 1.0);
		return;
	}
	light = lightShadowed * modelShadow;
	vec3 color = albedo * light;
	color += albedo * lit;

	// The raw lightmap masks the highlight - the shadowed one, so a model's
	// shadow takes the highlight with the light.
	if (u_gloss.w > 0.5) color = vec3_splat(0.0);
	if (u_gloss.z > 0.5)
	{
		vec3 gn = normalize(v_normal);
		vec3 r = reflect(normalize(v_wpos - u_eye.xyz), gn);
		vec3 lm = light / max(u_ambient.w, 0.0001);
		vec3 glossSum = vec3_splat(0.0);
		for (int i = 0; i < PAINFUL_GLOSS_LIGHTS; ++i)
		{
			if (float(i) >= u_gloss.y) break;
			vec3 toLight = u_glossPos[i].xyz - v_wpos;
			float d = length(toLight);
			vec3 l = toLight / max(d, 0.0001);
			float att = clamp(1.0 - d * u_glossPos[i].w, 0.0, 1.0);
			float s = pow(max(dot(r, l), 0.000001), u_gloss.x) * clamp(dot(l, gn), 0.0, 1.0) * att;
			glossSum += u_glossColor[i].rgb * (u_glossColor[i].w * s) *
					clamp(lm + vec3_splat(u_glossMask[i].x), 0.0, 1.0);
		}
		color += texture2D(s_gloss, uvDiffuse).rgb * glossSum;
	}

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
