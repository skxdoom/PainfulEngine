#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <vector>

namespace painful {

class SkyRenderer;

// The sky as a light, for SdfLighting's rays that leave the window. The dome is
// drawn into a 32x32 face, one of six a frame, read back to the CPU and binned
// by solid angle into a latitude-longitude map of the light from each
// direction. Once a level. Docs/Reference/Lighting.md, "Distance field ambient".
class SkyCapture {
public:
	static constexpr int kFace = 64;
	static constexpr int kWidth = 64, kHeight = 32; // longitude x latitude, row 0 straight up

	~SkyCapture() { Shutdown(); }
	SkyCapture() = default;
	SkyCapture(const SkyCapture&) = delete;
	SkyCapture& operator=(const SkyCapture&) = delete;

	// False without blit and read-back; the rays then keep the box ambient.
	bool Init();
	void Shutdown();
	// A new level: capture again.
	void Clear();
	// Once a frame until done(), with bgfx's current frame number. With no sky
	// it finishes at once, the map empty.
	void Tick(bgfx::ViewId drawView, bgfx::ViewId blitView, SkyRenderer* sky, float timeSeconds,
			uint32_t frameNumber);
	bool done() const { return face_ >= 6; }
	// RGB per cell; empty when the level has no sky.
	const std::vector<float>& map() const { return map_; }

private:
	void Accumulate();
	void Finish();

	bgfx::TextureHandle target_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
	bgfx::FrameBufferHandle framebuffer_ = BGFX_INVALID_HANDLE;
	std::vector<uint8_t> pixels_; // bgfx writes the read-back here, so it never shrinks
	float inverse_[16] = {}; // the face being read back: clip space to world
	uint32_t readyFrame_ = 0;
	bool waiting_ = false;
	int face_ = 0;
	std::vector<double> sum_; // RGB x solid angle, per cell
	std::vector<double> weight_;
	std::vector<float> map_;
};

} // namespace painful
