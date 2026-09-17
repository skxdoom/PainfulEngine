$input v_viewdist

// A volume's nearest front and back face at each pixel, as depth over End clamped
// to 1 (fogatten is a linear ramp): r the front face, g the back face, b 0 where a
// back face lies (all min-blended); a 1 where a front face lies (max-blended).
// Render/VolumeRenderer.h
#include <bgfx_shader.sh>

uniform vec4 u_volume; // x: 1 / End

void main()
{
	float u = clamp(v_viewdist * u_volume.x, 0.0, 1.0);
	if (gl_FrontFacing) gl_FragColor = vec4(u, 1.0, 1.0, 1.0);
	else gl_FragColor = vec4(1.0, u, 0.0, 0.0);
}
