$input v_texcoord0, v_normal, v_viewdist, v_wpos

// The model lighting path, reproducing skin.shader's `palskinned`:
//
//     texop[0]  texture modulate diffuse      albedo * (ambient + sum N.L)
//     specular  true                          + sum (N.H)^k, ADDED after
//
// Ambient and the one environment directional come from World/Lighting.h and
// are the original's. Everything POSITIONAL - a torch, a flash, the flashlight
// - goes through shared_lights.sh, the function the world mesh calls too, so a
// monster and the wall behind it are lit by identical arithmetic.
//
// That is a deliberate deviation. Entity::ComputeVSLights hands the vertex
// shader four lights already attenuated at the ENTITY ORIGIN, with no cookie
// and a cone edge the beam cannot follow, while the world gets a projected
// per-pixel one; the two never match, and a model under a flashlight reads as
// pasted on. Docs/Reference/Lighting.md, "Deviations".
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
// The second stage the model materials layer over the lit result - the blood
// on gibs, the freeze rime. Its own UV: palskinned_bloody says `notexmatrix`,
// so the stage-0 pan/tile do not reach it.
SAMPLER2D(s_stage1, 1);

#define PAINFUL_PROJ_STAGE 2
#define PAINFUL_PROJFALL_STAGE 3
#define PAINFUL_SHADOW_STAGE 4
#define PAINFUL_DIRSHADOW_STAGE 5
// The placed lights' shadow atlas: models only, so only this shader has it.
#define PAINFUL_LIGHTSHADOW_STAGE 6
#include "shared_lights.sh"

uniform vec4 u_params; // y: alpha-test ref (<0 off)
uniform vec4 u_uvanim; // xy: stage-0 scroll offset
uniform vec4 u_uv0; // slot UV transform: scale xy, offset zw
uniform vec4 u_tile; // xy: stage-0 tiling
uniform vec4 u_ambient; // rgb: ambient for this model
uniform vec4 u_fogColor;
uniform vec4 u_fog; // x: mode, y: start, z: end, w: density
// The environment directional: rgb colour x intensity, and the direction TO
// the light. It has no position and no falloff, so it is a term of its own
// rather than one of the slots.
uniform vec4 u_dirColor;
uniform vec4 u_dirDir;
uniform vec4 u_eye; // xyz: the camera, for the per-pixel half-vector
uniform vec4 u_specular; // x: exponent, y: strength, z: N.L gate softening
uniform vec4 u_stage1; // x: op - 0 off, 1 modulate, 2 add, 3 modulatealphaadd

void main()
{
	vec2 uv = (v_texcoord0 * u_uv0.xy + u_uv0.zw + u_uvanim.xy) * u_tile.xy;
	vec4 base = texture2D(s_diffuse, uv);
	if (u_params.y >= 0.0 && base.a <= u_params.y) discard;

	// NOT flipped for back faces. palskin has one normal per vertex and writes
	// one oD0/oD1 pair, so a `cull none` material shows its front-face lighting
	// on both windings. Turning the normal around on the back side is a
	// modern-looking improvement that the original does not make, and it reads
	// as inverted next to it.
	vec3 n = normalize(v_normal);

	// Ambient, plus the environment directional. A model in an unlit alcove is
	// left with nothing but the ambient its CEnvironment gave it, which is the
	// whole point of those boxes.
	float ndotl = max(dot(n, u_dirDir.xyz), 0.0);
	// Models cast the directional shadows and never receive them: the
	// original lit a model from its box alone, and receiving was tried and
	// judged not worth its artefacts. The placed lights' shadows they DO
	// receive, inside DynamicLights.
	vec3 diffuse = u_ambient.rgb + u_dirColor.rgb * ndotl;

	// The directional's specular, still `lit`-gated on N.L > 0 - a step in the
	// original, ramped here over u_specular.z, because per pixel the step
	// draws a hard line along the N.L = 0 contour.
	vec3 eyeDir = normalize(u_eye.xyz - v_wpos);
	vec3 specular = u_dirColor.rgb *
			pow(max(dot(n, normalize(u_dirDir.xyz + eyeDir)), 0.0), u_specular.x) *
			u_specular.y * smoothstep(0.0, u_specular.z, ndotl);

	// Everything positional, exactly as the world mesh gets it.
	float lightShadow = 1.0;
	vec3 unusedOccluded = vec3_splat(0.0);
	DynamicLights(v_wpos, n, u_eye.xyz, u_specular.xyz, diffuse, specular, lightShadow,
			unusedOccluded);
	// PAINFUL_SHADOWVIEW: models grey, darkened by the placed lights' term.
	if (u_dirShadowDir.w > 0.5) { gl_FragColor = vec4(vec3_splat(0.8 * lightShadow), 1.0); return; }

	// `texture modulate diffuse`, then `specular true` adds on top - the
	// specular is NOT modulated by the texture, which is what makes it read as
	// a sheen sitting over the material rather than part of it.
	vec3 color = base.rgb * diffuse + specular;

	// The second stage combines with what is already there ("previous"), which
	// is why it sits after the lighting rather than being mixed into the
	// albedo: blood on a gib darkens the lit skin, it is not part of it.
	if (u_stage1.x > 0.5)
	{
		vec4 s1 = texture2D(s_stage1, v_texcoord0);
		if (u_stage1.x < 1.5) color *= s1.rgb; // modulate
		else if (u_stage1.x < 2.5) color += s1.rgb; // add
		else color = s1.rgb + s1.a * color; // modulatealphaadd
	}

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
