#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace painful {

class SceneTargets;

// The bloom post-process: the scene's bright part is taken from the half-size
// copy, blurred in two passes and added back over the scene. The threshold,
// the kernel and the gains are the original's (View::Render -> FUN_100a9dc0,
// Bloom.fxo); the resolution is not - the original point-copies the screen to
// 512x512 first. The recovered chain and the deviations: Docs/Reference/Bloom.md.
class Bloom {
public:
	~Bloom() { Shutdown(); }
	// Owns GPU handles that Shutdown destroys, so it is not copyable.
	Bloom() = default;
	Bloom(const Bloom&) = delete;
	Bloom& operator=(const Bloom&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(bright_); }

	// The level's CLevel.BloomFX block, as WORLD.BloomFXParams delivers it:
	// LuminanceThreshold, Multiplier, OverlayColor packed A8R8G8B8.
	void SetParams(float threshold, float multiplier, uint32_t overlayArgb);
	// `scale` divides the screen for the blur buffers (the original's is 2;
	// 1 reads the full scene, 2 and up the half-size copy); `kernel` 0 is the
	// Gaussian carried out to three sigma, 1 the original's 13 taps cut at
	// one and a half. Both sum to the same weight.
	void SetQuality(int scale, int kernel);

	// After the scene and its half-size copy: the bright pass, the two blurs
	// and the composite, which lands the finished frame on the backbuffer.
	// Nothing when the scene is not in its target.
	void Draw(const SceneTargets& scene, bgfx::ViewId brightView, bgfx::ViewId blurHView,
			bgfx::ViewId blurVView, bgfx::ViewId compositeView);
	// Not this frame (the frame log reads active()).
	void Skip() { active_ = false; }

	bool active() const { return active_; }
	int bufferWidth() const { return bufW_; }
	int bufferHeight() const { return bufH_; }
	int taps() const { return pairs_ > 0 ? pairs_ * 2 - 1 : 0; }
	float threshold() const { return threshold_; }
	float multiplier() const { return multiplier_; }

	static constexpr int kMaxPairs = 16;

private:
	void ReleaseTargets();
	bool BuildTargets(int width, int height);
	void BuildKernel();

	bgfx::ProgramHandle bright_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle blur_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle composite_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sScene_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sBloom_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uDir_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uKernel_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uOverlay_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;

	bgfx::FrameBufferHandle fb_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	bgfx::TextureHandle color_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	int width_ = 0, height_ = 0, bufW_ = 0, bufH_ = 0;

	float threshold_ = 0.25f, multiplier_ = 1.f;
	float overlay_[4] = {0.5f, 0.5f, 0.5f, 0.f};
	int scale_ = 2, kernelMode_ = 0;
	float kernel_[kMaxPairs][4] = {};
	int pairs_ = 0;
	bool kernelDirty_ = true;
	bool active_ = false;
};

} // namespace painful
