$input v_texcoord0

// The finished frame: the scene plus the blurred bright pass scaled by the
// level's OverlayColor - the original's additive quad, MODULATE texture x
// diffuse with the overlay as the diffuse. Docs/Reference/Bloom.md, "Composite".
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);
// rgb = OverlayColor / 255.
uniform vec4 u_bloomOverlay;

void main()
{
	vec3 scene = texture2D(s_scene, v_texcoord0).rgb;
	vec3 bloom = texture2D(s_bloom, v_texcoord0).rgb;
	gl_FragColor = vec4(scene + bloom * u_bloomOverlay.rgb, 1.0);
}
