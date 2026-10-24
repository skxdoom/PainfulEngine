#pragma once
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

class SceneTargets;

// WORLD.SetRenderTarget: the scene goes into one of three textures and the
// screen shows a weighted sum of them (View::RenderWorldToTexture and
// RenderPostProcess, World::ComposeTextures 0x10059870) - TPlayerHit's blur.
// Half size, as DemonFx's trail. By default every stage is this frame, moved
// by the rotation its texture was meant to hold: the original's three
// textures each refresh every third tick. Docs/Reference/FrameCompose.md
class FrameCompose {
public:
	static constexpr int kTextures = 3;

	~FrameCompose() { Shutdown(); }
	FrameCompose() = default;
	FrameCompose(const FrameCompose&) = delete;
	FrameCompose& operator=(const FrameCompose&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(program_); }

	// After SceneTargets::Downsample: textures[i] * weights[i] onto the
	// backbuffer. With `shift` (uv, per texture 1..3) every stage samples this
	// frame at its texture's shift; without, this frame goes into texture
	// `target` and the stages read the textures, as the original. False when
	// it drew nothing and the scene still has to be presented.
	bool Draw(const SceneTargets& scene, int target, const int* textures, const float* weights,
			int count, const float (*shift)[2], bgfx::ViewId copyView, bgfx::ViewId composeView);

private:
	void ReleaseTargets();
	bool BuildTargets(const SceneTargets& scene);

	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sTex_[kTextures] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle uWeights_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uShift_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle tex_[kTextures] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::FrameBufferHandle fb_[kTextures] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	// A texture not drawn since the targets were built has nothing in it.
	bool written_[kTextures] = {false, false, false};
	int width_ = 0, height_ = 0;
};

} // namespace painful
