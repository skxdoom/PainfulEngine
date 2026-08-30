$input v_color0, v_texcoord0

// One shader for every 2D draw. A solid quad is the same thing as a textured
// one with a 1x1 white texture bound, which keeps the whole HUD - panels,
// icons, glyphs - in a single batch and a single state.
//
// Text is drawn from an alpha-only glyph atlas expanded to RGBA at bake time,
// so it modulates exactly like an icon does.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);

void main()
{
	gl_FragColor = texture2D(s_diffuse, v_texcoord0) * v_color0;
}
