#pragma once
#include "../Core/Frustum.h"
#include "../Core/Vectors.h"
#include "../World/Lighting.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

// A light that has a slot in the atlas this frame.
struct ShadowedLight {
	const LightSource* light; // into the script light list, stable for the frame
	int slot;
	// 1 near the camera, falling to 0 at the pick radius, so a light leaving
	// the set fades its shadows out rather than dropping them.
	float fade;
};

// Shadow maps for the placed lights: the models cast, the models and the
// world receive. A few lights a frame, each a slot of one depth atlas. A
// point light takes six 90-degree
// faces, a spot one face down its cone; each face is a bgfx view with its
// own rect. The receiver finds a point's face and depth analytically from
// the light-relative vector, so a light costs the shader one vec4.
// Docs/Reference/Lighting.md, "Shadows from the placed lights"
class LightShadowAtlas {
public:
	~LightShadowAtlas() { Shutdown(); }
	LightShadowAtlas() = default;
	LightShadowAtlas(const LightShadowAtlas&) = delete;
	LightShadowAtlas& operator=(const LightShadowAtlas&) = delete;

	// `slots` lights of `faceSize` texels per face: an atlas 3 faces wide and
	// 2 * slots faces tall.
	bool Init(const std::string& shaderDir, int faceSize, int slots);
	void Shutdown();
	bool ready() const { return bgfx::isValid(fb_); }
	int slots() const { return slots_; }
	int faceSize() const { return faceSize_; }
	// The first of the 6 * slots view ids the faces render into.
	void SetBaseView(bgfx::ViewId base) { base_ = base; }
	bgfx::ViewId viewId(int slot, int face) const { return bgfx::ViewId(base_ + slot * 6 + face); }

	// Forgets last frame's lights.
	void BeginFrame();
	// Aims a slot at a light: its faces' views, frustums and receiver
	// constants. Returns the face count, 6 for a point light, 1 for a spot.
	int Begin(int slot, const LightSource& light);
	int faceCount(int slot) const { return slots_ && slot < slots_ ? slotFaces_[slot] : 0; }
	const Frustum& faceFrustum(int slot, int face) const { return faceFrustum_[slot * 6 + face]; }
	// (slot, A, B, 0) as u_dynShadow wants it: depth in the map is
	// A + B / distance-along-the-face; the caller puts the fade in w. x is -1
	// for an unused slot.
	void ReceiverParams(int slot, float out[4]) const;
	// A point light's faces are rendered this much wider than 90 degrees, in
	// texels each side, so a lookup at a face's edge still has neighbours to
	// filter over - the seam between faces otherwise. The receiver derives
	// the same cotangent from the atlas texel size.
	static constexpr float kGuardTexels = 2.f;
	float pointCot() const { return 1.f - 2.f * kGuardTexels / float(faceSize_); }
	// (1 / (2 * slots), the v sign for this backend, one atlas texel in uv).
	const float* info() const { return info_; }

	bgfx::TextureHandle texture() const { return depth_; }
	bgfx::ProgramHandle program() const { return program_; }
	static constexpr uint64_t kState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
	// Receiver offsets, in face texels at the receiver's distance.
	static constexpr float kNormalOffset = 1.5f;
	static constexpr float kLightOffset = 1.0f;
	static constexpr int kMaxSlots = 8;

private:
	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::ViewId base_ = 0;
	int faceSize_ = 0;
	int slots_ = 0;
	float info_[4] = {0.f, -1.f, 0.f, 0.f};
	std::vector<int> slotFaces_;
	std::vector<Frustum> faceFrustum_;
	std::vector<float> slotParams_; // 4 per slot
};

} // namespace painful
