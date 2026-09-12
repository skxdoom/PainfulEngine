#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace painful {

// The scene as a texture: when a post-process wants the frame, the sky and
// world views draw into this target instead of the backbuffer, and a
// half-size copy is made once for whoever samples the scene small (the
// bloom's bright pass, the demon trail). Owned by the app, read by
// Render/Bloom.h and Render/DemonFx.h. Docs/Reference/Bloom.md, "Targets".
class SceneTargets {
public:
	~SceneTargets() { Shutdown(); }
	SceneTargets() = default;
	SceneTargets(const SceneTargets&) = delete;
	SceneTargets& operator=(const SceneTargets&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(copy_); }

	// The backbuffer's sample count; the scene target takes the same.
	void SetMsaa(int samples);
	// Once per frame before the scene is submitted: `skyView` and
	// `worldView` draw into the scene when `enabled`, into the backbuffer
	// otherwise. Targets follow the window size.
	void BeginFrame(int width, int height, bool enabled, bgfx::ViewId skyView,
			bgfx::ViewId worldView);
	// After the scene: the half-size copy, a bilinear 2x2 average.
	void Downsample(bgfx::ViewId halfView);

	bool active() const { return active_; }
	int width() const { return width_; }
	int height() const { return height_; }
	int halfWidth() const { return halfW_; }
	int halfHeight() const { return halfH_; }
	int msaa() const { return msaa_; }
	bgfx::TextureHandle color() const { return color_; }
	bgfx::TextureHandle depth() const { return depth_; }
	bgfx::TextureHandle half() const { return half_; }
	bgfx::FrameBufferHandle framebuffer() const { return fb_; }
	const bgfx::VertexLayout& layout() const { return layout_; }
	// The plain copy program, for any pass that only moves pixels.
	bgfx::ProgramHandle copyProgram() const { return copy_; }
	bgfx::UniformHandle sceneSampler() const { return sScene_; }

	// bgfx's render-target multisample flag for a count; no 6x, so "x6" is 8x.
	static uint64_t MsaaTextureFlag(int samples);

private:
	void ReleaseTargets();
	bool BuildTargets(int width, int height);

	bgfx::ProgramHandle copy_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sScene_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;
	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::FrameBufferHandle halfFb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle half_ = BGFX_INVALID_HANDLE;
	int width_ = 0, height_ = 0, halfW_ = 0, halfH_ = 0, msaa_ = 0;
	bool active_ = false;
};

} // namespace painful
