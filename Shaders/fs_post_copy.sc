$input v_texcoord0

// A full-screen copy: the finished Demon Morph frame onto the backbuffer,
// after it was kept in a texture for next frame's trail (Render/DemonFx.cpp).
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);

void main()
{
	gl_FragColor = vec4(texture2D(s_scene, v_texcoord0).rgb, 1.0);
}
