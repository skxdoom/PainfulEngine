$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_viewdist, v_wpos

// The model water's vertex half (palskin_water.vso): vs_entity plus the four
// impact ripples MDL.SetWaterImpact starts. Each is cos + sin of a phase that
// runs out from its centre, faded to nothing at its range, and pushed along the
// unnormalised normal, so the model's scale rides along as in the original.
// Docs/Reference/Water.md, "Impact ripples".
#include <bgfx_shader.sh>

// [0..3] centres; [4] time * speed, [5] 1 / period, [6] 1 / range, [7] amplitude
uniform vec4 u_waterImp[8];

void main()
{
	vec3 wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	vec3 wnormal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;

	vec4 d = vec4(length(wpos - u_waterImp[0].xyz), length(wpos - u_waterImp[1].xyz),
			length(wpos - u_waterImp[2].xyz), length(wpos - u_waterImp[3].xyz));
	// The phase wrapped to [-pi, pi), then the program's own series for cos and
	// sin (c5, c6), which drift from the true functions toward the ends.
	vec4 x = fract((d * u_waterImp[5] - u_waterImp[4]) * 0.15915494 + 0.5) * 6.2831855 - 3.1415927;
	vec4 x2 = x * x;
	vec4 x3 = x * x2;
	vec4 x4 = x2 * x2;
	vec4 x5 = x2 * x3;
	vec4 x6 = x3 * x3;
	vec4 x7 = x3 * x4;
	vec4 wave = 1.0 - 0.5 * x2 + 0.041666668 * x4 - 0.0013888889 * x6 +
			x - 0.16666667 * x3 + 0.0083333338 * x5 - 0.00019841270 * x7;
	wave *= max(1.0 - d * u_waterImp[6], 0.0) * u_waterImp[7];
	wpos += dot(wave, vec4(1.0, 1.0, 1.0, 1.0)) * wnormal;

	vec4 viewPos = mul(u_view, vec4(wpos, 1.0));
	gl_Position = mul(u_proj, viewPos);
	v_viewdist = length(viewPos.xyz);
	v_wpos = wpos;
	v_normal = normalize(wnormal);
	v_texcoord0 = a_texcoord0;
}
