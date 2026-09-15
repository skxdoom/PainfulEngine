#pragma once
#include "SdfLighting.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <vector>

namespace painful {

// Pf.RendererType 1, the GPU half of the level's distance field: SdfLighting's
// brick map, brick atlas and surface list uploaded a slab a frame, so a level's
// worth never lands on one frame, and published once whole; the sky; the fog.
// Shaders/shared_sdf.sh marches it. Docs/Reference/Lighting.md, "Distance field ambient".
class SdfField {
public:
	static constexpr int kSlabBytes = 4 << 20; // texels uploaded a frame, about

	~SdfField() { Shutdown(); }
	SdfField() = default;
	SdfField(const SdfField&) = delete;
	SdfField& operator=(const SdfField&) = delete;

	// False without compute or R32F and RG8 volumes; type 1 then stays off.
	bool Init();
	void Shutdown();
	// A level went away: its textures go and nothing is published.
	void Clear();
	// Once a frame: takes a finished volume, uploads the next slab of it,
	// publishes it the frame after the last, and follows the sky.
	void Update(SdfLighting& sdf);
	bool ready() const { return shown_.voxel > 0.f; }
	// Changes whenever what a trace would find does: a volume, sky or fog. A
	// model whose light had settled traces again.
	uint32_t lightGeneration() const { return lightGeneration_; }
	// The level's fog, which every ray gathers over its length the way the
	// renderer fogs by distance: mode, start, end and density as LevelInfo holds
	// them, colour 0-255. SdfFogGain scales the colour.
	void SetFog(int mode, float start, float end, float density, const Vec3& color255);
	void SetFogGain(float gain);
	// The brick map, the brick atlas, the surface list and the sky, with their
	// uniforms: stages first..first+3.
	void BindSurfaces(uint8_t first) const;

private:
	struct Volume {
		bgfx::TextureHandle map = BGFX_INVALID_HANDLE; // R32F: SdfLighting::Volume::map
		bgfx::TextureHandle bricks = BGFX_INVALID_HANDLE; // RG8: SdfLighting::Volume::atlas
		bgfx::TextureHandle list = BGFX_INVALID_HANDLE; // RGBA8: SdfLighting::Volume::list
		Vec3 origin;
		float voxel = 0.f; // 0 not published
		int dims[3] = {0, 0, 0};
		int cells[3] = {0, 0, 0};
		int atlas[3] = {0, 0, 0};
		int stored = 0; // bricks, the list's first texels
		uint32_t id = 0;
	};
	// A texture going up a slice at a time: z slices of a volume, rows of a 2D one.
	struct Upload {
		bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
		bool volume = false;
		int width = 0, height = 0, depth = 1;
		size_t texelBytes = 0;
		std::vector<uint8_t> data;
		int next = 0;
	};
	static void Release(Volume& v);

	bool ok_ = false;
	Volume shown_;
	Volume loading_;
	std::vector<Upload> uploads_; // loading_'s, in order
	bgfx::TextureHandle sky_ = BGFX_INVALID_HANDLE;
	uint32_t skyGeneration_ = 0;
	uint32_t lightGeneration_ = 1;
	float fog_[4] = {0.f, 0.f, 0.f, 0.f}; // mode, start, end, density
	Vec3 fogColor_{0.f, 0.f, 0.f}; // 0..1
	float fogGain_ = 1.f;
	float skyScale_ = 1.f; // SdfLighting::skyScale, which the fog's colour takes too
	bgfx::TextureHandle empty3D_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle empty2D_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sMap_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sBricks_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sList_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sSky_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uVolume_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uExtent_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uCells_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uAtlas_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uSky_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
