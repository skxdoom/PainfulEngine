// Pf.RendererType 1: one axis of the exact Euclidean distance transform that
// builds a volume's field on the GPU (Render/SdfProbes.h). Along each line of
// the axis a voxel takes the feature minimising (p - q)^2 + f(q), f(q) being the
// squared distance to q's feature over the axes done before: the lower envelope
// of parabolas (Felzenszwalb and Huttenlocher, "Distance Transforms of Sampled
// Functions"). A feature is a surface voxel packed x + y * 256 + z * 65536; -1 none.
#include <bgfx_compute.sh>

IMAGE3D_WO(i_sdfEdtOut, r32f, 0);
SAMPLER3D(s_sdfEdtIn, 1);

uniform vec4 u_sdfEdt; // xyz: the volume's voxels along each axis, w: the axis

#define MAX_SIDE 256

NUM_THREADS(8, 8, 1)
void main()
{
	int axis = int(u_sdfEdt.w + 0.5);
	ivec3 dims = ivec3(u_sdfEdt.xyz);
	ivec3 id = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 base = axis == 0 ? ivec3(0, id.x, id.y) : (axis == 1 ? ivec3(id.x, 0, id.y) : ivec3(id.x, id.y, 0));
	ivec3 along = axis == 0 ? ivec3(1, 0, 0) : (axis == 1 ? ivec3(0, 1, 0) : ivec3(0, 0, 1));
	if (base.x >= dims.x || base.y >= dims.y || base.z >= dims.z) return;
	int n = axis == 0 ? dims.x : (axis == 1 ? dims.y : dims.z);

	// fq[q]: f(q) + q^2; v: the envelope's parabolas; z: where each takes over.
	float fq[MAX_SIDE];
	float feature[MAX_SIDE];
	int v[MAX_SIDE];
	float z[MAX_SIDE + 1];
	for (int i = 0; i < MAX_SIDE; ++i)
	{
		fq[i] = 0.0;
		v[i] = 0;
		z[i] = 0.0;
	}
	z[MAX_SIDE] = 0.0;
	int k = -1;
	for (int q = 0; q < n; ++q)
	{
		ivec3 cell = base + along * q;
		float packed = texelFetch(s_sdfEdtIn, cell, 0).r;
		feature[q] = packed;
		if (packed < 0.0) continue;
		vec3 at = vec3(mod(packed, 256.0), mod(floor(packed / 256.0), 256.0), floor(packed / 65536.0));
		vec3 d = vec3(cell) - at;
		fq[q] = dot(d, d) + float(q * q);
		float s = -1e20;
		for (int guard = 0; guard < MAX_SIDE && k >= 0; ++guard)
		{
			s = (fq[q] - fq[v[k]]) / float(2 * (q - v[k]));
			if (s > z[k]) break;
			k -= 1;
		}
		k += 1;
		v[k] = q;
		z[k] = k == 0 ? -1e20 : s;
		z[k + 1] = 1e20;
	}

	int j = 0;
	for (int p = 0; p < n; ++p)
	{
		float value = -1.0;
		if (k >= 0)
		{
			for (int guard = 0; guard < MAX_SIDE && j < k && z[j + 1] < float(p); ++guard) j += 1;
			value = feature[v[j]];
		}
		imageStore(i_sdfEdtOut, base + along * p, vec4_splat(value));
	}
}
