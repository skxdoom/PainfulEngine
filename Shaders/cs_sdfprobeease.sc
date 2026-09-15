// Pf.RendererType 1: what the models sample, eased toward the latest traced
// grid (Render/SdfProbes.h). Each cell starts from the probe the eased grid held
// at its place - scrolled when the grid moved - or, where that grid did not
// reach, from the coarser grid's light there (the trace itself with none), and
// moves a step toward the traced probe. A re-trace or a move so blends in over
// a few frames, whenever and however fast the data changes.
#include <bgfx_compute.sh>

IMAGE3D_WO(i_sdfShown, rgba16f, 0);
SAMPLER3D(s_sdfShownOld, 1); // the eased grid the models read this frame
SAMPLER3D(s_sdfTarget, 2); // the latest traced grid
SAMPLER3D(s_sdfCoarser, 3); // the next grid out, eased

uniform vec4 u_sdfEase; // xyz: a cell here plus this is the same place in the eased grid, w: the step, 0..1
uniform vec4 u_sdfEaseOld; // w: 1 when there is an eased grid to start from
uniform vec4 u_sdfCoarser; // xyz: cell (0,0,0) in the coarser grid's texels, w: cells per coarser texel (0 none)

#define PROBES 32
#define SLABS 7

NUM_THREADS(8, 8, 8)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	if (cell.x >= PROBES || cell.y >= PROBES || cell.z >= PROBES) return;
	ivec3 old = cell + ivec3(u_sdfEase.xyz);
	bool kept = u_sdfEaseOld.w > 0.5 && old.x >= 0 && old.y >= 0 && old.z >= 0 && old.x < PROBES &&
			old.y < PROBES && old.z < PROBES;
	vec3 g = u_sdfCoarser.xyz + vec3(cell) * u_sdfCoarser.w;
	float last = float(PROBES) - 0.5;
	bool coarse = u_sdfCoarser.w > 0.0 && g.x >= 0.5 && g.y >= 0.5 && g.z >= 0.5 && g.x <= last &&
			g.y <= last && g.z <= last;
	for (int s = 0; s < SLABS; ++s)
	{
		ivec3 slab = ivec3(0, 0, PROBES * s);
		vec4 target = texelFetch(s_sdfTarget, cell + slab, 0);
		vec4 from = target;
		if (kept)
		{
			from = texelFetch(s_sdfShownOld, old + slab, 0);
		}
		else if (coarse)
		{
			vec3 uvw = vec3(g.x, g.y, g.z + float(PROBES * s)) / vec3(float(PROBES), float(PROBES), float(PROBES * SLABS));
			from = texture3DLod(s_sdfCoarser, uvw, 0.0);
		}
		imageStore(i_sdfShown, cell + slab, mix(from, target, u_sdfEase.w));
	}
}
