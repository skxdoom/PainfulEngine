// Pf.RendererType 1: a box of one cascade's probe grid traced through the
// distance field (Render/SdfProbes.h). Each probe casts SDF_RAYS rays in a
// spherical Fibonacci set by shared_sdf.sh's march: a surface, in a cascade or
// the level field, gives its light, and a ray leaving the level the sky - one
// light, the lightmaps' and the sky's, with no box ambient in it. A probe most
// of whose rays saw back faces sits inside the level and is marked -1.
#include <bgfx_compute.sh>

// The image first: D3D11 has eight write slots a compute shader.
IMAGE3D_WO(i_sdfProbes, rgba16f, 0);

#define PAINFUL_SDF_FIELD_STAGE0 1
#define PAINFUL_SDF_FIELD_STAGE1 2
#define PAINFUL_SDF_FIELD_STAGE2 3
#define PAINFUL_SDF_LEVEL_STAGE 4
#define PAINFUL_SDF_SURFACE_STAGE0 5
#define PAINFUL_SDF_SURFACE_STAGE1 6
#define PAINFUL_SDF_SURFACE_STAGE2 7
#define PAINFUL_SDF_BINS_STAGE0 8
#define PAINFUL_SDF_BINS_STAGE1 9
#define PAINFUL_SDF_BINS_STAGE2 10
#define PAINFUL_SDF_SKY_STAGE 11
#define PAINFUL_SDF_LEVEL_SURFACE_STAGE 12
#define PAINFUL_SDF_LEVEL_BINS_STAGE 13
#include "shared_sdf.sh"

uniform vec4 u_sdfJob; // xyz: probe (0,0,0)'s centre, w: spacing
uniform vec4 u_sdfJobMin; // xyz: the first cell this dispatch traces
uniform vec4 u_sdfJobSize; // xyz: cells along each axis

#define SDF_RAYS 64 // SdfLighting::kProbeRays
#define SDF_STEPS 256

NUM_THREADS(8, 8, 1)
void main()
{
	ivec3 local = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 size = ivec3(u_sdfJobSize.xyz);
	if (local.x >= size.x || local.y >= size.y || local.z >= size.z) return;
	ivec3 cell = local + ivec3(u_sdfJobMin.xyz);
	int probes = int(SDF_PROBES);
	vec3 origin = u_sdfJob.xyz + vec3(cell) * u_sdfJob.w;

	vec3 sh[9];
	for (int k = 0; k < 9; ++k) sh[k] = vec3_splat(0.0);
	float backFaces = 0.0;
	float golden = 3.14159265 * (3.0 - sqrt(5.0));
	for (int r = 0; r < SDF_RAYS; ++r)
	{
		float y = 1.0 - (float(r) + 0.5) * 2.0 / float(SDF_RAYS);
		float radius = sqrt(max(0.0, 1.0 - y * y));
		float a = golden * float(r);
		vec3 d = vec3(cos(a) * radius, y, sin(a) * radius);
		vec3 p = origin;
		int v = SdfMarch(p, d, SDF_STEPS);
		vec3 light = vec3_splat(0.0);
		if (v >= 0)
		{
			float front;
			light = SdfFogged(SdfSurfaceLight(v, p, d, front), distance(p, origin));
			backFaces += 1.0 - front;
		}
		else if (v == -1)
		{
			// Out of the level: the sky, or the fog the frame is cleared to.
			light = SdfBackdrop(d);
		}
		else
		{
			// Out of steps, skimming a surface: that surface's light.
			int at = SdfVolumeAt(p);
			if (at >= 0) light = SdfSurfaceLightAround(at, p);
			light = SdfFogged(light, distance(p, origin));
		}
		for (int k = 0; k < 9; ++k) sh[k] += light * ShBasis(k, d);
	}
	// Monte Carlo over the sphere, then the cosine lobe per band.
	float scale = 4.0 * 3.14159265 / float(SDF_RAYS);
	for (int k = 0; k < 9; ++k)
	{
		float lobe = k == 0 ? 1.0 : (k < 4 ? 2.0 / 3.0 : 0.25);
		sh[k] *= scale * lobe;
	}
	float share = backFaces > float(SDF_RAYS) * 0.25 ? -1.0 : 0.0;

	imageStore(i_sdfProbes, cell, vec4(sh[0], sh[1].x));
	imageStore(i_sdfProbes, cell + ivec3(0, 0, probes), vec4(sh[1].yz, sh[2].xy));
	imageStore(i_sdfProbes, cell + ivec3(0, 0, probes * 2), vec4(sh[2].z, sh[3]));
	imageStore(i_sdfProbes, cell + ivec3(0, 0, probes * 3), vec4(sh[4], sh[5].x));
	imageStore(i_sdfProbes, cell + ivec3(0, 0, probes * 4), vec4(sh[5].yz, sh[6].xy));
	imageStore(i_sdfProbes, cell + ivec3(0, 0, probes * 5), vec4(sh[6].z, sh[7]));
	imageStore(i_sdfProbes, cell + ivec3(0, 0, probes * 6), vec4(sh[8], share));
}
