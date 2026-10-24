$input v_texcoord0

// compose2.pso / compose4.pso: each stage times its weight, summed, each at
// its own uv offset (blur.vso's c10 + i). The stages are WORLD.SetRenderTarget's
// frames. Docs/Reference/FrameCompose.md
#include <bgfx_shader.sh>

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
uniform vec4 u_composeWeights;
uniform vec4 u_composeShift[2]; // stage 0 xy, stage 1 zw; stage 2 in [1].xy

void main()
{
	vec3 c = texture2D(s_tex0, v_texcoord0 + u_composeShift[0].xy).rgb * u_composeWeights.x
			+ texture2D(s_tex1, v_texcoord0 + u_composeShift[0].zw).rgb * u_composeWeights.y
			+ texture2D(s_tex2, v_texcoord0 + u_composeShift[1].xy).rgb * u_composeWeights.z;
	gl_FragColor = vec4(c, 1.0);
}
