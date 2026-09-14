$input v_normal

// pfsdfdebug's probe spheres: the traced SH at the pixel normal, times the
// gain - the ambient a model would take there, and nothing else.
#include <bgfx_shader.sh>
#include "shared_sh.sh"

uniform vec4 u_sh[9];
uniform vec4 u_sdfProbe; // x: SdfGain

void main()
{
	vec3 n = normalize(v_normal);
	vec3 sum = vec3_splat(0.0);
	for (int k = 0; k < 9; ++k) sum += u_sh[k].rgb * ShBasis(k, n);
	gl_FragColor = vec4(max(sum, vec3_splat(0.0)) * u_sdfProbe.x, 1.0);
}
