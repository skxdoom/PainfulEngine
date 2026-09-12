$input v_texcoord0

// Demon Morph, the world: neg_gray.pso verbatim - luminance, then
// Scale * lum + 1 - Bias - Scale (the mad_x4 of c1 = Scale/4 and
// c2 = (1 - Bias - Scale)/4). Docs/Reference/DemonFx.md, "The world".
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
// x = Scale, y = Bias.
uniform vec4 u_demonGray;

void main()
{
	vec3 c = texture2D(s_scene, v_texcoord0).rgb;
	float lum = dot(c, vec3(0.3, 0.59, 0.11));
	float g = u_demonGray.x * lum + 1.0 - u_demonGray.y - u_demonGray.x;
	gl_FragColor = vec4(vec3_splat(clamp(g, 0.0, 1.0)), 1.0);
}
