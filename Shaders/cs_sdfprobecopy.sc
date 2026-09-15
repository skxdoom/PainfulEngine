// Pf.RendererType 1: a probe grid whose cascade moved keeps the probes it still
// covers (Render/SdfProbes.h): each cell takes the published grid's probe at the
// same place; a cell it did not cover is cleared for cs_sdfprobe to trace.
#include <bgfx_compute.sh>

IMAGE3D_WO(i_sdfProbes, rgba16f, 0);
SAMPLER3D(s_sdfOld, 1);

uniform vec4 u_sdfCopy; // xyz: a cell here plus this is the same place in the old grid

#define PROBES 32
#define SLABS 7

NUM_THREADS(8, 8, 8)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	if (cell.x >= PROBES || cell.y >= PROBES || cell.z >= PROBES) return;
	ivec3 old = cell + ivec3(u_sdfCopy.xyz);
	bool inside = old.x >= 0 && old.y >= 0 && old.z >= 0 && old.x < PROBES && old.y < PROBES && old.z < PROBES;
	for (int s = 0; s < SLABS; ++s)
	{
		vec4 slab = vec4_splat(0.0);
		if (inside) slab = texelFetch(s_sdfOld, old + ivec3(0, 0, PROBES * s), 0);
		imageStore(i_sdfProbes, cell + ivec3(0, 0, PROBES * s), slab);
	}
}
