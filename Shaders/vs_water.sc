$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_wpos, v_viewdist

// Water surface, following water_ref.vso (the vertex program the nv20 water
// pass names). That program hands the pixel shader:
//
//     oT0     the $normalmap UV, texcoord0 through the stage-0 matrix
//     oT1..3  rows of a tangent-to-world 3x3 in xyz, and the EYE VECTOR
//             in the w lane - "add oT1, c4, -v0.wwwx" is c4.w - position.x
//
// The tangent basis is CONSTANT there, because a water surface is a flat
// horizontal plane, so it does not need to travel per vertex. What does is the
// eye vector, which is all v_wpos is for here.
//
// water_ref.vso also displaces position.y by a sine polynomial evaluated from
// the xz position (the "add r1.y, r1.y, r7.x" near the end). Its coefficients
// live in engine-side constants c13..c20 that are not in any shipped file, so
// that displacement is not reproduced - the surface here is flat.
#include <bgfx_shader.sh>

void main()
{
	vec4 world   = mul(u_model[0], vec4(a_position, 1.0));
	v_wpos       = world.xyz;
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	gl_Position  = mul(u_proj, viewPos);
	v_viewdist   = length(viewPos.xyz);
	v_texcoord0  = a_texcoord0;
	v_texcoord1  = a_texcoord1;
}
