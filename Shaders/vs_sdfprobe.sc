$input a_position, a_normal, a_texcoord1
$output v_normal, v_sdfLight

// pfsdfdebuggrid's spheres (Render/SdfDebug.h): a unit sphere scaled and moved
// by the transform, so its normal needs no transform at all, lit by the light
// traced at its own vertices.
#include <bgfx_shader.sh>

#define PAINFUL_SDF_VERTEX_STAGE 4
#include "shared_sdfvertex.sh"

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_normal = a_normal;
	v_sdfLight = SdfVertexLight(a_texcoord1.x);
}
