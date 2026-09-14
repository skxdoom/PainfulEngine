$input v_texcoord0, v_normal, v_viewdist, v_wpos

// A model's water: skin.shader's palskinned_water / dirtywater, which is
// palskin_water.vso and skin_water.pso / skin_dirtywater.pso decoded - two
// looks into $envcubemap, a refracted and a reflected one, lerped by a
// fresnel and tinted, the dirty variant then mixing the colour map in by
// its alpha. RenderDefault (0x100041d0) uploads c11 = (Refract, Refract^2,
// Fresnel, 2) and the tints as (Refl - Refr, 1), (Refr, 0), from
// MDL.SetMaterialRefractFresnel. Docs/Reference/Water.md, "Swamp".
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
SAMPLERCUBE(s_envcube, 1);

uniform vec4 u_uvanim; // xy: stage-0 scroll offset
uniform vec4 u_uv0; // slot UV transform: scale xy, offset zw
uniform vec4 u_tile; // xy: stage-0 tiling
uniform vec4 u_eye; // xyz: camera position in world space
uniform vec4 u_fogColor;
uniform vec4 u_fog; // x: mode, y: start, z: end, w: density
uniform vec4 u_entWater; // x Refract, y Refract^2, z Fresnel, w 1 = dirty (colour map by alpha)
uniform vec4 u_entWaterRefl; // rgb ReflTint
uniform vec4 u_entWaterRefr; // rgb RefrTint

void main()
{
	vec3 n = normalize(v_normal);
	vec3 e = normalize(u_eye.xyz - v_wpos);
	vec3 i = -e;
	// The refracted look, as the vertex program writes it: T = eta I + (eta (n.I)
	// + sqrt(1 - eta^2 (1 - (n.I)^2))) n, the root taken of the magnitude as
	// the hardware's rsq did.
	float ndi = dot(n, i);
	float k = u_entWater.x * ndi + sqrt(abs(1.0 - u_entWater.y * (1.0 - ndi * ndi)));
	vec3 refr = u_entWater.x * i + k * n;
	// The reflected one, R = 2 (n.e) n - e.
	vec3 refl = 2.0 * dot(n, e) * n - e;
	// f = Fresnel (1 - e.R)^2, clamped by the colour register it rides in.
	float t = 1.0 - dot(e, refl);
	float f = clamp(u_entWater.z * t * t, 0.0, 1.0);

	// The lookups negate z, as palskin_water writes oT1.z / oT2.z: the cube
	// faces are laid out the D3D way, and the world is right-handed.
	vec3 color = mix(textureCube(s_envcube, vec3(refr.x, refr.y, -refr.z)).rgb,
			textureCube(s_envcube, vec3(refl.x, refl.y, -refl.z)).rgb, f);
	color *= mix(u_entWaterRefr.rgb, u_entWaterRefl.rgb, f);
	if (u_entWater.w > 0.5)
	{
		vec2 uv = ((v_texcoord0 * u_uv0.xy + u_uv0.zw) + u_uvanim.xy) * u_tile.xy;
		vec4 base = texture2D(s_diffuse, uv);
		color = mix(color, base.rgb, base.a);
	}

	float fog = 1.0;
	if (u_fog.x > 2.5) fog = (u_fog.z - v_viewdist) / max(u_fog.z - u_fog.y, 0.001);
	else if (u_fog.x > 1.5) { float fd = u_fog.w * v_viewdist; fog = exp(-fd * fd); }
	else if (u_fog.x > 0.5) fog = exp(-u_fog.w * v_viewdist);
	color = mix(u_fogColor.rgb, color, clamp(fog, 0.0, 1.0));

	gl_FragColor = vec4(color, 1.0);
}
