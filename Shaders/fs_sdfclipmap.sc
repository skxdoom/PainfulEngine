$input v_texcoord0

// pfsdfdebugclipmaps: a ray per pixel from the eye through the distance field
// by the probes' own march (shared_sdf.sh), showing what a probe ray finds: a
// surface lit by its bins facing the eye, a cascade's or the level field's
// coarser one, and the sky, or the fog, where the ray leaves the level.
// Render/SdfClipmapDebug.h
#include <bgfx_shader.sh>

#define PAINFUL_SDF_FIELD_STAGE0 0
#define PAINFUL_SDF_FIELD_STAGE1 1
#define PAINFUL_SDF_FIELD_STAGE2 2
#define PAINFUL_SDF_LEVEL_STAGE 3
#define PAINFUL_SDF_SURFACE_STAGE0 4
#define PAINFUL_SDF_SURFACE_STAGE1 5
#define PAINFUL_SDF_SURFACE_STAGE2 6
#define PAINFUL_SDF_BINS_STAGE0 7
#define PAINFUL_SDF_BINS_STAGE1 8
#define PAINFUL_SDF_BINS_STAGE2 9
#define PAINFUL_SDF_SKY_STAGE 10
#define PAINFUL_SDF_LEVEL_SURFACE_STAGE 11
#define PAINFUL_SDF_LEVEL_BINS_STAGE 12
#include "shared_sdf.sh"

uniform mat4 u_sdfInvViewProj;
uniform vec4 u_sdfEye; // xyz: the camera
uniform vec4 u_sdfScreen; // xy: backbuffer size, z: 1 when its origin is bottom-left

void main()
{
	vec2 frag = gl_FragCoord.xy / u_sdfScreen.xy;
	float ndcY = u_sdfScreen.z > 0.5 ? frag.y * 2.0 - 1.0 : 1.0 - frag.y * 2.0;
	vec4 farPoint = mul(u_sdfInvViewProj, vec4(frag.x * 2.0 - 1.0, ndcY, 1.0, 1.0));
	vec3 eye = u_sdfEye.xyz;
	vec3 dir = normalize(farPoint.xyz / farPoint.w - eye);
	vec3 p = eye;
	int v = SdfMarch(p, dir, 384);

	// Leaving the level with no sky near black, out of steps dark blue.
	vec3 color = vec3(0.02, 0.02, 0.03);
	if (v >= 0)
	{
		float front;
		color = SdfSurfaceLight(v, p, dir, front);
	}
	else if (v == -2)
	{
		color = vec3(0.04, 0.05, 0.10);
	}
	else
	{
		color = max(SdfBackdrop(dir), color);
	}
	gl_FragColor = vec4(color, 1.0);
}
