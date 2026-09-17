$input a_position
$output v_viewdist

// Fog and light volumes (Render/VolumeRenderer.h): the position, and its distance
// along the view axis - the clip z the original's fog.vso ramps over. The camera
// is right-handed, so ahead is -z.
#include <bgfx_shader.sh>

void main()
{
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	gl_Position = mul(u_proj, viewPos);
	v_viewdist = -viewPos.z;
}
