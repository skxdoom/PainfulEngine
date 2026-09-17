$input v_viewdist

// A volume over the scene, from a single-sampled scene depth (Render/VolumeRenderer.h).
#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);

float VolumeSceneDepth(ivec2 p)
{
	return texelFetch(s_depth, p, 0).r;
}

#include "shared_volume.sh"
