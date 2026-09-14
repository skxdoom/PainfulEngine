$input v_texcoord0

// pfsdfdebugclipmaps: SdfLighting's cascades raymarched per pixel with the CPU
// trace's own rule - each step in the finest cascade holding the point, the
// distance to its nearest surface voxel less one voxel (at least half), never
// past that cascade's face, a hit within one voxel - and the hit coloured with
// the light bin facing the eye. Render/SdfClipmapDebug.h
#include <bgfx_shader.sh>

// Per cascade: voxels to the nearest surface voxel's centre x 63.75, that
// surface's index (-1 none), and six light texels a surface (+X -X +Y -Y +Z -Z;
// a: had light).
SAMPLER3D(s_sdfDistance0, 0);
SAMPLER3D(s_sdfSurface0, 1);
SAMPLER2D(s_sdfBins0, 2);
SAMPLER3D(s_sdfDistance1, 3);
SAMPLER3D(s_sdfSurface1, 4);
SAMPLER2D(s_sdfBins1, 5);
SAMPLER3D(s_sdfDistance2, 6);
SAMPLER3D(s_sdfSurface2, 7);
SAMPLER2D(s_sdfBins2, 8);

uniform mat4 u_sdfInvViewProj;
uniform vec4 u_sdfEye; // xyz: the camera
uniform vec4 u_sdfWindow[3]; // per cascade, xyz: its corner, w: voxel size (0 not built)
uniform vec4 u_sdfGrid; // x: voxels a side, y: bins width
uniform vec4 u_sdfScreen; // xy: backbuffer size, z: 1 when its origin is bottom-left

float DistanceAt(int c, vec3 uvw)
{
	if (c == 0) return texture3DLod(s_sdfDistance0, uvw, 0.0).r;
	if (c == 1) return texture3DLod(s_sdfDistance1, uvw, 0.0).r;
	return texture3DLod(s_sdfDistance2, uvw, 0.0).r;
}

float SurfaceAt(int c, ivec3 v)
{
	if (c == 0) return texelFetch(s_sdfSurface0, v, 0).r;
	if (c == 1) return texelFetch(s_sdfSurface1, v, 0).r;
	return texelFetch(s_sdfSurface2, v, 0).r;
}

vec4 BinAt(int c, ivec2 p)
{
	if (c == 0) return texelFetch(s_sdfBins0, p, 0);
	if (c == 1) return texelFetch(s_sdfBins1, p, 0);
	return texelFetch(s_sdfBins2, p, 0);
}

// SdfLighting::Trace's hit colour: the bins facing back along the ray.
vec3 SurfaceLight(int c, int s, vec3 d)
{
	int width = int(u_sdfGrid.y);
	vec3 sum = vec3_splat(0.0);
	float w = 0.0;
	for (int k = 0; k < 6; ++k)
	{
		int i = s * 6 + k;
		vec4 bin = BinAt(c, ivec2(i - (i / width) * width, i / width));
		int a = k / 2;
		float sgn = (k - a * 2) == 0 ? 1.0 : -1.0;
		vec3 axis = vec3(a == 0 ? sgn : 0.0, a == 1 ? sgn : 0.0, a == 2 ? sgn : 0.0);
		float f = -dot(axis, d);
		if (f > 0.0 && bin.a > 0.5)
		{
			sum += bin.rgb * f;
			w += f;
		}
	}
	return w > 0.0 ? sum / w : vec3_splat(0.0);
}

// Entry and exit of cascade c's box along the ray; entry > exit misses.
vec2 BoxSpan(int c, vec3 eye, vec3 inv)
{
	vec3 lo = u_sdfWindow[c].xyz;
	vec3 hi = lo + vec3_splat(u_sdfGrid.x * u_sdfWindow[c].w);
	vec3 t0 = (lo - eye) * inv;
	vec3 t1 = (hi - eye) * inv;
	vec3 tmin = min(t0, t1);
	vec3 tmax = max(t0, t1);
	return vec2(max(max(tmin.x, tmin.y), max(tmin.z, 0.0)), min(min(tmax.x, tmax.y), tmax.z));
}

void main()
{
	vec2 frag = gl_FragCoord.xy / u_sdfScreen.xy;
	float ndcY = u_sdfScreen.z > 0.5 ? frag.y * 2.0 - 1.0 : 1.0 - frag.y * 2.0;
	vec4 farPoint = mul(u_sdfInvViewProj, vec4(frag.x * 2.0 - 1.0, ndcY, 1.0, 1.0));
	vec3 eye = u_sdfEye.xyz;
	vec3 dir = normalize(farPoint.xyz / farPoint.w - eye);
	vec3 safe = dir + vec3(abs(dir.x) < 1e-6 ? 1e-6 : 0.0, abs(dir.y) < 1e-6 ? 1e-6 : 0.0,
			abs(dir.z) < 1e-6 ? 1e-6 : 0.0);
	vec3 inv = 1.0 / safe;
	float n = u_sdfGrid.x;

	// Outside every cascade near black; inside one with nothing hit, dark blue.
	vec3 color = vec3(0.02, 0.02, 0.03);
	float t = 1e9;
	float tEnd = 0.0;
	for (int c = 0; c < 3; ++c)
	{
		if (u_sdfWindow[c].w <= 0.0) continue;
		vec2 span = BoxSpan(c, eye, inv);
		if (span.x < span.y)
		{
			t = min(t, span.x);
			tEnd = max(tEnd, span.y);
		}
	}
	if (t < tEnd)
	{
		color = vec3(0.04, 0.05, 0.10);
		for (int i = 0; i < 384; ++i)
		{
			if (t > tEnd) break;
			vec3 p = eye + dir * t;
			int c = -1;
			for (int k = 0; k < 3; ++k)
			{
				if (u_sdfWindow[k].w <= 0.0) continue;
				vec3 lo = u_sdfWindow[k].xyz;
				vec3 hi = lo + vec3_splat(n * u_sdfWindow[k].w);
				if (all(greaterThanEqual(p, lo)) && all(lessThan(p, hi)))
				{
					c = k;
					break;
				}
			}
			if (c < 0)
			{
				// Between cascades that do not nest: on to the next entry ahead.
				float next = 1e9;
				for (int k = 0; k < 3; ++k)
				{
					if (u_sdfWindow[k].w <= 0.0) continue;
					vec2 span = BoxSpan(k, eye, inv);
					if (span.x < span.y && span.x > t) next = min(next, span.x);
				}
				if (next > tEnd) break;
				t = next + 1e-3;
				continue;
			}
			float voxel = u_sdfWindow[c].w;
			vec3 lo = u_sdfWindow[c].xyz;
			vec3 q = (p - lo) / voxel;
			float d = DistanceAt(c, q / n) * 63.75;
			if (d < 1.0)
			{
				ivec3 v = ivec3(clamp(floor(q), vec3_splat(0.0), vec3_splat(n - 1.0)));
				float s = SurfaceAt(c, v);
				if (s >= 0.0) color = SurfaceLight(c, int(s + 0.5), dir);
				break;
			}
			vec3 hi = lo + vec3_splat(n * voxel);
			vec3 faces = (mix(lo, hi, step(vec3_splat(0.0), dir)) - p) * inv;
			float exit = min(min(faces.x, faces.y), faces.z);
			t += min(max(d - 1.0, 0.5) * voxel, exit + 0.01 * voxel);
		}
	}
	gl_FragColor = vec4(color, 1.0);
}
