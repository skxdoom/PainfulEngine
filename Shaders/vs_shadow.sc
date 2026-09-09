$input a_position, a_texcoord0
$output v_texcoord0

// The flashlight's depth pass. World chunks and models share MeshVertex, so
// one program draws both into the shadow map; only the UV survives, for the
// alpha test in fs_shadow. Docs/Reference/Lighting.md, "Shadows"
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
}
