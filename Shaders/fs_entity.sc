$input v_texcoord0, v_normal, v_viewdist

// The model lighting path, reproducing skin.shader's `palskinned`:
//
//     texop[0]  texture modulate diffuse      albedo * (ambient + sum N.L)
//     specular  true                          + sum (N.H)^k, ADDED after
//
// The light set comes from World/Lighting.h - one directional from the level or
// the CEnvironment box the model stands in, plus up to four CLights picked by
// attenuated intensity. Their half-vectors were computed once for the whole
// model, exactly as Entity::ComputeVSLights does, which is what keeps the
// specular a broad sheen instead of a tight highlight.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
// The second stage the model materials layer over the lit result - the blood
// on gibs, the freeze rime. Its own UV: palskinned_bloody says `notexmatrix`,
// so the stage-0 pan/tile do not reach it.
SAMPLER2D(s_stage1, 1);

uniform vec4 u_params;      // y: alpha-test ref (<0 off)
uniform vec4 u_uvanim;      // xy: stage-0 scroll offset
uniform vec4 u_uv0;         // slot UV transform: scale xy, offset zw
uniform vec4 u_tile;        // xy: stage-0 tiling
uniform vec4 u_ambient;     // rgb: ambient for this model
uniform vec4 u_fogColor;
uniform vec4 u_fog;         // x: mode, y: start, z: end, w: density
// Four lights, three registers each, in the engine's own order - this is the
// c12..c23 block Entity::ComputeVSLights fills.
uniform vec4 u_lightColor[4];   // rgb: colour x intensity x attenuation
uniform vec4 u_lightDir[4];     // xyz: direction to the light, w: slot used
uniform vec4 u_lightHalf[4];    // xyz: half-vector, w: diffuse weight (0 = specular-only)
uniform vec4 u_specular;        // x: exponent, y: strength, z: N.L gate softening
uniform vec4 u_stage1;          // x: op - 0 off, 1 modulate, 2 add, 3 modulatealphaadd

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

	// Ambient only. The environment's directional is not a term of its own -
	// Entity::ResetLights AddLight()s it, so it arrives as one of the four
	// slots below and casts specular like any other light. A model in an unlit
	// alcove is left with nothing but the ambient its CEnvironment gave it,
	// which is the whole point of those boxes.
	vec3 diffuse = u_ambient.rgb;

	// One `lit` per light, which is literally what palskin.bones1.lights2.vso
	// does: dp3 N.L, dp3 N.H, lit, then mad into oD0 and oD1.
	//
	//     lit.y = max(N.L, 0)
	//     lit.z = (N.L > 0 && N.H > 0) ? pow(N.H, power) : 0
	//
	// The gate on lit.z is the whole difference between a highlight and a
	// camera-facing wash: the half-vector here is (camera - entity) + lightDir
	// with the camera term unnormalised, so N.H tracks the VIEW far more than
	// the light. Ungated, every surface pointed at the player lights up
	// regardless of where the light is, which looks exactly inside-out.
	//
	// The N.L gate is a STEP, though, and the original gets away with it only
	// because it runs per vertex: oD1 is interpolated across the triangle, so
	// the discontinuity is smeared over however much N.L varies between three
	// corners. Per pixel the same step draws a hard line along the N.L = 0
	// contour - a clean diagonal across a shield, which is a triangle's worth
	// of interpolated normal. u_specular.z is that triangle's worth, put back:
	// ramp the gate over a small band of N.L instead of switching on it.
	vec3 specular = vec3_splat(0.0);
	for (int i = 0; i < 4; ++i)
	{
		if (u_lightDir[i].w < 0.5) continue;
		float ndotl = dot(n, u_lightDir[i].xyz);
		float ndoth = dot(n, u_lightHalf[i].xyz);
		diffuse += u_lightColor[i].rgb * max(ndotl, 0.0) * u_lightHalf[i].w;
		specular += u_lightColor[i].rgb * pow(max(ndoth, 0.0), u_specular.x) *
		            smoothstep(0.0, u_specular.z, ndotl);
	}

	// `texture modulate diffuse`, then `specular true` adds on top - the
	// specular is NOT modulated by the texture, which is what makes it read as
	// a sheen sitting over the material rather than part of it.
	vec3 color = base.rgb * diffuse + specular * u_specular.y;

	// The second stage combines with what is already there ("previous"), which
	// is why it sits after the lighting rather than being mixed into the
	// albedo: blood on a gib darkens the lit skin, it is not part of it.
	if (u_stage1.x > 0.5)
	{
		vec4 s1 = texture2D(s_stage1, v_texcoord0);
		if (u_stage1.x < 1.5)       color *= s1.rgb;              // modulate
		else if (u_stage1.x < 2.5)  color += s1.rgb;              // add
		else                        color = s1.rgb + s1.a * color; // modulatealphaadd
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
