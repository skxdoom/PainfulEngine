$input v_color0

// Flat vertex colour: this is a wireframe overlay, not part of the scene.
#include <bgfx_shader.sh>

void main()
{
	gl_FragColor = v_color0;
}
