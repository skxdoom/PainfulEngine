#pragma once
#include "Camera.h"
#include "../World/Level.h"
#include <bgfx/bgfx.h>

namespace painful {

class SkyRenderer;
class WorldRenderer;

// The live environment cube map a level with o.RTCubeMap asks for. The
// original (View::RenderCubemap, 0x100b4c80) draws the world into six 512
// faces every frame from the eye mirrored about o.Water.WaterLevel, which is
// what makes a model's $envcubemap reflection sit where a planar one would -
// the Swamp's water reflects its bonfires this way. Water.md, "Swamp".
class EnvCubeMap {
public:
	~EnvCubeMap() { Shutdown(); }
	EnvCubeMap() = default;
	EnvCubeMap(const EnvCubeMap&) = delete;
	EnvCubeMap& operator=(const EnvCubeMap&) = delete;

	void Shutdown();
	// Draws the six faces into views base .. base + 11 (sky, world per face).
	// False when no target could be made.
	bool Render(bgfx::ViewId base, SkyRenderer* sky, WorldRenderer& world, const Camera& camera,
			float waterLevel, const LevelInfo& info, float timeSeconds, int size);
	bgfx::TextureHandle texture() const { return cube_; }
	bool ready() const { return bgfx::isValid(cube_); }

private:
	bool Build(int size);
	bgfx::TextureHandle cube_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::FrameBufferHandle faces_[6] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	int size_ = 0;
	int failedSize_ = 0; // a size that could not be built, not retried every frame
};

} // namespace painful
