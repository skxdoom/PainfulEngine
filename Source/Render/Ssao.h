#pragma once
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

struct Camera;
class SceneTargets;

// Screen-space ambient occlusion over the scene: the horizon occlusion of each
// pixel's surroundings from the scene's depth at half size, blurred along depth, and
// multiplied over the scene before anything reads the frame. A deviation with
// nothing to recover behind it. Docs/Reference/Lighting.md, "Screen-space ambient occlusion".
class Ssao {
public:
	~Ssao() { Shutdown(); }
	Ssao() = default;
	Ssao(const Ssao&) = delete;
	Ssao& operator=(const Ssao&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(apply_); }

	// SSAOScreenRadius as a share of the screen's height.
	void SetRadius(float radius);
	// SSAOIntensity as a factor; the rest of the shape is fixed below.
	void SetIntensity(float intensity);
	// The fixed tuning (Lighting.md, "Screen-space ambient occlusion"): the share of
	// the occlusion applied, how far an occluder must rise over a surface and how
	// high (of the radius) to count fully, and the fade-out distances in world units.
	static constexpr float kStrength = 1.f;
	static constexpr float kAngleDegrees = 30.f;
	static constexpr float kHeight = 0.4f;
	static constexpr float kFadeStart = 30.f, kFadeEnd = 60.f;
	// The level fog, as the world shaders get it: the occlusion thins with it.
	void SetFog(int mode, float start, float end, float density) {
		fog_[0] = float(mode); fog_[1] = start; fog_[2] = end; fog_[3] = density;
	}
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
	bgfx::UniformHandle uShape_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFade_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uBlur_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;
	// Half size, RGBA16F: r the occlusion, g the view depth for the blur.
	bgfx::FrameBufferHandle fb_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	bgfx::TextureHandle tex_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
	int bufW_ = 0, bufH_ = 0;
	float radius_ = 1.f;
	float shape_[4] = {5.f, 0.5f, 0.1f, 0.4f}; // SetIntensity rebuilds it from the constants
	float fog_[4] = {0.f, 0.f, 0.f, 0.f};
	const float fade_[4] = {kFadeStart, kFadeEnd, 0.f, 0.f};
	bool active_ = false;
};

} // namespace painful
