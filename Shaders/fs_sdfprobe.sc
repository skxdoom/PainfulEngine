$input v_normal

// pfsdfdebuggrid's spheres: the probe grids sampled at the sphere's centre for
// each pixel's normal, times the gain - the ambient a model's pixel there with
// that normal takes, and nothing else. Hidden outside the grids and where the
// centre is buried in a surface. Render/SdfDebug.h
#include <bgfx_shader.sh>

#define PAINFUL_SDF_FIELD_STAGE0 0
#define PAINFUL_SDF_FIELD_STAGE1 1
#define PAINFUL_SDF_FIELD_STAGE2 2
#define PAINFUL_SDF_LEVEL_STAGE 3
#define PAINFUL_SDF_PROBE_STAGE0 4
#define PAINFUL_SDF_PROBE_STAGE1 5
#define PAINFUL_SDF_PROBE_STAGE2 6
#include "shared_sdf.sh"

uniform vec4 u_sdfProbe; // xyz: the sphere's centre, w: SdfGain
uniform vec4 u_ambient; // rgb: the box ambient there, w: the sphere's radius

void main()
{
	vec3 centre = u_sdfProbe.xyz;
	int v = SdfVolumeAt(centre);
	if (v >= 0 && v < SDF_LEVEL &&
			SdfFieldAt(v, (centre - u_sdfVolume[v].xyz) / u_sdfVolume[v].w).w * u_sdfVolume[v].w < u_ambient.w)
		discard;
	float weight = 0.0;
	vec3 light = SdfAmbient(centre, normalize(v_normal), u_ambient.rgb, u_sdfProbe.w, weight);
	if (weight <= 0.0) discard;
	gl_FragColor = vec4(light, 1.0);
}
