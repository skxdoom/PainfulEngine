// Pf.RendererType 1: model vertices traced through the level's distance field
// (Render/SdfVertexLight.h). A vertex casts the rays of one fixed set over the
// sphere that face its normal, by shared_sdf.sh's march, and their cosine-weighted
// mean, irradiance / pi, blends into its history texel. The set never turns, so a
// still vertex traces the same light every time. Outside the volume: untraced.
#include <bgfx_compute.sh>

UIMAGE2D_RW(i_sdfVertexLight, r32ui, 0);
SAMPLER2D(s_sdfJobPos, 1); // xyz: the vertex, w: its slot
SAMPLER2D(s_sdfJobNormal, 2); // xyz: its normal, w: 1 to forget its history

#define PAINFUL_SDF_INDEX_STAGE 3
#define PAINFUL_SDF_LIST_STAGE 4
#define PAINFUL_SDF_SKY_STAGE 5
#include "shared_sdf.sh"

uniform vec4 u_sdfVertexJob; // x: vertices queued, y: the job textures' width, z: the history's width
uniform vec4 u_sdfVertexBlend; // x: a new trace's share

#define SDF_VERTEX_DIRECTIONS 32 // over the whole sphere; about half face a vertex
#define SDF_STEPS 256
#define SDF_LIGHT_RANGE 4.0 // a history texel's 10 bits a channel span 0..this

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
		return;
	}

	// Spherical Fibonacci directions in world space, the same for every vertex
	// and frame; the cosine weight fades one in and out as the normal turns.
	vec3 sum = vec3_splat(0.0);
	float weight = 0.0;
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
			sum += SdfRayLight(origin, d, SDF_STEPS, front) * w;
			weight += w;
		}
	}
	vec3 lit = weight > 0.0 ? sum / weight : vec3_splat(0.0);

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
