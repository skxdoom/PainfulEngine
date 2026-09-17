$input v_viewdist

// A volume over the scene, from a multisampled scene depth (Render/VolumeRenderer.h):
// depth does not resolve, so the first sample stands for the pixel.
#include <bgfx_shader.sh>

SAMPLER2DMS(s_depth, 0);

float VolumeSceneDepth(ivec2 p)
{
	return texelFetch(s_depth, p, 0).r;
}

#include "shared_volume.sh"
