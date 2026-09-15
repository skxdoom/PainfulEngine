#pragma once
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace painful {

class SdfField;

// Pf.RendererType 1: the light at model vertices, traced through SdfField's
// distance field on the GPU. Every vertex holds a history texel; each frame a
// budget of vertices is traced along one fixed set of directions and blended in,
// so a still vertex holds still light. The vertex shader reads its texel by the
// vertex's index
// (Shaders/shared_sdfvertex.sh). Docs/Reference/Lighting.md, "Per-vertex tracing".
class SdfVertexLight {
public:
	static constexpr int kSide = 1024; // history texels a side, one a vertex
	static constexpr int kJobWidth = 1024;
	static constexpr int kTracesPerFrame = kJobWidth * 16; // vertices traced a frame, at most
	static constexpr float kBlend = 0.3f; // a new trace's share of a vertex's light

	~SdfVertexLight() { Shutdown(); }
	SdfVertexLight() = default;
	SdfVertexLight(const SdfVertexLight&) = delete;
	SdfVertexLight& operator=(const SdfVertexLight&) = delete;

	// False without compute or R32U images to read and write; type 1 then stays off.
	bool Init(const std::string& shaderDir);
	void Shutdown();
	// A level went away: every slot is free again, and generation() changes so
	// a holder of slots can tell.
	void Clear();
	uint32_t generation() const { return generation_; }
	// A run of `count` consecutive slots; -1 when no run that long is free.
	int Allocate(int count);
	void Release(int first, int count);
	// The frame Queue files vertices under, and whether that frame was traced: a
	// frame before the field is published is dropped whole.
	uint32_t frame() const { return frame_; }
	bool WasTraced(uint32_t frame) const;
	// How many more vertices this frame can take.
	int budget() const { return std::max(0, kTracesPerFrame - jobs_ - reserved_); }
	// Holds `count` of this frame's budget back from budget() until Unreserve:
	// the models draw first, and the debug grid after them would get none.
	void Reserve(int count) { reserved_ = std::min(count, kTracesPerFrame); }
	void Unreserve() { reserved_ = 0; }
	// One vertex to trace this frame, world space; `reset` forgets its history.
	void Queue(const Vec3& pos, const Vec3& normal, int slot, bool reset);
	// Uploads the frame's vertices and traces them in `view`, a view after every
	// draw that reads the history.
	void Dispatch(const SdfField& field, bgfx::ViewId view);
	// SdfField::lightGeneration at the last dispatch.
	uint32_t fieldGeneration() const { return fieldGeneration_; }
	// For a draw: the history at `stage` and u_sdfVertex, the first slot of the
	// buffer drawn (negative: read nothing).
	void Bind(uint8_t stage, int firstSlot) const;

private:
	bool ok_ = false;
	uint32_t generation_ = 1;
	std::vector<std::pair<int, int>> free_; // first slot, count; sorted by first
	std::vector<float> jobPos_; // xyz, slot
	std::vector<float> jobNormal_; // xyz, 1 to reset
	int jobs_ = 0;
	int reserved_ = 0;
	uint32_t frame_ = 1;
	// For the log: vertices traced and the GPU frame, summed over dispatches.
	double statJobs_ = 0.0, statGpuMs_ = 0.0;
	uint32_t statFrames_ = 0, statSayAt_ = 60;
	uint64_t traced_ = 0; // bit n: frame_ - 1 - n was traced
	uint32_t fieldGeneration_ = 0;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	// R32U: rgb as 10 bits each over 0..4 (irradiance / pi), bit 30 set once traced
	// inside the fields.
	bgfx::TextureHandle history_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle jobPosTexture_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle jobNormalTexture_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sHistory_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sJobPos_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sJobNormal_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uJob_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uBlend_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uVertex_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
