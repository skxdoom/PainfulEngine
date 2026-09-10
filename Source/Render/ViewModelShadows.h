#pragma once
#include "../Core/Frustum.h"
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

// The view model's shadow map: one orthographic map down the environment
// box's directional, fitted to the weapon's bounding sphere, so it is dense
// where the eye is closest. The weapon alone casts - its self-shadowing is
// what this is for - and alone reads it. Placed lights and the flashlight
// were given slots of their own and taken out again: at the eye they added
// nothing worth a map. Docs/Reference/Lighting.md, "Shadows on the view model"
class ViewModelShadows {
public:
	~ViewModelShadows() { Shutdown(); }
	ViewModelShadows() = default;
	ViewModelShadows(const ViewModelShadows&) = delete;
	ViewModelShadows& operator=(const ViewModelShadows&) = delete;

	bool Init(const std::string& shaderDir, int size);
	void Shutdown();
	bool ready() const { return bgfx::isValid(fb_); }
	int size() const { return size_; }
	void SetView(bgfx::ViewId view) { view_ = view; }
	bgfx::ViewId viewId() const { return view_; }

	void BeginFrame() { active_ = false; }
	// An orthographic box about the sphere, looking down toLight.
	void Begin(const Vec3& toLight, const Vec3& centre, float radius);
	bool active() const { return active_; }
	const Frustum& frustum() const { return frustum_; }

	// The receiver's uniforms: u_vmMtx (world -> uv and depth, crop
	// included) and u_vmLight (w: one texel in world units, orthographic).
	const float* matrix() const { return matrix_; }
	const float* light() const { return light_; }
	float texel() const { return texel_; } // one map texel in uv
	bgfx::TextureHandle texture() const { return depth_; }
	bgfx::ProgramHandle program() const { return program_; }
	static constexpr uint64_t kState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

private:
	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::ViewId view_ = 0;
	int size_ = 0;
	float texel_ = 0.f;
	bool active_ = false;
	Frustum frustum_ = {};
	float matrix_[16] = {};
	float light_[4] = {};
};

} // namespace painful
