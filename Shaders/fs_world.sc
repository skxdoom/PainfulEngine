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
#define PAINFUL_DIRSHADOW_STAGE 8
#include "shared_lights.sh"

// The CEnvironment boxes that overwrite the directional, outermost first, as
// the models blend them (EntityLighting::Evaluate): how strong the
// directional is HERE against the level's brightest, which is what a model's
// shadow on the world is scaled by. A box that says "shade" weakens both the
// term on the model and the shadow it throws, together.
uniform vec4 u_envCount; // x: how many, y: the factor outside every box
uniform vec4 u_envLo[PAINFUL_MAX_ENV]; // xyz: box min, w: blend margin
uniform vec4 u_envHi[PAINFUL_MAX_ENV]; // xyz: box max, w: the factor inside

float DirectionalFactor(vec3 p)
{
	float f = u_envCount.y;
	for (int i = 0; i < PAINFUL_MAX_ENV; ++i)
	{
		if (float(i) >= u_envCount.x) break;
		vec3 dlo = p - u_envLo[i].xyz;
		vec3 dhi = u_envHi[i].xyz - p;
		float inside = min(min(min(dlo.x, dlo.y), dlo.z), min(min(dhi.x, dhi.y), dhi.z));
		float w = clamp(inside / max(u_envLo[i].w, 0.0001), 0.0, 1.0);
		f = mix(f, u_envHi[i].w, w);
	}
	return f;
}

uniform vec4 u_params; // x: has lightmap, y: alpha-test ref (<0 off), z: terrain blend, w: unused
uniform vec4 u_uvanim; // xy: stage-0 scroll offset, zw: stage-1 scroll offset
uniform vec4 u_detail; // xy: detail tiling, z: detail on/off
uniform vec4 u_uv0; // diffuse slot UV transform: scale xy, offset zw
uniform vec4 u_tile; // tile[N]: xy stage 0, zw stage 1
uniform vec4 u_uv1; // blend slot UV transform: scale xy, offset zw
uniform vec4 u_ambient; // rgb: level ambient, w: lightmap scale (2 when Overbright)
uniform vec4 u_fogColor; // rgb: level fog colour
uniform vec4 u_fog; // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density

void main()
{
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

	// No ambient: defaultTU2 is `lighting false`, so the lightmap is the only
	// light term. o.Ambient drives the vertex lighting the MODELS use (c11).
	//
	// Unlightmapped objects are defaultNTU - vertex-lit like a model - and are
	// left at full albedo rather than going black; only the dynamic lights
	// below reach them.
	// The model shadows, laid over the baked light. The world's own shadows
	// are in the lightmap already, so this only darkens, by the strength set.
	// The model shadows over the baked light, as strong as the boxes say the
	// directional is here. The world's own shadows are in the lightmap
	// already, so this only darkens.
	float modelShadow = 1.0;
	if (u_dirShadowParams.x > 0.0)
	{
		modelShadow = mix(1.0, ModelShadow(v_wpos, normalize(v_normal)),
				u_dirShadowParams.x * DirectionalFactor(v_wpos));
	}
	// PAINFUL_SHADOWVIEW: the term alone, as applied.
	if (u_dirShadowDir.w > 0.5) { gl_FragColor = vec4(vec3_splat(modelShadow), 1.0); return; }
	light *= modelShadow;
	vec3 color = albedo * light;

	// The dynamic lights, added over the lightmap - the same call the models
	// make, with no specular, because the world's light passes have none.
	vec3 lit = vec3_splat(0.0);
	vec3 unusedSpec = vec3_splat(0.0);
	DynamicLights(v_wpos, normalize(v_normal), vec3_splat(0.0), vec3_splat(0.0),
			lit, unusedSpec);
	color += albedo * lit;

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
