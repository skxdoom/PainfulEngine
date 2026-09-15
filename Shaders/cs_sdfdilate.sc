// Pf.RendererType 1: a traced probe grid made ready to sample
// (Render/SdfProbes.h). A probe cs_sdfprobe marked -1 sits inside the level and
// takes the mean of its valid neighbours, so trilinear filtering never pulls a
// model's light toward the inside of a wall; with none, the box ambient.
#include <bgfx_compute.sh>

SAMPLER3D(s_sdfTraced, 0);
IMAGE3D_WO(i_sdfProbes, rgba16f, 1);

#define PROBES 32
#define SLABS 7

bool Valid(ivec3 cell)
{
	return texelFetch(s_sdfTraced, cell + ivec3(0, 0, PROBES * (SLABS - 1)), 0).a >= 0.0;
}

NUM_THREADS(8, 8, 8)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	if (cell.x >= PROBES || cell.y >= PROBES || cell.z >= PROBES) return;
	vec4 slab[SLABS];
	for (int s = 0; s < SLABS; ++s) slab[s] = vec4_splat(0.0);
	if (Valid(cell))
	{
		for (int s = 0; s < SLABS; ++s)
			slab[s] = texelFetch(s_sdfTraced, cell + ivec3(0, 0, PROBES * s), 0);
	}
	else
	{
		float count = 0.0;
		for (int dz = -1; dz <= 1; ++dz)
		for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
		{
			ivec3 at = cell + ivec3(dx, dy, dz);
			if (at.x < 0 || at.y < 0 || at.z < 0 || at.x >= PROBES || at.y >= PROBES || at.z >= PROBES)
				continue;
			if (!Valid(at)) continue;
			for (int s = 0; s < SLABS; ++s)
				slab[s] += texelFetch(s_sdfTraced, at + ivec3(0, 0, PROBES * s), 0);
			count += 1.0;
		}
		if (count > 0.0)
		{
			for (int s = 0; s < SLABS; ++s) slab[s] /= count;
		}
		else
		{
			slab[SLABS - 1].a = 1.0;
		}
	}
	for (int s = 0; s < SLABS; ++s)
		imageStore(i_sdfProbes, cell + ivec3(0, 0, PROBES * s), slab[s]);
}
