#pragma once
#include "../Core/Frustum.h"
#include "../World/Lighting.h"
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

// The flashlight's shadow map: one depth target rendered down the beam by the
// world and the models, then tested per pixel by both. The original has no
// equivalent - MDL.CreateShadowMap is a per-actor blob - so this is a
// deviation, recorded in Docs/Reference/Lighting.md, "Shadows".
class ShadowMap {
public:
	~ShadowMap() { Shutdown(); }
	// Owns GPU handles that Shutdown destroys, so it is not copyable.
	ShadowMap() = default;
	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	// False, with a log line, when the backend has no hardware depth compare
	// or no depth format it can sample; the receivers then read "no shadow".
	bool Init(const std::string& shaderDir, int size);
	void Shutdown();
	bool ready() const { return bgfx::isValid(fb_); }

	// Aims the pass down `light` for this frame: the view's target, clear and
	// transform, the caster frustum and the receiver matrix. The projection
	// is Light::UpdateProj's - half-fov acos(coneOuterCos), near 0.1, far Range.
	void Begin(bgfx::ViewId view, const LightSource& light);
	// The other shape: an orthographic box `extent` half-wide about `centre`,
	// reaching `back` toward the light and `forward` away from it, snapped to
	// its own texel grid so the map does not shimmer as the centre moves.
	// Two maps with the same window and different reaches line up in xy.
	void BeginOrtho(bgfx::ViewId view, const Vec3& toLight, const Vec3& centre,
			float extent, float back, float forward);
	// No light this frame.
	void End() { active_ = false; }
	bool active() const { return active_; }

	const Frustum& frustum() const { return frustum_; }
	// World -> shadow uv and depth, the crop for the backend's depth range and
	// texture origin already applied.
	const float* matrix() const { return matrix_; }
	bgfx::TextureHandle texture() const { return depth_; }
	bgfx::ProgramHandle program() const { return program_; }
	int size() const { return size_; }
	// The orthographic map's fixed geometry: one texel in world units, and
	// the direction to the light.
	float texelWorld() const { return texelWorld_; }
	const Vec3& toLight() const { return toLight_; }
	// How dark a receiver goes under a model shadow, 0..1. The models lose
	// their directional term outright; the world, whose light is baked, is
	// darkened by this much instead, scaled by the boxes' directional there.
	void SetStrength(float k) { strength_ = k; }
	float strength() const { return strength_; }
	// The orthographic box ends somewhere in view; the shadows fade out over
	// this fraction of its width at every edge rather than stopping on a line.
	static constexpr float kEdgeFade = 0.15f;

	// The caster state: depth only, BOTH faces. The level meshes are one-sided,
	// so casting back faces alone would leak light through every wall.
	static constexpr uint64_t kState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
	// How far a receiver is lifted off its surface before the lookup, in shadow
	// texels at that depth: along the normal, and toward the light.
	static constexpr float kNormalOffset = 1.5f;
	static constexpr float kLightOffset = 1.0f;

private:
	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	float view_[16] = {};
	float proj_[16] = {};
	float matrix_[16] = {};
	Frustum frustum_ = {};
	Vec3 toLight_{0.f, 1.f, 0.f};
	float texelWorld_ = 0.f;
	float strength_ = 1.f;
	int size_ = 0;
	bool active_ = false;
};

} // namespace painful
