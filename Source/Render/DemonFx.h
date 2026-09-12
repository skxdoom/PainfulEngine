#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace painful {

class SceneTargets;
class TextureCache;

// Demon Morph (View::RenderDemonFXWorld, 0x100B5D30): the scene (in
// Render/SceneTargets.h) is turned into hard black-and-white, the demonic
// models are drawn over it in their red fresnel glow, and the result is
// warped through a dudv map and mixed with last frame's for the trail, at
// half size - which is the softness the original's 512x512 textures had.
// The chain and the constants are the original's. Docs/Reference/DemonFx.md.
class DemonFx {
public:
	~DemonFx() { Shutdown(); }
	DemonFx() = default;
	DemonFx(const DemonFx&) = delete;
	DemonFx& operator=(const DemonFx&) = delete;

	bool Init(const std::string& shaderDir, TextureCache& textures);
	void Shutdown();
	bool ready() const { return bgfx::isValid(gray_); }

	// WORLD.DemonFXParams(Scale, Bias, 1 - MBlur, MBlur) - CLevel.DemonFX.
	void SetParams(float scale, float bias, float keep, float mblur);
	// WORLD.DemonFXWarp(amount), the DemonFXWarp process, every tick.
	void SetWarp(float warp) { warp_ = warp; }

	// Once per frame before the scene is submitted, after the scene targets'
	// own BeginFrame: builds this pass's targets on the scene's size and
	// points `entityView` (the demonic models) at the grayscale target,
	// which shares the scene's depth.
	void BeginFrame(const SceneTargets& scene, bool enabled, bgfx::ViewId entityView);
	// After the scene and the models: grayscale, warp + trail, copy out.
	void Draw(const SceneTargets& scene, bgfx::ViewId grayView, bgfx::ViewId warpView,
			bgfx::ViewId copyView, float dt);

	bool active() const { return active_; }
	// What the demonic models are drawn with (EntityRenderer::SetDemonPass).
	bgfx::TextureHandle detail() const { return detail_; }
	bgfx::TextureHandle ramp() const { return ramp_; }
	// c11.z of palskin_fresnel.vso. Not recovered from the binary - the
	// material record field it is read from (+0x94) has no found writer -
	// so this is set by eye against the original. Docs/Reference/DemonFx.md.
	static constexpr float kFresnelScale = 0.5f;
	float warp() const { return warp_; }

private:
	void ReleaseTargets();
	bool BuildTargets(const SceneTargets& scene);
	// warp_dudv.tga as a signed RG8 texture (owned here, unlike the other two).
	static bgfx::TextureHandle LoadDudv(const std::string& path);

	bgfx::ProgramHandle gray_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle warpProgram_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sScene_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDudv_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sPrev_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uGray_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uWarp_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle ramp_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle dudv_ = BGFX_INVALID_HANDLE;

	bgfx::FrameBufferHandle grayFb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle grayColor_ = BGFX_INVALID_HANDLE;
	bgfx::FrameBufferHandle pingFb_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	bgfx::TextureHandle ping_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	int width_ = 0, height_ = 0, halfW_ = 0, halfH_ = 0, msaa_ = -1;
	bgfx::TextureHandle builtDepth_ = BGFX_INVALID_HANDLE;
	int current_ = 0;
	bool fresh_ = true;

	float scale_ = 4.f, bias_ = -2.7f, keep_ = 0.3f, mblur_ = 0.7f;
	float warp_ = 0.f;
	bool active_ = false;
};

} // namespace painful
