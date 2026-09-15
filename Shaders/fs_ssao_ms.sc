$input v_texcoord0

// SSAO's occlusion from a multisampled scene depth (Render/Ssao.h): depth does not
// resolve, so the first sample stands for the pixel.
#include <bgfx_shader.sh>

SAMPLER2DMS(s_depth, 0);

float SsaoDepth(ivec2 p)
{
	return texelFetch(s_depth, p, 0).r;
}

#include "shared_ssao.sh"
