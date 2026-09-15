// Pf.RendererType 1's distance field on the GPU (Render/SdfField.h): the level cut
// into bricks of 8^3 voxels, only those holding a surface voxel stored, each voxel
// naming the nearest surface voxel in its brick; the list of surface voxels with
// their light; the sky. The includer defines the stages:
//   PAINFUL_SDF_MAP_STAGE, PAINFUL_SDF_BRICKS_STAGE, PAINFUL_SDF_LIST_STAGE, PAINFUL_SDF_SKY_STAGE
#ifndef PAINFUL_SHARED_SDF
#define PAINFUL_SHARED_SDF

#define SDF_BRICK 8

// Per brick: its slot in the atlas, or for a brick without a surface voxel
// -(1 + the distance in bricks from its centre to the nearest stored brick's).
SAMPLER3D(s_sdfMap, PAINFUL_SDF_MAP_STAGE);
// The stored bricks side by side, u_sdfCells.w along x and u_sdfAtlas.x along y:
// per voxel, the ordinal of the nearest surface voxel in its brick (r + 256 g).
SAMPLER3D(s_sdfBricks, PAINFUL_SDF_BRICKS_STAGE);
// Row-major: a texel per stored brick, the index of its first surface (rgb, 24
// bits); then six texels a surface: its voxel in the brick (rgb) and which bins
// had light (a, bit k), then six bins as rgb / 2 (+X -X +Y -Y +Z -Z), three
// bytes each running on over five texels.
SAMPLER2D(s_sdfList, PAINFUL_SDF_LIST_STAGE);
// The sky per direction, each texel averaged over a cone; latitude-longitude,
// row 0 straight up.
SAMPLER2D(s_sdfSky, PAINFUL_SDF_SKY_STAGE);

uniform vec4 u_sdfVolume; // xyz: voxel (0,0,0)'s corner, w: voxel size (0 not built)
uniform vec4 u_sdfExtent; // xyz: voxels along each axis, w: the list's width
uniform vec4 u_sdfCells; // xyz: bricks along each axis, w: the atlas's bricks along x
uniform vec4 u_sdfAtlas; // x: the atlas's bricks along y, y: stored bricks
uniform vec4 u_sdfSky; // x: 1 when the level has a sky
uniform vec4 u_sdfFog; // x: mode (0 none, 1 exp, 2 exp2, 3 linear), y: start, z: end, w: density
uniform vec4 u_sdfFogColor; // rgb: the fog's colour as light

// p in the volume's voxels.
vec3 SdfVoxels(vec3 p)
{
	return (p - u_sdfVolume.xyz) / u_sdfVolume.w;
}

bool SdfInside(vec3 q)
{
	return u_sdfVolume.w > 0.0 && all(greaterThanEqual(q, vec3_splat(0.0))) && all(lessThan(q, u_sdfExtent.xyz));
}

vec4 SdfListTexel(int i)
{
	int width = int(u_sdfExtent.w);
	return texelFetch(s_sdfList, ivec2(i - (i / width) * width, i / width), 0);
}

// Texel k of surface s.
vec4 SdfSurfaceTexel(int s, int k)
{
	return SdfListTexel(int(u_sdfAtlas.y) + s * 6 + k);
}

// The surface voxel nearest q within q's brick, and its centre in voxels; -1 in
// a brick without one, `bricks` then the distance to the nearest stored brick.
int SdfNearest(vec3 q, out vec3 centre, out float bricks)
{
	centre = vec3_splat(0.0);
	bricks = 0.0;
	ivec3 voxel = ivec3(clamp(floor(q), vec3_splat(0.0), u_sdfExtent.xyz - 1.0));
	ivec3 cell = voxel / SDF_BRICK;
	float m = texelFetch(s_sdfMap, cell, 0).r;
	if (m < 0.0)
	{
		bricks = -m - 1.0;
		return -1;
	}
	int slot = int(m + 0.5);
	int ax = int(u_sdfCells.w);
	int ay = int(u_sdfAtlas.x);
	int row = slot / ax;
	ivec3 brick = ivec3(slot - row * ax, row - (row / ay) * ay, slot / (ax * ay));
	vec2 ord = floor(texelFetch(s_sdfBricks, brick * SDF_BRICK + (voxel - cell * SDF_BRICK), 0).rg * 255.0 + 0.5);
	vec3 base = floor(SdfListTexel(slot).rgb * 255.0 + 0.5);
	int s = int(base.r + base.g * 256.0 + base.b * 65536.0 + ord.r + ord.g * 256.0);
	centre = vec3(cell * SDF_BRICK) + floor(SdfSurfaceTexel(s, 0).rgb * 255.0 + 0.5) + 0.5;
	return s;
}

vec3 SdfSafeInverse(vec3 d)
{
	return 1.0 / vec3(abs(d.x) < 1e-6 ? 1e-6 : d.x, abs(d.y) < 1e-6 ? 1e-6 : d.y, abs(d.z) < 1e-6 ? 1e-6 : d.z);
}

// A hit: the ray is in a surface voxel's cube, or enters it within half a voxel,
// having started outside it - the voxel a ray starts in (a vertex resting in a
// floor voxel) never stops it. `at` and `start` are the ray's point and origin
// from the cube's centre, in voxels; `enter` how far ahead the ray enters.
bool SdfHits(vec3 at, vec3 start, vec3 inv, out float enter)
{
	enter = 0.0;
	if (all(lessThan(abs(start), vec3_splat(0.5)))) return false;
	vec3 ta = (-0.5 - at) * inv;
	vec3 tb = (0.5 - at) * inv;
	vec3 lo = min(ta, tb);
	vec3 hi = max(ta, tb);
	enter = max(max(lo.x, lo.y), lo.z);
	float leave = min(min(hi.x, hi.y), hi.z);
	return enter <= leave && leave > 0.0 && enter < 0.5;
}

// One ray from p along unit d to the first hit. In a stored brick it strides the
// distance to the brick's nearest surface voxel less one voxel (at least half; a
// cube's corner is 0.87 from its centre), never past the brick's far face, whose
// neighbour may hold a nearer one; through an empty brick, at least to its far
// face, and further while no stored brick can be near. Returns the surface hit (p
// where it enters), -1 when the ray leaves the volume, -2 out of steps.
int SdfMarch(inout vec3 p, vec3 d, int steps)
{
	vec3 inv = SdfSafeInverse(d);
	vec3 start = SdfVoxels(p);
	for (int i = 0; i < steps; ++i)
	{
		vec3 q = SdfVoxels(p);
		if (!SdfInside(q)) return -1;
		vec3 brickLo = floor(q / 8.0) * 8.0;
		vec3 faces = max((brickLo - q) * inv, (brickLo + 8.0 - q) * inv);
		float toFace = min(min(faces.x, faces.y), faces.z) + 0.01;
		vec3 centre;
		float bricks;
		int s = SdfNearest(q, centre, bricks);
		float stride;
		if (s >= 0)
		{
			float enter;
			if (SdfHits(q - centre, start - centre, inv, enter))
			{
				p += d * (max(enter, 0.0) * u_sdfVolume.w);
				return s;
			}
			stride = min(max(length(centre - q) - 1.0, 0.5), toFace);
		}
		else
		{
			// No surface voxel within (bricks - 0.87) bricks of this brick's centre.
			stride = max((bricks - 0.87) * 8.0 - length(q - brickLo - 4.0) - 1.0, toFace);
		}
		p += d * (stride * u_sdfVolume.w);
	}
	return -2;
}

float SdfBit(float flags, float bit)
{
	return mod(floor(flags / bit), 2.0);
}

// The light surface s sends back along d: its bins facing the ray, by how
// squarely. Seen only from behind it sends nothing, front 0.
vec3 SdfSurfaceLight(int s, vec3 d, out float front)
{
	float flags = floor(SdfSurfaceTexel(s, 0).a * 255.0 + 0.5);
	float w0 = max(-d.x, 0.0) * SdfBit(flags, 1.0);
	float w1 = max(d.x, 0.0) * SdfBit(flags, 2.0);
	float w2 = max(-d.y, 0.0) * SdfBit(flags, 4.0);
	float w3 = max(d.y, 0.0) * SdfBit(flags, 8.0);
	float w4 = max(-d.z, 0.0) * SdfBit(flags, 16.0);
	float w5 = max(d.z, 0.0) * SdfBit(flags, 32.0);
	float w = w0 + w1 + w2 + w3 + w4 + w5;
	front = 0.0;
	if (w <= 0.0) return vec3_splat(0.0);
	front = 1.0;
	vec4 t1 = SdfSurfaceTexel(s, 1);
	vec4 t2 = SdfSurfaceTexel(s, 2);
	vec4 t3 = SdfSurfaceTexel(s, 3);
	vec4 t4 = SdfSurfaceTexel(s, 4);
	vec4 t5 = SdfSurfaceTexel(s, 5);
	vec3 sum = t1.rgb * w0 + vec3(t1.a, t2.rg) * w1 + vec3(t2.ba, t3.r) * w2 + t3.gba * w3 + t4.rgb * w4 +
			vec3(t4.a, t5.rg) * w5;
	return sum * (2.0 / w);
}

// The mean of every lit bin of surface s: what a ray that skims a surface
// without facing any side of it squarely takes.
vec3 SdfSurfaceLightAround(int s)
{
	float flags = floor(SdfSurfaceTexel(s, 0).a * 255.0 + 0.5);
	float w0 = SdfBit(flags, 1.0);
	float w1 = SdfBit(flags, 2.0);
	float w2 = SdfBit(flags, 4.0);
	float w3 = SdfBit(flags, 8.0);
	float w4 = SdfBit(flags, 16.0);
	float w5 = SdfBit(flags, 32.0);
	float w = w0 + w1 + w2 + w3 + w4 + w5;
	if (w <= 0.0) return vec3_splat(0.0);
	vec4 t1 = SdfSurfaceTexel(s, 1);
	vec4 t2 = SdfSurfaceTexel(s, 2);
	vec4 t3 = SdfSurfaceTexel(s, 3);
	vec4 t4 = SdfSurfaceTexel(s, 4);
	vec4 t5 = SdfSurfaceTexel(s, 5);
	vec3 sum = t1.rgb * w0 + vec3(t1.a, t2.rg) * w1 + vec3(t2.ba, t3.r) * w2 + t3.gba * w3 + t4.rgb * w4 +
			vec3(t4.a, t5.rg) * w5;
	return sum * (2.0 / w);
}

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

// The light one ray from `origin` along d brings: a surface's, fogged over the
// way to it; past the level, the sky or the fog; out of steps, the mean light
// of the surface it was skimming, fogged. `front` is 0 for a back face.
vec3 SdfRayLight(vec3 origin, vec3 d, int steps, out float front)
{
	front = 1.0;
	vec3 p = origin;
	int s = SdfMarch(p, d, steps);
	if (s >= 0) return SdfFogged(SdfSurfaceLight(s, d, front), distance(p, origin));
	if (s == -1) return SdfBackdrop(d);
	vec3 centre;
	float bricks;
	int near = SdfNearest(SdfVoxels(p), centre, bricks);
	vec3 light = near >= 0 ? SdfSurfaceLightAround(near) : vec3_splat(0.0);
	return SdfFogged(light, distance(p, origin));
}

#endif
