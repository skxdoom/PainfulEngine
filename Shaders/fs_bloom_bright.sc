$input v_texcoord0

// The bright pass, Bloom.fxo's bright_pass ps_1_1 verbatim: the pixel is
// kept in proportion to how far its luminance sits over the level's
// LuminanceThreshold, doubled, then written to an 8-bit target - so the
// clamp at 1 is the original's too. Docs/Reference/Bloom.md, "Bright pass".
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
// x = LuminanceThreshold.
uniform vec4 u_bloomParams;

void main()
{
	vec3 c = texture2D(s_scene, v_texcoord0).rgb;
	float lum = dot(c, vec3(0.3, 0.6, 0.11));
	float w = 2.0 * max(lum - u_bloomParams.x, 0.0);
	gl_FragColor = vec4(min(c * w, vec3_splat(1.0)), 1.0);
}
