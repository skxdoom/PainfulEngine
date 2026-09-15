$input v_texcoord0

// pfsdfdebug: a ray per pixel from the eye through the level's distance field by
// the vertex traces' own march (shared_sdf.sh), showing what such a ray finds: a
// surface lit by its bins facing the eye, voxels and all, and past the level the
// sky or the fog. Render/SdfFieldDebug.h
#include <bgfx_shader.sh>

#define PAINFUL_SDF_INDEX_STAGE 0
#define PAINFUL_SDF_LIST_STAGE 1
#define PAINFUL_SDF_SKY_STAGE 2
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
	vec3 color = vec3(0.02, 0.02, 0.03);
	if (u_sdfVolume.w > 0.0)
	{
		// From where the ray enters the volume, when the eye is outside it.
		vec3 lo = u_sdfVolume.xyz;
		vec3 hi = lo + u_sdfExtent.xyz * u_sdfVolume.w;
		vec3 inv = 1.0 / (dir + vec3(abs(dir.x) < 1e-6 ? 1e-6 : 0.0, abs(dir.y) < 1e-6 ? 1e-6 : 0.0,
				abs(dir.z) < 1e-6 ? 1e-6 : 0.0));
		vec3 t0 = (lo - eye) * inv;
		vec3 t1 = (hi - eye) * inv;
		vec3 tmin = min(t0, t1);
		vec3 tmax = max(t0, t1);
		float enter = max(max(tmin.x, tmin.y), max(tmin.z, 0.0));
		float leave = min(min(tmax.x, tmax.y), tmax.z);
		vec3 p = eye + dir * (enter + (enter > 0.0 ? 0.01 * u_sdfVolume.w : 0.0));
		int s = enter < leave ? SdfMarch(p, dir, 384) : -1;
		if (s >= 0)
		{
			float front;
			color = SdfSurfaceLight(s, dir, front);
		}
		else if (s == -2)
		{
			color = vec3(0.04, 0.05, 0.10);
		}
		else
		{
			color = max(SdfBackdrop(dir), color);
		}
	}
	gl_FragColor = vec4(color, 1.0);
}
