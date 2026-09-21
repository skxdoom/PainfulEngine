#pragma once
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

// The characters' shadows (MDL.CreateShadowMap), each down its own environment
// directional. The original gives every character a 128-texel silhouette,
// projects it onto the world meshes under it and fades it along the light over
// four times the character's height. Here each gets a depth slot in one atlas,
// so nothing between the light and the character darkens; the slots share one
// view, each draw scissored to its own. Docs/Reference/Lighting.md, "Character shadows"
class CharacterShadows {
public:
	~CharacterShadows() { Shutdown(); }
	// Owns GPU handles that Shutdown destroys, so it is not copyable.
	CharacterShadows() = default;
	CharacterShadows(const CharacterShadows&) = delete;
	CharacterShadows& operator=(const CharacterShadows&) = delete;

	struct Caster {
		int instance = -1; // the EntityRenderer slot
		Vec3 toLight;
		float strength = 0.f; // how dark full shadow is, 0..1
		Vec3 reachLo, reachHi; // bounds of everything the shadow can land on
		float fadeStart = 0.f; // dot(p, toLight) at the caster's side nearest the light
		float fadeRate = 0.f; // 1 / (kFadeHeights x height)
		float normalOffset = 0.f, lightOffset = 0.f; // the receiver's lift, world units
		float drawMatrix[16] = {}; // world -> the slot's clip space
		float receiverMatrix[16] = {}; // world -> atlas uv and depth
		float rect[4] = {}; // the slot in atlas uv, inset for the filter
		uint16_t scissor[4] = {};
	};

	// `slots` casters of `slotSize` texels a side, in a square-ish atlas. False,
	// with a log line, without hardware depth compare or a sampleable format.
	bool Init(const std::string& shaderDir, int slotSize, int slots);
	void Shutdown();
	bool ready() const { return bgfx::isValid(fb_); }

	// Forgets last frame's casters and clears the atlas in `view`.
	void BeginFrame(bgfx::ViewId view);
	// A caster bounded by lo..hi, lit from toLight. False when the atlas is full.
	bool Add(int instance, const Vec3& lo, const Vec3& hi, const Vec3& toLight, float strength);
	// The bounds a caster's shadow can reach, for culling before a slot is spent.
	static void Reach(const Vec3& lo, const Vec3& hi, const Vec3& toLight, Vec3& outLo, Vec3& outHi);

	const std::vector<Caster>& casters() const { return casters_; }
	bgfx::ViewId view() const { return view_; }
	bgfx::TextureHandle texture() const { return depth_; }
	bgfx::ProgramHandle program() const { return program_; }
	float texelUv() const { return texelUv_; }
	int slots() const { return slots_; }
	int slotSize() const { return slotSize_; }

	// WorldMesh::RenderShadowPass: the fade plane's scale is 1 / (0.5 x height x 8).
	static constexpr float kFadeHeights = 4.f;

private:
	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::ViewId view_ = 0;
	std::vector<Caster> casters_;
	int slotSize_ = 0, slots_ = 0, cols_ = 0, rows_ = 0;
	float texelUv_ = 0.f;
};

} // namespace painful
