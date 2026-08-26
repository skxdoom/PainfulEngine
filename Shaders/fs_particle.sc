$input v_color0, v_texcoord0

// The original modulates the sprite texture by a single per-particle D3DCOLOR
// (colour from the Color ramp, alpha from the fade curve) and nothing else -
// no lighting, no fog on the particle pass.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);

void main()
{
	gl_FragColor = texture2D(s_diffuse, v_texcoord0) * v_color0;
}
