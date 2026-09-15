#pragma once
#include "Camera.h"
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

class SdfField;

// pfsdfdebug: the level's distance field raymarched per pixel over the finished
// frame by the vertex traces' own march, from SdfField's textures: what a ray
// from the camera would take at each surface, and the sky or fog beyond.
// Docs/Reference/Lighting.md, "Distance field ambient".
class SdfFieldDebug {
public:
	~SdfFieldDebug() { Shutdown(); }
	SdfFieldDebug() = default;
	SdfFieldDebug(const SdfFieldDebug&) = delete;
	SdfFieldDebug& operator=(const SdfFieldDebug&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	// Over `view`, a backbuffer view of its own after the post chain.
	void Draw(bgfx::ViewId view, const Camera& camera, int width, int height, const SdfField& field);

private:
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;
	bgfx::UniformHandle uInvViewProj_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEye_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uScreen_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
