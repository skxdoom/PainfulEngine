$input v_texcoord0

// One direction of the separable blur. The kernel arrives as (offset in
// texels, weight) pairs, mirrored about the centre tap, and u_bloomDir turns
// a texel into uv along this pass's axis. The weights carry the level's
// Multiplier, as gauss_fb1/fb2 did. Docs/Reference/Bloom.md, "The blur".
#include <bgfx_shader.sh>

#define BLOOM_PAIRS 16

SAMPLER2D(s_source, 0);
// xy = one texel along the pass axis, in uv; z = pairs in use.
uniform vec4 u_bloomDir;
// [i].x = offset of pair i in texels, [i].y = its weight; [0] is the centre.
uniform vec4 u_bloomKernel[BLOOM_PAIRS];

void main()
{
	vec2 uv = v_texcoord0;
	vec3 sum = texture2D(s_source, uv).rgb * u_bloomKernel[0].y;
	for (int i = 1; i < BLOOM_PAIRS; ++i)
	{
		vec2 d = u_bloomDir.xy * u_bloomKernel[i].x;
		sum += (texture2D(s_source, uv + d).rgb + texture2D(s_source, uv - d).rgb)
				* u_bloomKernel[i].y;
	}
	gl_FragColor = vec4(sum, 1.0);
}
