#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>

namespace painful {

// The water's planar reflection target: the scene mirrored about the water
// plane, drawn into a half-size texture the water shader projects back onto
// the surface. The original renders it into World+0x18d0 at half the back
// buffer (World::Init), clears it and draws nothing when the camera is further
// from the water than the environment's ReflectDist (View::RenderFakeReflection).
// Docs/Reference/Water.md, "The planar reflection".
class WaterReflection {
public:
	~WaterReflection() { Shutdown(); }
	WaterReflection() = default;
	WaterReflection(const WaterReflection&) = delete;
	WaterReflection& operator=(const WaterReflection&) = delete;

	void Shutdown();
	// Sizes the target to half the window and points the two views at it,
	// cleared to black. False when no target could be made.
	bool Begin(int width, int height, bgfx::ViewId skyView, bgfx::ViewId worldView);
	bgfx::TextureHandle texture() const { return color_; }
	bool ready() const { return bgfx::isValid(fb_); }

private:
	bool Build(int width, int height);
	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	int width_ = 0, height_ = 0;
};

} // namespace painful
