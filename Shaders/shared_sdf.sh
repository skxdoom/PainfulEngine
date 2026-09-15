// Pf.RendererType 1's distance field on the GPU (Render/SdfField.h): one volume
// over the whole level, each voxel naming its nearest surface voxel, the list of
// surface voxels with their light, and the sky. The includer defines the stages:
//   PAINFUL_SDF_INDEX_STAGE, PAINFUL_SDF_LIST_STAGE, PAINFUL_SDF_SKY_STAGE
#ifndef PAINFUL_SHARED_SDF
#define PAINFUL_SHARED_SDF

// Per voxel, the list index of its nearest surface voxel (-1 none).
SAMPLER3D(s_sdfIndex, PAINFUL_SDF_INDEX_STAGE);
// Seven RGBA8 texels a surface voxel, row-major: its voxel (the low 8 bits of x,
// y, z; a: their next 2 bits each), then six light bins as rgb / 2 (+X -X +Y -Y
// +Z -Z; a: had light).
SAMPLER2D(s_sdfList, PAINFUL_SDF_LIST_STAGE);
// The sky per direction, each texel averaged over a cone; latitude-longitude,
// row 0 straight up.
SAMPLER2D(s_sdfSky, PAINFUL_SDF_SKY_STAGE);

uniform vec4 u_sdfVolume; // xyz: voxel (0,0,0)'s corner, w: voxel size (0 not built)
uniform vec4 u_sdfExtent; // xyz: voxels along each axis, w: the list's width
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

// The nearest surface voxel to the voxel holding q; -1 none.
int SdfNearest(vec3 q)
{
	ivec3 cell = ivec3(clamp(floor(q), vec3_splat(0.0), u_sdfExtent.xyz - 1.0));
	float s = texelFetch(s_sdfIndex, cell, 0).r;
	return s < 0.0 ? -1 : int(s + 0.5);
}

vec4 SdfListTexel(int s, int k)
{
	int i = s * 7 + k;
	int width = int(u_sdfExtent.w);
	return texelFetch(s_sdfList, ivec2(i - (i / width) * width, i / width), 0);
}

// The centre of surface voxel s, in voxels.
vec3 SdfSurfaceCentre(int s)
{
	vec4 t = floor(SdfListTexel(s, 0) * 255.0 + 0.5);
	vec3 high = vec3(mod(t.a, 4.0), mod(floor(t.a / 4.0), 4.0), mod(floor(t.a / 16.0), 4.0));
	return t.xyz + high * 256.0 + 0.5;
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

// One ray from p along unit d to the first hit, stepping the distance to the
// nearest surface voxel less one voxel (at least half; a cube's corner is 0.87
// from its centre). Returns the surface hit (p where it enters), -1 when the ray
// leaves the volume, -2 out of steps.
int SdfMarch(inout vec3 p, vec3 d, int steps)
{
	vec3 inv = SdfSafeInverse(d);
	vec3 start = SdfVoxels(p);
	for (int i = 0; i < steps; ++i)
	{
		vec3 q = SdfVoxels(p);
		if (!SdfInside(q)) return -1;
		int s = SdfNearest(q);
		float stride = 16.0;
		if (s >= 0)
		{
			vec3 centre = SdfSurfaceCentre(s);
			float enter;
			if (SdfHits(q - centre, start - centre, inv, enter))
			{
				p += d * (max(enter, 0.0) * u_sdfVolume.w);
				return s;
			}
			stride = max(length(centre - q) - 1.0, 0.5);
		}
		p += d * (stride * u_sdfVolume.w);
	}
	return -2;
}

// The light surface s sends back along d: its bins facing the ray, by how
// squarely. Seen only from behind it sends nothing, front 0.
vec3 SdfSurfaceLight(int s, vec3 d, out float front)
{
	front = 0.0;
	vec3 sum = vec3_splat(0.0);
	float w = 0.0;
	for (int k = 0; k < 6; ++k)
	{
		vec4 bin = SdfListTexel(s, 1 + k);
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

// The mean of every lit bin of surface s: what a ray that skims a surface
// without facing any side of it squarely takes.
vec3 SdfSurfaceLightAround(int s)
{
	vec3 sum = vec3_splat(0.0);
	float w = 0.0;
	for (int k = 0; k < 6; ++k)
	{
		vec4 bin = SdfListTexel(s, 1 + k);
		if (bin.a > 0.5)
		{
			sum += bin.rgb * 2.0;
			w += 1.0;
		}
	}
	return w > 0.0 ? sum / w : vec3_splat(0.0);
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
	int near = SdfNearest(SdfVoxels(p));
	vec3 light = near >= 0 ? SdfSurfaceLightAround(near) : vec3_splat(0.0);
	return SdfFogged(light, distance(p, origin));
}

#endif
