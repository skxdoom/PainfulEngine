$input v_texcoord0

// Demon Morph, the warp and the trail: embm_mblur.pso. The scene is fetched
// through the dudv map scaled by WORLD.DemonFXWarp's amount (the bump-env
// matrix), then mixed with last frame's result by the level's MBlur.
// Docs/Reference/DemonFx.md, "The warp" and "The trail".
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_dudv, 1);
SAMPLER2D(s_prev, 2);
// x = warp amount, y = weight of this frame, z = weight of the last one.
uniform vec4 u_demonWarp;

void main()
{
	// A signed texture (RG8S, DemonFx::LoadDudv): -1..1, 0 is no offset.
	vec2 d = texture2D(s_dudv, v_texcoord0).rg;
	vec3 scene = texture2D(s_scene, v_texcoord0 + d * u_demonWarp.x).rgb;
	vec3 prev = texture2D(s_prev, v_texcoord0).rgb;
	gl_FragColor = vec4(scene * u_demonWarp.y + prev * u_demonWarp.z, 1.0);
}
