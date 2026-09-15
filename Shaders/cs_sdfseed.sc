// Pf.RendererType 1: a volume's surface voxels scattered from its list into a
// 3D texture (Render/SdfProbes.h) - each voxel's own packed coordinates, the
// distance transform's features, or its index in the list, for the light.
#include <bgfx_compute.sh>

IMAGE3D_WO(i_sdfSeed, r32f, 0);
// Seven texels a surface: its voxel (x, y, z), then the light bins.
SAMPLER2D(s_sdfList, 1);

uniform vec4 u_sdfSeed; // x: surfaces, y: 1 writes indices, z: the list's width

NUM_THREADS(64, 1, 1)
void main()
{
	int i = int(gl_GlobalInvocationID.x);
	if (i >= int(u_sdfSeed.x)) return;
	int width = int(u_sdfSeed.z);
	int t = i * 7;
	vec4 c = texelFetch(s_sdfList, ivec2(t - (t / width) * width, t / width), 0);
	ivec3 cell = ivec3(floor(c.xyz * 255.0 + 0.5));
	float packed = float(cell.x + cell.y * 256 + cell.z * 65536);
	imageStore(i_sdfSeed, cell, vec4_splat(u_sdfSeed.y > 0.5 ? float(i) : packed));
}
