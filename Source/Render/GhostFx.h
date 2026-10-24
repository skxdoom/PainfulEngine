#pragma once
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

// The ghost pass (WORLD.EnableGhostsFX, View::RenderGhostFXWorld 0x100B5960):
// the demonic models are drawn in grey into their own target, cleared black
// with its own depth, mixed into a trail with last frame's (0.2 / 0.8), and
// the trail is added over the finished frame. C5L4's spawned monsters and
// Lucifer's sword. Half size, as DemonFx's trail. Docs/Reference/GhostFx.md
class GhostFx {
public:
	~GhostFx() { Shutdown(); }
	GhostFx() = default;
	GhostFx(const GhostFx&) = delete;
	GhostFx& operator=(const GhostFx&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(compose_) && bgfx::isValid(copy_); }

	// Before the scene: the targets at half the window, and `entityView` (the
	// demonic models, EntityRenderer::SetGhostPass) pointed at the ghost target.
	void BeginFrame(int width, int height, bool enabled, bgfx::ViewId entityView);
	// After the frame is on the backbuffer: the trail, then its add.
	void Draw(bgfx::ViewId trailView, bgfx::ViewId addView, float dt);
	bool active() const { return active_; }

private:
	void ReleaseTargets();
	bool BuildTargets(int width, int height);

	bgfx::VertexLayout layout_;
	bgfx::ProgramHandle compose_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle copy_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sTex_[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	bgfx::UniformHandle uWeights_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sScene_ = BGFX_INVALID_HANDLE;

	bgfx::FrameBufferHandle ghostFb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle ghostColor_ = BGFX_INVALID_HANDLE;
	bgfx::FrameBufferHandle pingFb_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	bgfx::TextureHandle ping_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	int width_ = 0, height_ = 0, halfW_ = 0, halfH_ = 0;
	int current_ = 0;
	bool fresh_ = true;
	bool active_ = false;
};

} // namespace painful
