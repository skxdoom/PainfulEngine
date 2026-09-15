$input v_normal, v_sdfLight

// pfsdfdebuggrid's spheres: the light traced through the distance field at
// each vertex of the sphere (cs_sdfvertex), times the gain - what a model's
// vertex there facing that way takes, and nothing else. Hidden where the centre
// is buried in a surface and where nothing is traced yet. Render/SdfDebug.h
#include <bgfx_shader.sh>

#define PAINFUL_SDF_INDEX_STAGE 0
#define PAINFUL_SDF_LIST_STAGE 1
#define PAINFUL_SDF_SKY_STAGE 2
#include "shared_sdf.sh"

uniform vec4 u_sdfProbe; // xyz: the sphere's centre, w: SdfGain
uniform vec4 u_ambient; // w: the sphere's radius

void main()
{
	vec3 q = SdfVoxels(u_sdfProbe.xyz);
	if (SdfInside(q))
	{
		int s = SdfNearest(q);
		if (s >= 0 && length(SdfSurfaceCentre(s) - q) * u_sdfVolume.w < u_ambient.w) discard;
	}
	if (v_sdfLight.w < 0.5) discard;
	gl_FragColor = vec4(v_sdfLight.rgb / v_sdfLight.w * u_sdfProbe.w, 1.0);
}
