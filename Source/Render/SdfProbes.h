#pragma once
#include "SdfLighting.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

// Pf.RendererType 1, the GPU half. From SdfLighting's surface lists it builds
// each volume's distance field in compute - the voxels seeded, an exact
// Euclidean transform one axis a frame, the field written - and per cascade
// keeps a grid of probes, one every four voxels, traced through the fields into
// L2 SH that the models sample per pixel (Shaders/shared_sdf.sh). A grid whose
// cascade moved keeps the probes it still covers and traces only the new ones.
// Every result is written in the last view of a frame and published the frame
// after, so nothing ever reads half a build, and what the models sample is each
// grid eased toward its latest trace every frame, so no change arrives at once.
// Docs/Reference/Lighting.md, "Distance field ambient".
class SdfProbes {
public:
	static constexpr int kProbes = 32; // a side, every grid: SDF_PROBES in Shaders/shared_sdf.sh
	static constexpr int kSlabs = 7; // RGBA texels a probe
	static constexpr int kProbesPerFrame = 2048; // probes traced a frame, at most
	static constexpr float kEaseSeconds = 0.2f; // the time constant of the grids the models sample

	~SdfProbes() { Shutdown(); }
	SdfProbes() = default;
	SdfProbes(const SdfProbes&) = delete;
	SdfProbes& operator=(const SdfProbes&) = delete;

	// False without compute, 3D textures or the formats; type 1 then stays off.
	bool Init(const std::string& shaderDir);
	void Shutdown();
	// A level went away: its textures go and nothing is published.
	void Clear();
	// Once a frame: publishes what the last frame finished, then does one step
	// of GPU work in `computeView`, a view after every draw that samples the
	// fields - a field build first, a probe grid otherwise.
	void Update(SdfLighting& sdf, bgfx::ViewId computeView);
	bool ready() const;
	// The level's fog, which every probe ray gathers over its length the way the
	// renderer fogs by distance: mode, start, end and density as LevelInfo holds
	// them, colour 0-255. SdfFogGain scales the colour. A change re-traces every grid.
	void SetFog(int mode, float start, float end, float density, const Vec3& color255);
	void SetFogGain(float gain);
	// The fields for a march or a field lookup: stages first..first+2, level first+3.
	void BindFields(uint8_t first) const;
	// The eased grids for shared_sdf.sh's shading half: stages first..first+2.
	void BindShading(uint8_t first) const;
	// The fields with the light and the sky, for a march that reads a hit's
	// light: fields first..first+2, level +3, surfaces +4..+6, lists +7..+9, sky
	// +10, the level field's surfaces +11 and list +12.
	void BindSurfaces(uint8_t first) const;

private:
	struct Volume {
		bgfx::TextureHandle field = BGFX_INVALID_HANDLE; // RGBA8: the way and distance to the nearest surface voxel
		bgfx::TextureHandle seed = BGFX_INVALID_HANDLE; // R32F: a surface voxel's list index, -1 elsewhere
		bgfx::TextureHandle list = BGFX_INVALID_HANDLE; // RGBA8: SdfLighting::Volume::list
		Vec3 origin;
		float voxel = 0.f; // 0 not published
		int dims[3] = {0, 0, 0};
		uint32_t id = 0;
	};
	// A volume's field under construction: seed, the three axes, the field.
	struct Build {
		int volume = -1;
		int step = 0;
		uint32_t id = 0;
		Vec3 origin;
		float voxel = 0.f;
		int dims[3] = {0, 0, 0};
		size_t surfaces = 0, kept = 0;
		bool incremental = false;
		bgfx::TextureHandle list = BGFX_INVALID_HANDLE;
		bgfx::TextureHandle field = BGFX_INVALID_HANDLE; // the published one unless its size changed
		bgfx::TextureHandle seed = BGFX_INVALID_HANDLE;
		int frames = 0;
	};
	struct Grid {
		bgfx::TextureHandle traced = BGFX_INVALID_HANDLE; // what cs_sdfprobe writes
		bgfx::TextureHandle target = BGFX_INVALID_HANDLE; // the latest trace, dilated
		// What the models sample, eased toward the target: shown[current], the
		// other written from it each frame the grid is still easing.
		bgfx::TextureHandle shown[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
		int current = 0;
		Vec3 corner; // probe (0, 0, 0)'s centre as the models sample it
		float spacing = 0.f; // 0 nothing to sample yet
		Vec3 targetCorner; // the same for the target
		float targetSpacing = 0.f; // 0 not traced yet
		float sinceChange = 0.f; // seconds the target has held
		bool settled = true; // the sampled grid equals the target
	};
	struct Box {
		int min[3];
		int size[3];
	};
	enum class Stage { kIdle, kCopy, kTrace, kDilate, kPublish };
	struct Job {
		Stage stage = Stage::kIdle;
		int cascade = 0;
		Vec3 corner;
		float spacing = 0.f;
		int offset[3] = {0, 0, 0}; // a cell here plus this is the same place in the published grid
		std::vector<Box> boxes; // the cells to trace
		size_t box = 0;
		int layer = 0;
		int traced = 0;
		int frames = 0;
		double gpuMs = 0.0;
	};

	void StartBuild(int volume, SdfLighting::Volume& cpu);
	void StepBuild(bgfx::ViewId view);
	void PublishBuild();
	void DispatchClear(bgfx::ViewId view, bgfx::TextureHandle target, const int dims[3]);
	void DispatchSeed(bgfx::ViewId view, bgfx::TextureHandle target, bool indices);
	void StartJob(int cascade);
	void StepJob(bgfx::ViewId view);
	void PublishJob();
	void EaseGrids(bgfx::ViewId view, float seconds);
	void SetFieldUniforms() const;

	bool ok_ = false;
	Volume volumes_[SdfLighting::kCascades + 1]; // the cascades, then the level field
	Build build_;
	bgfx::TextureHandle work_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}; // R32F features, a cascade's size
	bgfx::TextureHandle levelWork_[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}; // the level field's, while it builds
	Grid grids_[SdfLighting::kCascades];
	bool pending_[SdfLighting::kCascades] = {};
	bool pendingFull_[SdfLighting::kCascades] = {}; // nothing of the published grid can be kept
	Job job_;
	int lastGrid_ = SdfLighting::kCascades - 1; // the grid the last job served
	float fog_[4] = {0.f, 0.f, 0.f, 0.f}; // mode, start, end, density
	Vec3 fogColor_{0.f, 0.f, 0.f}; // 0..1
	float fogGain_ = 1.f;
	float skyScale_ = 1.f; // SdfLighting::skyScale, which the fog's colour takes too
	bgfx::TextureHandle sky_ = BGFX_INVALID_HANDLE;
	uint32_t skyGeneration_ = 0;
	double idleGpuMs_ = 0.0;
	// A frame much longer than usual is logged with what the one before it
	// submitted, so a hitch names its cause.
	int64_t lastUpdateUs_ = 0;
	double frameMsTypical_ = 0.0;
	std::string frameNote_;

	bgfx::ProgramHandle trace_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle dilate_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle copy_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle clear_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle seed_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle edt_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle field_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle ease_ = BGFX_INVALID_HANDLE;
	// Bound where a volume, grid or sky is missing, so every sampler has a texture.
	bgfx::TextureHandle empty3D_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle empty2D_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sField_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle sSurface_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle sBins_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle sProbes_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle sLevel_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sLevelSurface_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sLevelBins_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sSky_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sTraced_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sEdtIn_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sList_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sFeatures_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sOld_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sShownOld_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sTarget_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sCoarser_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEase_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEaseOld_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uCoarser_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uVolume_ = BGFX_INVALID_HANDLE; // four
	bgfx::UniformHandle uExtent_ = BGFX_INVALID_HANDLE; // four
	bgfx::UniformHandle uProbeGrid_ = BGFX_INVALID_HANDLE; // three
	bgfx::UniformHandle uBins_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uSky_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uJob_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uJobMin_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uJobSize_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEdt_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uSeed_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uCopy_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
