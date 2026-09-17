$input v_texcoord0, v_normal, v_viewdist, v_wpos

// Demon Morph, a demonic model: palskinned_fresnel. The detail texture at
// the model's own uv, times a ramp looked up by the view-angle term
// palskin_fresnel.vso computes - (1 - cos 2theta)^2 * scale = 4 sin^4 * scale
// - here per pixel rather than per vertex. Fog as every model gets it.
// Docs/Reference/DemonFx.md, "The monsters".
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0); // special/fresnel_detail
SAMPLER2D(s_stage1, 1); // special/fresnel_func, a 512x1 ramp
uniform vec4 u_eye;
uniform vec4 u_fogColor;
uniform vec4 u_fog; // x: mode, y: start, z: end, w: density
// x = the fresnel scale (c11.z).
uniform vec4 u_demonFresnel;

void main()
{
	vec3 n = normalize(v_normal);
	vec3 v = normalize(u_eye.xyz - v_wpos);
	float c = clamp(dot(n, v), 0.0, 1.0);
	float s2 = 1.0 - c * c;
	float f = 4.0 * s2 * s2 * u_demonFresnel.x;
	vec3 detail = texture2D(s_diffuse, v_texcoord0).rgb;
	vec3 ramp = texture2D(s_stage1, vec2(clamp(f, 0.0, 1.0), 0.5)).rgb;
	vec3 color = detail * ramp;

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
	gl_FragColor = vec4(color, 1.0);
}
