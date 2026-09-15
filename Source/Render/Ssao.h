#pragma once
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

struct Camera;
class SceneTargets;

// Screen-space ambient occlusion over the scene, under either model shading: the
// obscurance of each pixel's surroundings (McGuire's Alchemy estimator) from the
// scene's depth at half size, blurred along depth, and multiplied over the scene
// before anything reads the frame. A deviation with nothing to recover behind it.
// Docs/Reference/Lighting.md, "Screen-space ambient occlusion".
class Ssao {
public:
	~Ssao() { Shutdown(); }
	Ssao() = default;
	Ssao(const Ssao&) = delete;
	Ssao& operator=(const Ssao&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(apply_); }

	// SSAOScreenRadius as a share of the screen's height, SSAOStrength 0..1.
	void SetParams(float radius, float strength);
	// After the scene's draws, in views ordered after the world's and before any
	// pass that reads the frame. Nothing when the scene is not in its target or
	// its depth cannot be read.
	void Draw(const SceneTargets& scene, const Camera& camera, bgfx::ViewId aoView, bgfx::ViewId blurHView,
			bgfx::ViewId blurVView, bgfx::ViewId applyView);
	bool active() const { return active_; }

private:
	void ReleaseTargets();
	bool BuildTargets(int width, int height);

	bgfx::ProgramHandle ao_ = BGFX_INVALID_HANDLE; // the depth single-sampled
	bgfx::ProgramHandle aoMs_ = BGFX_INVALID_HANDLE; // the depth multisampled
	bgfx::ProgramHandle blur_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle apply_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDepth_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sAo_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uInvProj_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uScreen_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uBlur_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;
	// Half size, RGBA16F: r the occlusion, g the view depth for the blur.
	bgfx::FrameBufferHandle fb_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	bgfx::TextureHandle tex_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	int bufW_ = 0, bufH_ = 0;
	float radius_ = 1.f, strength_ = 1.f;
	bool active_ = false;
};

} // namespace painful
