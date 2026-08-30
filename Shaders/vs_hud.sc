$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

// The 2D layer. Positions arrive already in PIXELS - the scripts do their own
// layout against R3D.ScreenSize() - and an orthographic projection set by the
// renderer maps them straight to the screen, so there is no model matrix and
// nothing to transform per quad.
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0    = a_color0;
	v_texcoord0 = a_texcoord0;
}
