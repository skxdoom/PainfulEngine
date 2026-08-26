$input v_texcoord0, v_texcoord1, v_wpos, v_viewdist

// Water, following the nv20 construction in water.shader. That is two passes:
//
//   pass 1   map[0] = $lightmap, blend none          - the lightmap alone
//   pass 2   blend modulate, map[0] = $normalmap,
//            map[3] = $cubemap, fshader = water_embm - reflection over the top
//
// and "blend modulate" is DESTCOLOR * ZERO, i.e. a multiply. Both passes are
// folded into this one shader, so the result is lightmap * reflection, which is
// what those two passes multiply out to.
//
// water_embm.pso is five instructions:
//
//     tex          t0             sample $normalmap at the tiled, panned UV
//     texm3x3pad   t1, t0_bx2     rows of the tangent-to-world 3x3, dotted
//     texm3x3pad   t2, t0_bx2       with the BIASED normal (2*n - 1)
//     texm3x3vspec t3, t0_bx2     ...then reflect the eye vector about it and
//                                 sample the cube map
//     mad          r0, t3, v0, v1  cube * diffuse + specular
//
// The tangent basis is constant, the surface being a flat horizontal plane, so
// the tangent-to-world transform is the usual "swap y and z" for a plane whose
// normal is +y. The diffuse and specular terms (v0, v1) come from a lit()
// chain over engine-side light constants that no shipped file carries, so they
// are not reproduced - only the reflection and the lightmap are.
#include <bgfx_shader.sh>

SAMPLER2D(s_normal,   0);
SAMPLERCUBE(s_cube,   1);
SAMPLER2D(s_lightmap, 2);

uniform vec4 u_tile;      // xy: stage 0 tile, zw: stage 1 tile
uniform vec4 u_uvanim;    // xy: stage 0 pan * t, zw: stage 1 pan * t
uniform vec4 u_eye;       // xyz: camera position in world space
uniform vec4 u_ambient;   // rgb: level ambient, w: lightmap scale
uniform vec4 u_fogColor;
uniform vec4 u_fog;       // x: mode, y: start, z: end, w: density
// o.Water from the level: x BumpHeight, y FresnelBias, z FresnelExponent,
// w ReflectionAmount.
uniform vec4 u_water;
uniform vec4 u_waterDeep;      // rgb DeepWaterColor, w WaterAmount
uniform vec4 u_waterShallow;   // rgb ShallowWaterColor

void main()
{
	// Same transform order as every other stage: (uv + pan * t) * tile.
	// ONE scrolling layer: the nv20 EMBM pass declares tile[0] and pan[0] and
	// nothing else. (The nv30 variant samples $normalmap twice with different
	// transforms, but that is a different construction with its own FX
	// program.) Shipped values are tile 17.5 10, pan 0.00172 0.003.
	vec2 uv0 = (v_texcoord0 + u_uvanim.xy) * u_tile.xy;

	// _bx2 in the pixel shader is the 2*x - 1 bias.
	//
	// ripples_00 is a WORLD-space normal map for a horizontal plane, not a
	// tangent-space one: sample it anywhere and green is pinned at 255 while
	// red sits near 128 and blue swings the full range. Green is the up axis,
	// so the texel is already (x, y, z) with y up and needs no basis change -
	// which is also why the 3x3 water_ref.vso builds is constant. Reading it as
	// tangent-space, with blue as up, swings the VERTICAL component across
	// -1..1 and the surface looks violently bumpy.
	//
	// BumpHeight scales the two horizontal components, i.e. how far the normal
	// tilts, which is the magnitude of the rows built as "c4.xyz - 1".
	vec3 nT = texture2D(s_normal, uv0).xyz * 2.0 - 1.0;
	vec3 n = normalize(vec3(nT.x * u_water.x, nT.y, nT.z * u_water.x));

	vec3 eye  = normalize(v_wpos - u_eye.xyz);
	vec3 refl = reflect(eye, n);
	vec3 color = textureCube(s_cube, refl).rgb;

	// STOPS HERE, deliberately.
	//
	// water_embm.pso ends "mad r0, t3, v0, v1": the cube scaled by a diffuse
	// term, plus a specular one. water_ref.vso builds both with a lit() chain
	// whose inputs are engine constants, and o.Water plainly supplies the
	// ingredients - FresnelBias, FresnelExponent, ReflectionAmount,
	// DeepWaterColor, ShallowWaterColor. What is NOT recoverable is which
	// property feeds which term: WorldMesh::RenderWater uploads those registers
	// from values computed out of a TWater struct, and the decompiler loses the
	// register numbers across that run of setter calls.
	//
	// Guessing the mapping was tried and produces water that is confidently
	// wrong - too dark, or a flat tint with the reflection swamped. So the
	// combine stays at what IS decoded: the reflection, times the lightmap the
	// first pass draws. The unused values are parsed and passed in (u_water,
	// u_waterDeep, u_waterShallow) ready for whoever pins the mapping down.

	// Pass 1 multiplied in.
	color *= texture2D(s_lightmap, v_texcoord1).rgb * u_ambient.w;

	// Fog matches the world pass; modes are CLevel.lua's.
	if (u_fog.x > 0.5)
	{
		float f = 1.0;
		if (u_fog.x < 1.5)      f = exp(-u_fog.w * v_viewdist);
		else if (u_fog.x < 2.5) f = exp(-u_fog.w * u_fog.w * v_viewdist * v_viewdist);
		else                    f = (u_fog.z - v_viewdist) / max(u_fog.z - u_fog.y, 0.001);
		color = mix(u_fogColor.rgb, color, clamp(f, 0.0, 1.0));
	}

	gl_FragColor = vec4(color, 1.0);
}
