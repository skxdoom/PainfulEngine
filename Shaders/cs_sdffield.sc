// Pf.RendererType 1: a volume's field from its distance transform
// (Render/SdfProbes.h): per voxel the unit direction to the nearest surface
// voxel's centre as rgb * 0.5 + 0.5, and the distance in quarter voxels (a; 1 none).
#include <bgfx_compute.sh>

IMAGE3D_WO(i_sdfField, rgba8, 0);
SAMPLER3D(s_sdfFeatures, 1);

uniform vec4 u_sdfEdt; // xyz: the volume's voxels along each axis

NUM_THREADS(8, 8, 8)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 dims = ivec3(u_sdfEdt.xyz);
	if (cell.x >= dims.x || cell.y >= dims.y || cell.z >= dims.z) return;
	float packed = texelFetch(s_sdfFeatures, cell, 0).r;
	if (packed < 0.0)
	{
		imageStore(i_sdfField, cell, vec4(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 1.0));
		return;
	}
	vec3 at = vec3(mod(packed, 256.0), mod(floor(packed / 256.0), 256.0), floor(packed / 65536.0));
	vec3 delta = at - vec3(cell);
	float d = length(delta);
	vec3 u = delta / max(d, 1e-6);
	imageStore(i_sdfField, cell, vec4(u * 0.5 + 0.5, min(d, 63.75) * 4.0 / 255.0));
}
