$input v_texcoord0

// SSAO's occlusion from a single-sampled scene depth (Render/Ssao.h).
#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);

float SsaoDepth(ivec2 p)
{
	return texelFetch(s_depth, p, 0).r;
}

#include "shared_ssao.sh"
