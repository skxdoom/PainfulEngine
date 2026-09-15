// Pf.RendererType 1: model vertices traced through the level's distance field
// (Render/SdfVertexLight.h). A vertex casts the rays of one fixed set over the
// sphere that face its normal, by shared_sdf.sh's march. Their cosine-weighted
// mean, irradiance / pi, blends into its history texel; a least-squares fit of
// the light from each direction, c0 + c1 . d a colour channel, is written whole
// for the sheen. The set never turns, so a still vertex traces the same light
// every time. Outside the volume: untraced.
#include <bgfx_compute.sh>

UIMAGE2D_RW(i_sdfVertexLight, r32ui, 0);
// Write-only: Direct3D 11 reads back single-channel images alone.
IMAGE2D_WO(i_sdfSheenR, rgba16f, 1);
IMAGE2D_WO(i_sdfSheenG, rgba16f, 2);
IMAGE2D_WO(i_sdfSheenB, rgba16f, 3);
SAMPLER2D(s_sdfJobPos, 4); // xyz: the vertex, w: its slot
SAMPLER2D(s_sdfJobNormal, 5); // xyz: its normal, w: 1 to forget its history

#define PAINFUL_SDF_MAP_STAGE 6
#define PAINFUL_SDF_BRICKS_STAGE 7
#define PAINFUL_SDF_LIST_STAGE 8
#define PAINFUL_SDF_SKY_STAGE 9
#include "shared_sdf.sh"

uniform vec4 u_sdfVertexJob; // x: vertices queued, y: the job textures' width, z: the history's width
uniform vec4 u_sdfVertexBlend; // x: a new trace's share

#define SDF_VERTEX_DIRECTIONS 32 // over the whole sphere; about half face a vertex
#define SDF_STEPS 256
#define SDF_LIGHT_RANGE 4.0 // a history texel's 10 bits a channel span 0..this
// Added to the fit's gradient terms: the rays cover only a hemisphere, so the
// gradient along the normal is weakly held apart from the mean.
#define SDF_SHEEN_RIDGE 0.5

NUM_THREADS(64, 1, 1)
void main()
{
	int i = int(gl_GlobalInvocationID.x);
	if (i >= int(u_sdfVertexJob.x)) return;
	int width = int(u_sdfVertexJob.y);
	ivec2 at = ivec2(i - (i / width) * width, i / width);
	vec4 job = texelFetch(s_sdfJobPos, at, 0);
	vec4 facing = texelFetch(s_sdfJobNormal, at, 0);
	int slot = int(job.w + 0.5);
	int side = int(u_sdfVertexJob.z);
	ivec2 cell = ivec2(slot - (slot / side) * side, slot / side);
	vec3 n = normalize(facing.xyz);
	vec3 origin = job.xyz + n * 0.05;
	if (!SdfInside(SdfVoxels(origin)))
	{
		imageStore(i_sdfVertexLight, cell, uvec4(0u, 0u, 0u, 0u));
		imageStore(i_sdfSheenR, cell, vec4_splat(0.0));
		imageStore(i_sdfSheenG, cell, vec4_splat(0.0));
		imageStore(i_sdfSheenB, cell, vec4_splat(0.0));
		return;
	}

	// Spherical Fibonacci directions in world space, the same for every vertex
	// and frame; the cosine weight fades one in and out as the normal turns.
	vec3 sum = vec3_splat(0.0);
	float weight = 0.0;
	// The fit's normal equations: A over (1, d.x, d.y, d.z), b = sum of light * that.
	float a00 = 0.0;
	float a01 = 0.0;
	float a02 = 0.0;
	float a03 = 0.0;
	float a11 = SDF_SHEEN_RIDGE;
	float a12 = 0.0;
	float a13 = 0.0;
	float a22 = SDF_SHEEN_RIDGE;
	float a23 = 0.0;
	float a33 = SDF_SHEEN_RIDGE;
	vec3 b0 = vec3_splat(0.0);
	vec3 b1 = vec3_splat(0.0);
	vec3 b2 = vec3_splat(0.0);
	vec3 b3 = vec3_splat(0.0);
	for (int r = 0; r < SDF_VERTEX_DIRECTIONS; ++r)
	{
		float y = 1.0 - (2.0 * float(r) + 1.0) / float(SDF_VERTEX_DIRECTIONS);
		float phi = 2.3999632 * float(r);
		float radius = sqrt(max(0.0, 1.0 - y * y));
		vec3 d = vec3(radius * cos(phi), y, radius * sin(phi));
		float w = dot(d, n);
		if (w > 0.0)
		{
			float front;
			vec3 light = SdfRayLight(origin, d, SDF_STEPS, front);
			sum += light * w;
			weight += w;
			a00 += 1.0;
			a01 += d.x;
			a02 += d.y;
			a03 += d.z;
			a11 += d.x * d.x;
			a12 += d.x * d.y;
			a13 += d.x * d.z;
			a22 += d.y * d.y;
			a23 += d.y * d.z;
			a33 += d.z * d.z;
			b0 += light;
			b1 += light * d.x;
			b2 += light * d.y;
			b3 += light * d.z;
		}
	}
	vec3 lit = weight > 0.0 ? sum / weight : vec3_splat(0.0);

	// The sheen fit: A c = b by Cholesky, one right-hand side a colour channel.
	float l00 = sqrt(max(a00, 1e-4));
	float l10 = a01 / l00;
	float l20 = a02 / l00;
	float l30 = a03 / l00;
	float l11 = sqrt(max(a11 - l10 * l10, 1e-4));
	float l21 = (a12 - l20 * l10) / l11;
	float l31 = (a13 - l30 * l10) / l11;
	float l22 = sqrt(max(a22 - l20 * l20 - l21 * l21, 1e-4));
	float l32 = (a23 - l30 * l20 - l31 * l21) / l22;
	float l33 = sqrt(max(a33 - l30 * l30 - l31 * l31 - l32 * l32, 1e-4));
	vec3 y0 = b0 / l00;
	vec3 y1 = (b1 - y0 * l10) / l11;
	vec3 y2 = (b2 - y0 * l20 - y1 * l21) / l22;
	vec3 y3 = (b3 - y0 * l30 - y1 * l31 - y2 * l32) / l33;
	vec3 c3 = y3 / l33;
	vec3 c2 = (y2 - c3 * l32) / l22;
	vec3 c1 = (y1 - c2 * l21 - c3 * l31) / l11;
	vec3 c0 = (y0 - c1 * l10 - c2 * l20 - c3 * l30) / l00;
	imageStore(i_sdfSheenR, cell, vec4(c0.r, c1.r, c2.r, c3.r));
	imageStore(i_sdfSheenG, cell, vec4(c0.g, c1.g, c2.g, c3.g));
	imageStore(i_sdfSheenB, cell, vec4(c0.b, c1.b, c2.b, c3.b));

	uint old = imageLoad(i_sdfVertexLight, cell).x;
	if (facing.w < 0.5 && (old >> 30u) != 0u)
	{
		vec3 before = vec3(float(old & 1023u), float((old >> 10u) & 1023u), float((old >> 20u) & 1023u)) *
				(SDF_LIGHT_RANGE / 1023.0);
		lit = mix(before, lit, u_sdfVertexBlend.x);
	}
	uvec3 q = uvec3(clamp(lit / SDF_LIGHT_RANGE, 0.0, 1.0) * 1023.0 + 0.5);
	imageStore(i_sdfVertexLight, cell, uvec4(q.x | (q.y << 10u) | (q.z << 20u) | (1u << 30u), 0u, 0u, 0u));
}
