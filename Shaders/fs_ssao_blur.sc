$input v_texcoord0

// SSAO's blur, one axis a pass (Render/Ssao.h): seven taps, each weighted down by
// how far its view depth is from the centre's, so the occlusion keeps to its
// surface. g, the depth, passes through.
#include <bgfx_shader.sh>

SAMPLER2D(s_ao, 0);

uniform vec4 u_ssaoBlur; // xy: one texel along the pass, in uv

void main()
{
	vec4 centre = texture2D(s_ao, v_texcoord0);
	float sum = centre.r * 0.2270;
	float weight = 0.2270;
	for (int k = 1; k <= 3; ++k)
	{
		float g = k == 1 ? 0.1945 : (k == 2 ? 0.1216 : 0.0540);
		for (int side = 0; side < 2; ++side)
		{
			vec2 at = v_texcoord0 + u_ssaoBlur.xy * (side == 0 ? float(k) : -float(k));
			vec4 tap = texture2D(s_ao, at);
			float w = g * max(0.0, 1.0 - abs(tap.g - centre.g) / (0.05 * centre.g + 0.05));
			sum += tap.r * w;
			weight += w;
		}
	}
	gl_FragColor = vec4(sum / weight, centre.g, 0.0, 1.0);
}
