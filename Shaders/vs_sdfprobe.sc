$input a_position, a_normal
$output v_normal

// pfsdfdebug's probe spheres (Render/SdfDebug.h): a unit sphere scaled and
// moved by the transform, so its normal needs no transform at all.
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_normal = a_normal;
}
