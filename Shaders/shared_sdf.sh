// Pf.RendererType 1's distance field on the GPU (Render/SdfProbes.h): the three
// cascades about the camera (volumes 0..2) and the level-wide field behind them
// (volume 3), the march every ray takes through them, and the probe grids the
// models sample. The includer defines the stages of what it samples:
//   PAINFUL_SDF_FIELD_STAGE0..2, PAINFUL_SDF_LEVEL_STAGE       the fields and the march
//   PAINFUL_SDF_SURFACE_STAGE0..2, PAINFUL_SDF_BINS_STAGE0..2,
//   PAINFUL_SDF_LEVEL_SURFACE_STAGE, PAINFUL_SDF_LEVEL_BINS_STAGE    the light a ray hits
//   PAINFUL_SDF_SKY_STAGE                                      the sky a ray leaves into
//   PAINFUL_SDF_PROBE_STAGE0..2                                the probe grids
#ifndef PAINFUL_SHARED_SDF
#define PAINFUL_SHARED_SDF

#include "shared_sh.sh"

#define SDF_LEVEL 3
#define SDF_PROBES 32.0 // probes a side, every grid
#define SDF_SLABS 7.0 // RGBA texels a probe

#ifdef PAINFUL_SDF_FIELD_STAGE0
// Per voxel: rgb the unit direction to the nearest surface voxel's centre,
// a its distance in quarter voxels (1 none).
SAMPLER3D(s_sdfField0, PAINFUL_SDF_FIELD_STAGE0);
SAMPLER3D(s_sdfField1, PAINFUL_SDF_FIELD_STAGE1);
SAMPLER3D(s_sdfField2, PAINFUL_SDF_FIELD_STAGE2);
SAMPLER3D(s_sdfLevel, PAINFUL_SDF_LEVEL_STAGE);

uniform vec4 u_sdfVolume[4]; // xyz: voxel (0,0,0)'s corner, w: voxel size (0 not built)
uniform vec4 u_sdfExtent[4]; // xyz: voxels along each axis

bool SdfInside(int v, vec3 p)
{
	if (u_sdfVolume[v].w <= 0.0) return false;
	vec3 q = (p - u_sdfVolume[v].xyz) / u_sdfVolume[v].w;
	return all(greaterThanEqual(q, vec3_splat(0.0))) && all(lessThan(q, u_sdfExtent[v].xyz));
}

// The finest volume holding p; -1 outside them all.
int SdfVolumeAt(vec3 p)
{
	for (int v = 0; v < 4; ++v)
		if (SdfInside(v, p)) return v;
	return -1;
}

// At q, in volume v's voxels: xyz toward the nearest surface voxel, w voxels to it.
vec4 SdfFieldAt(int v, vec3 q)
{
	vec3 uvw = q / u_sdfExtent[v].xyz;
	vec4 f = vec4_splat(0.0);
	if (v == 0) f = texture3DLod(s_sdfField0, uvw, 0.0);
	else if (v == 1) f = texture3DLod(s_sdfField1, uvw, 0.0);
	else if (v == 2) f = texture3DLod(s_sdfField2, uvw, 0.0);
	else f = texture3DLod(s_sdfLevel, uvw, 0.0);
	return vec4(f.xyz * 2.0 - 1.0, f.w * 63.75);
}

vec3 SdfSafeInverse(vec3 d)
{
	return 1.0 / (d + vec3(abs(d.x) < 1e-6 ? 1e-6 : 0.0, abs(d.y) < 1e-6 ? 1e-6 : 0.0,
			abs(d.z) < 1e-6 ? 1e-6 : 0.0));
}

// World units volume v lets a ray at p along d step: the distance to the
// nearest surface voxel less one voxel (at least half), never past v's face.
float SdfStride(int v, vec3 p, vec3 inv, float voxels)
{
	vec4 vol = u_sdfVolume[v];
	vec3 hi = vol.xyz + u_sdfExtent[v].xyz * vol.w;
	vec3 faces = (mix(vol.xyz, hi, step(vec3_splat(0.0), inv)) - p) * inv;
	float exit = min(min(faces.x, faces.y), faces.z);
	return min(max(voxels - 1.0, 0.5) * vol.w, exit + 0.01 * vol.w);
}

// How squarely the nearest surface voxel lies ahead of a ray along d: the
// cosine between d and the way to it.
float SdfToward(vec4 f, vec3 d)
{
	return dot(f.xyz, d) / max(length(f.xyz), 1e-4);
}

// A hit: the ray passes within half a voxel of its nearest surface voxel's
// centre, ahead of it, or is already inside that voxel. A surface beside or
// behind the ray - the floor under a ray leaving it at a low angle - is not
// one; a 0.5-voxel stride cannot step over a voxel it approaches.
bool SdfHits(vec4 f, vec3 d)
{
	if (f.w < 0.35) return true;
	float toward = SdfToward(f, d);
	return f.w < 1.0 && toward > 0.0 && f.w * f.w * (1.0 - toward * toward) < 0.25;
}

// One ray from p along unit d, each step in the finest volume holding it, to
// the first hit. Returns the volume hit (p there), -1 when the ray leaves them
// all, -2 out of steps.
int SdfMarch(inout vec3 p, vec3 d, int steps)
{
	vec3 inv = SdfSafeInverse(d);
	for (int i = 0; i < steps; ++i)
	{
		int v = SdfVolumeAt(p);
		if (v < 0) return -1;
		vec4 f = SdfFieldAt(v, (p - u_sdfVolume[v].xyz) / u_sdfVolume[v].w);
		if (SdfHits(f, d)) return v;
		p += d * SdfStride(v, p, inv, f.w);
	}
	return -2;
}
#endif

#ifdef PAINFUL_SDF_SURFACE_STAGE0
// Per voxel, the list index of the surface voxel there (-1 elsewhere); the
// list, seven RGBA8 texels a surface, row-major: its voxel, then six light
// bins as rgb / 2 (+X -X +Y -Y +Z -Z; a: had light).
SAMPLER3D(s_sdfSurface0, PAINFUL_SDF_SURFACE_STAGE0);
SAMPLER3D(s_sdfSurface1, PAINFUL_SDF_SURFACE_STAGE1);
SAMPLER3D(s_sdfSurface2, PAINFUL_SDF_SURFACE_STAGE2);
SAMPLER3D(s_sdfLevelSurface, PAINFUL_SDF_LEVEL_SURFACE_STAGE);
SAMPLER2D(s_sdfBins0, PAINFUL_SDF_BINS_STAGE0);
SAMPLER2D(s_sdfBins1, PAINFUL_SDF_BINS_STAGE1);
SAMPLER2D(s_sdfBins2, PAINFUL_SDF_BINS_STAGE2);
SAMPLER2D(s_sdfLevelBins, PAINFUL_SDF_LEVEL_BINS_STAGE);

uniform vec4 u_sdfBins; // x: the list's width

// The surface voxel nearest `cell`, from the field's own texel there.
ivec3 SdfNearestVoxel(int v, ivec3 cell)
{
	vec4 f = vec4_splat(0.0);
	if (v == 0) f = texelFetch(s_sdfField0, cell, 0);
	else if (v == 1) f = texelFetch(s_sdfField1, cell, 0);
	else if (v == 2) f = texelFetch(s_sdfField2, cell, 0);
	else f = texelFetch(s_sdfLevel, cell, 0);
	return ivec3(floor(vec3(cell) + (f.xyz * 2.0 - 1.0) * (f.w * 63.75) + 0.5));
}

float SdfSurfaceIndex(int v, ivec3 cell)
{
	if (v == 0) return texelFetch(s_sdfSurface0, cell, 0).r;
	if (v == 1) return texelFetch(s_sdfSurface1, cell, 0).r;
	if (v == 2) return texelFetch(s_sdfSurface2, cell, 0).r;
	return texelFetch(s_sdfLevelSurface, cell, 0).r;
}

vec4 SdfBin(int v, int i)
{
	int width = int(u_sdfBins.x);
	ivec2 at = ivec2(i - (i / width) * width, i / width);
	if (v == 0) return texelFetch(s_sdfBins0, at, 0);
	if (v == 1) return texelFetch(s_sdfBins1, at, 0);
	if (v == 2) return texelFetch(s_sdfBins2, at, 0);
	return texelFetch(s_sdfLevelBins, at, 0);
}

// The list index of the surface voxel nearest p in volume v; -1 none.
float SdfSurfaceAt(int v, vec3 p)
{
	vec3 q = (p - u_sdfVolume[v].xyz) / u_sdfVolume[v].w;
	ivec3 cell = ivec3(clamp(floor(q), vec3_splat(0.0), u_sdfExtent[v].xyz - 1.0));
	ivec3 at = SdfNearestVoxel(v, cell);
	ivec3 dims = ivec3(u_sdfExtent[v].xyz);
	if (at.x < 0 || at.y < 0 || at.z < 0 || at.x >= dims.x || at.y >= dims.y || at.z >= dims.z)
		return -1.0;
	return SdfSurfaceIndex(v, at);
}

// The light volume v's surface at p sends back along d: its bins facing the
// ray, by how squarely. Seen only from behind it sends nothing, front 0.
vec3 SdfSurfaceLight(int v, vec3 p, vec3 d, out float front)
{
	front = 0.0;
	float s = SdfSurfaceAt(v, p);
	if (s < 0.0) return vec3_splat(0.0);
	vec3 sum = vec3_splat(0.0);
	float w = 0.0;
	for (int k = 0; k < 6; ++k)
	{
		vec4 bin = SdfBin(v, int(s + 0.5) * 7 + 1 + k);
		int a = k / 2;
		float sgn = (k - a * 2) == 0 ? 1.0 : -1.0;
		vec3 axis = vec3(a == 0 ? sgn : 0.0, a == 1 ? sgn : 0.0, a == 2 ? sgn : 0.0);
		float f = -dot(axis, d);
		if (f > 0.0 && bin.a > 0.5)
		{
			sum += bin.rgb * (2.0 * f);
			w += f;
		}
	}
	if (w <= 0.0) return vec3_splat(0.0);
	front = 1.0;
	return sum / w;
}

// The mean of every lit bin of the surface voxel nearest p: what a ray that
// skims a surface without facing any side of it squarely takes.
vec3 SdfSurfaceLightAround(int v, vec3 p)
{
	float s = SdfSurfaceAt(v, p);
	if (s < 0.0) return vec3_splat(0.0);
	vec3 sum = vec3_splat(0.0);
	float w = 0.0;
	for (int k = 0; k < 6; ++k)
	{
		vec4 bin = SdfBin(v, int(s + 0.5) * 7 + 1 + k);
		if (bin.a > 0.5)
		{
			sum += bin.rgb * 2.0;
			w += 1.0;
		}
	}
	return w > 0.0 ? sum / w : vec3_splat(0.0);
}
#endif

#ifdef PAINFUL_SDF_SKY_STAGE
// The sky per direction, each texel averaged over a probe ray's cone;
// latitude-longitude, row 0 straight up. And the level's fog.
SAMPLER2D(s_sdfSky, PAINFUL_SDF_SKY_STAGE);

uniform vec4 u_sdfSky; // x: 1 when the level has a sky
uniform vec4 u_sdfFog; // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density
uniform vec4 u_sdfFogColor; // rgb: the fog's colour as light

vec3 SdfSky(vec3 d)
{
	vec2 uv = vec2(atan2(d.z, d.x) / 6.28318530 + 0.5, 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.14159265);
	return texture2DLod(s_sdfSky, uv, 0.0).rgb;
}

// What a ray that leaves the level sees: the sky, which the renderer draws
// without fog; on a level without one the frame's clear colour, which is the
// fog's when the level has fog (GameApp's SetClearColor) and black otherwise.
vec3 SdfBackdrop(vec3 d)
{
	if (u_sdfSky.x > 0.5) return SdfSky(d);
	return u_sdfFog.x > 0.5 ? u_sdfFogColor.rgb : vec3_splat(0.0);
}

// Light from `dist` away through the level's fog: the renderer's own curve of
// distance (fs_entity), the rest of the way lit by the fog's colour.
vec3 SdfFogged(vec3 light, float dist)
{
	float clear = 1.0;
	if (u_sdfFog.x > 2.5)
	{
		clear = (u_sdfFog.z - dist) / max(u_sdfFog.z - u_sdfFog.y, 0.001);
	}
	else if (u_sdfFog.x > 1.5)
	{
		float fd = u_sdfFog.w * dist;
		clear = exp(-fd * fd);
	}
	else if (u_sdfFog.x > 0.5)
	{
		clear = exp(-u_sdfFog.w * dist);
	}
	return mix(u_sdfFogColor.rgb, light, clamp(clear, 0.0, 1.0));
}
#endif

#ifdef PAINFUL_SDF_PROBE_STAGE0
// Per cascade, SDF_PROBES^3 probes stacked in SDF_SLABS slabs along z: the 27
// SH values in order (coefficient k's channel c at k * 3 + c, the cosine lobe
// applied), then the share left to the box ambient (1 only where a buried probe
// had no valid neighbour). What the models read is each grid eased toward its
// latest trace (cs_sdfprobeease).
SAMPLER3D(s_sdfProbes0, PAINFUL_SDF_PROBE_STAGE0);
SAMPLER3D(s_sdfProbes1, PAINFUL_SDF_PROBE_STAGE1);
SAMPLER3D(s_sdfProbes2, PAINFUL_SDF_PROBE_STAGE2);

uniform vec4 u_sdfProbeGrid[3]; // xyz: probe (0,0,0)'s centre, w: spacing (0 not ready)

vec4 SdfProbeSlab(int c, vec3 g, float slab)
{
	vec3 uvw = vec3(g.x, g.y, g.z + slab * SDF_PROBES) / vec3(SDF_PROBES, SDF_PROBES, SDF_SLABS * SDF_PROBES);
	if (c == 0) return texture3DLod(s_sdfProbes0, uvw, 0.0);
	if (c == 1) return texture3DLod(s_sdfProbes1, uvw, 0.0);
	return texture3DLod(s_sdfProbes2, uvw, 0.0);
}

// Grid c's irradiance / pi at texel position g for normal n; w: the box's share.
vec4 SdfProbeIrradiance(int c, vec3 g, vec3 n)
{
	vec4 t0 = SdfProbeSlab(c, g, 0.0);
	vec4 t1 = SdfProbeSlab(c, g, 1.0);
	vec4 t2 = SdfProbeSlab(c, g, 2.0);
	vec4 t3 = SdfProbeSlab(c, g, 3.0);
	vec4 t4 = SdfProbeSlab(c, g, 4.0);
	vec4 t5 = SdfProbeSlab(c, g, 5.0);
	vec4 t6 = SdfProbeSlab(c, g, 6.0);
	vec3 sum = t0.rgb * ShBasis(0, n)
			+ vec3(t0.a, t1.r, t1.g) * ShBasis(1, n)
			+ vec3(t1.b, t1.a, t2.r) * ShBasis(2, n)
			+ t2.gba * ShBasis(3, n)
			+ t3.rgb * ShBasis(4, n)
			+ vec3(t3.a, t4.r, t4.g) * ShBasis(5, n)
			+ vec3(t4.b, t4.a, t5.r) * ShBasis(6, n)
			+ t5.gba * ShBasis(7, n)
			+ t6.rgb * ShBasis(8, n);
	return vec4(max(sum, vec3_splat(0.0)), clamp(t6.a, 0.0, 1.0));
}

// How much a grid placed at `grid` covers p: full from two probes inside its
// outer probe centres, none within half a probe of them. g: p in its texels.
float SdfGridWeight(vec4 grid, vec3 p, out vec3 g)
{
	g = vec3_splat(0.0);
	if (grid.w <= 0.0) return 0.0;
	g = (p - grid.xyz) / grid.w + 0.5;
	vec3 edge = min(g - 0.5, SDF_PROBES - 0.5 - g);
	return clamp((min(min(edge.x, edge.y), edge.z) - 0.5) / 2.0, 0.0, 1.0);
}

vec3 SdfGridLight(int c, vec3 g, vec3 n, vec3 box)
{
	vec4 irr = SdfProbeIrradiance(c, clamp(g, vec3_splat(0.5), vec3_splat(SDF_PROBES - 0.5)), n);
	return irr.rgb + box * irr.w;
}

// The traced ambient at p: the finest ready grid holding it, blended into the
// next over its outer two probes, and the box ambient past the grids. weight:
// the traced part.
vec3 SdfAmbient(vec3 p, vec3 n, vec3 box, float gain, out float weight)
{
	vec3 sum = vec3_splat(0.0);
	float remain = 1.0;
	for (int c = 0; c < 3; ++c)
	{
		vec3 g;
		float w = SdfGridWeight(u_sdfProbeGrid[c], p, g);
		if (w <= 0.0 || remain <= 0.001) continue;
		sum += SdfGridLight(c, g, n, box) * (remain * w);
		remain *= 1.0 - w;
	}
	weight = 1.0 - remain;
	return sum * gain + box * remain;
}
#endif

#endif
