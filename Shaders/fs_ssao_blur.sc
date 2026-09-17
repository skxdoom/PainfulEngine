$input v_texcoord0

// SSAO's blur, one axis a pass (Render/Ssao.h): nine taps weighted 1 2 3 4 4 4 3 2 1,
// each weighted down by how far its view depth is from the centre's, so the
// occlusion keeps to its surface. Every offset modulo 4 sums to the same weight, so
// on a surface the two passes average the occlusion pass's 4x4 turn tile out
// exactly. g, the depth, passes through.
#include <bgfx_shader.sh>

SAMPLER2D(s_ao, 0);

uniform vec4 u_ssaoBlur; // xy: one texel along the pass, in uv

void main()
{
	vec4 centre = texture2D(s_ao, v_texcoord0);
	float sum = 0.0;
	float weight = 0.0;
	for (int k = -4; k <= 4; ++k)
	{
		vec4 tap = texture2D(s_ao, v_texcoord0 + u_ssaoBlur.xy * float(k));
		float g = min(min(float(k + 5), float(5 - k)), 4.0);
		float w = g * max(0.0, 1.0 - abs(tap.g - centre.g) / (0.05 * centre.g + 0.05));
		sum += tap.r * w;
		weight += w;
	}
	gl_FragColor = vec4(sum / weight, centre.g, 0.0, 1.0);
}
