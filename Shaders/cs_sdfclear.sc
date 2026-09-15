// Pf.RendererType 1: a volume-sized R32F texture set to -1 before it is
// seeded (Render/SdfProbes.h).
#include <bgfx_compute.sh>

IMAGE3D_WO(i_sdfClear, r32f, 0);

uniform vec4 u_sdfEdt; // xyz: the volume's voxels along each axis

NUM_THREADS(8, 8, 8)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 dims = ivec3(u_sdfEdt.xyz);
	if (cell.x >= dims.x || cell.y >= dims.y || cell.z >= dims.z) return;
	imageStore(i_sdfClear, cell, vec4_splat(-1.0));
}
