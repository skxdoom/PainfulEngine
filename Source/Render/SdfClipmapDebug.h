#pragma once
#include "Camera.h"
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

class SdfProbes;

// pfsdfdebugclipmaps: the distance field raymarched per pixel over the finished
// frame by the probes' own march, from SdfProbes' textures: what a ray from the
// camera would take at each surface, the level field past the cascades, and the
// sky beyond it. Docs/Reference/Lighting.md, "Distance field ambient".
class SdfClipmapDebug {
public:
	~SdfClipmapDebug() { Shutdown(); }
	SdfClipmapDebug() = default;
	SdfClipmapDebug(const SdfClipmapDebug&) = delete;
	SdfClipmapDebug& operator=(const SdfClipmapDebug&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	// Over `view`, a backbuffer view of its own after the post chain.
	void Draw(bgfx::ViewId view, const Camera& camera, int width, int height, const SdfProbes& probes);

private:
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;
	bgfx::UniformHandle uInvViewProj_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEye_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uScreen_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
