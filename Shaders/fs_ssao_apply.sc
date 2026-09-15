$input v_texcoord0

// SSAO over the scene (Render/Ssao.h): the blurred occlusion, at the strength set,
// as a colour the scene is multiplied by.
#include <bgfx_shader.sh>

SAMPLER2D(s_ao, 0);

uniform vec4 u_ssaoParams; // y: strength

void main()
{
	float kept = texture2D(s_ao, v_texcoord0).r;
	gl_FragColor = vec4(vec3_splat(mix(1.0, kept, u_ssaoParams.y)), 1.0);
}
