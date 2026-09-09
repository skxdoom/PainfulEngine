$input v_texcoord0

// Depth only. The one thing it has to get right is the alpha test: a grate
// or a bush that discards in the colour pass must discard here too, or it
// casts a solid slab. Same uniform names as fs_world, so the world's per-batch
// UV transform is replayed unchanged.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
uniform vec4 u_params; // y: alpha-test ref (<0 off)
uniform vec4 u_uvanim; // xy: stage-0 scroll offset
uniform vec4 u_uv0; // slot UV transform: scale xy, offset zw
uniform vec4 u_tile; // xy: stage-0 tiling

void main()
{
	if (u_params.y >= 0.0)
	{
		vec2 uv = (v_texcoord0 * u_uv0.xy + u_uv0.zw + u_uvanim.xy) * u_tile.xy;
		if (texture2D(s_diffuse, uv).a <= u_params.y) discard;
	}
	gl_FragColor = vec4_splat(0.0);
}
