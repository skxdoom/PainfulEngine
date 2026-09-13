$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_wpos, v_viewdist, v_clip

// FXWater_20's vertex program (Water.fxo, vs @21672): two sine waves along
// GDirs (-1,0,0) and (-0.7,0,0.7) lift the vertex in OBJECT space before the
// transform, the two normal-map UVs and the eye vector are left to the
// fragment shader. Docs/Reference/Water.md, "FXWater_20, decoded".
#include <bgfx_shader.sh>

// x BumpHeight, y WaveFrequency, z WaveAmplitude, w phase (WaveSpeed * t)
uniform vec4 u_water;

void main()
{
	vec3 p = a_position;
	// RenderWater (0x101d8bb0) uploads GFreq = (f, 2f), GPhase = (0.5, 1.3) * phase
	// and GAmpli = (a, 0.5a); the second wave is the finer, faster one.
	float d0 = -p.x;
	float d1 = -0.7 * p.x + 0.7 * p.z;
	float w0 = d0 * u_water.y - u_water.w * 0.5;
	float w1 = d1 * (2.0 * u_water.y) - u_water.w * 1.3;
	p.y += sin(w0) * u_water.z + sin(w1) * (0.5 * u_water.z);

	vec4 world = mul(u_model[0], vec4(p, 1.0));
	v_wpos = world.xyz;
	vec4 viewPos = mul(u_modelView, vec4(p, 1.0));
	gl_Position = mul(u_proj, viewPos);
	v_clip = gl_Position;
	v_viewdist = length(viewPos.xyz);
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
}
