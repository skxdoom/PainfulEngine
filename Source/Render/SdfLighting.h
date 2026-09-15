#pragma once
#include "../Assets/Mpk.h"
#include "../Core/Vectors.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace painful {

class TextureCache;

// Pf.RendererType 1, the CPU half: the world's surfaces voxelized on a worker
// thread into sparse lists that Render/SdfProbes turns into distance fields and
// traces on the GPU. The world's light is baked, so what reaches a surface never
// changes: nested cascades about the camera, each twice the size of the last at
// the same resolution, keep the light of their surface voxels, and a cascade
// that moves voxelizes only what entered it. One coarse field of the whole level
// carries the light past them.
// Docs/Reference/Lighting.md, "Distance field ambient".
class SdfLighting {
public:
	static constexpr int kWindow = 128; // voxels a side, every cascade
	static constexpr int kCascades = 3;
	static constexpr float kVoxel0 = 0.25f; // the finest cascade's voxel, world units
	static constexpr int kLevelSide = 255; // the level field's voxels along any axis, at most
	static constexpr int kListWidth = 1024; // the surface list's texture width
	static constexpr int kListTexels = 7; // texels a surface in the list
	static constexpr int kProbeRays = 64; // a probe's rays: SDF_RAYS in Shaders/cs_sdfprobe.sc
	static float VoxelSize(int cascade) { return kVoxel0 * float(1 << cascade); }

	SdfLighting() = default;
	~SdfLighting() { Clear(); }
	SdfLighting(const SdfLighting&) = delete;
	SdfLighting& operator=(const SdfLighting&) = delete;

	// The level to voxelize; nothing is read until the first Update. `map`
	// must stay alive until Clear.
	void SetLevel(const MapMesh* map, float worldScale, bool overbright, TextureCache* textures,
			const std::string& levelHint);
	// Stops the worker and forgets the level.
	void Clear();
	// Per frame while type 1 is on: builds the surface list on first use,
	// adopts what the worker finished and starts the next job - the level
	// field once, then every missing cascade coarsest first, then the finest
	// one whose centre the camera is a quarter of its size from.
	void Update(const Vec3& camera);
	// The worker has a job, queued or running.
	bool building() {
		std::lock_guard<std::mutex> lock(mutex_);
		return busy_ || jobPending_;
	}

	// SdfAlbedo, 0..1: how much of a surface's albedo the field's light keeps.
	// 0 holds the light the lightmap says arrives, 1 what leaves the surface. A
	// change rebuilds every cascade whole.
	void SetAlbedo(float k) { albedo_ = k; }
	// The sky as a light (SkyCapture's map, RGB per cell, row 0 straight up)
	// for a ray that leaves the level: scaled so what it sends onto open ground
	// facing up is what the level field's lightmaps say arrives there, and each
	// direction averaged over the cone one of a probe's kProbeRays rays covers.
	void SetSky(const std::vector<float>& map, int width, int height);
	// SdfSkyGain scales the sky's light on top of that; SdfSkyHighlight lifts
	// its brightest cells, the clipped sun, by up to that factor at white.
	void SetSkyGain(float gain, float highlight);

	// One volume's surfaces as the GPU takes them.
	struct Volume {
		Vec3 origin; // the corner of voxel (0, 0, 0)
		Vec3 centre;
		float voxel = kVoxel0;
		int cascade = -1; // -1 the level field
		int dims[3] = {kWindow, kWindow, kWindow};
		uint32_t id = 0; // unique per adoption
		// RGBA8, kListWidth wide, kListTexels a surface: its voxel (x, y, z),
		// then six light bins as rgb / 2 (+X -X +Y -Y +Z -Z; a: had light).
		// The GPU side moves it out.
		std::vector<uint8_t> list;
		int listHeight = 0;
		size_t surfaces = 0;
		size_t kept = 0; // of them, carried over from the cascade's last build
		bool incremental = false; // false: every surface voxelized afresh
		double voxelizeMs = 0.0;
		float albedo = 0.f; // the SdfAlbedo it was voxelized with
		// The level field: the median luminance of the untinted light arriving
		// on surface voxels that face up with none above them, and how many.
		float openSkyLight = 0.f;
		size_t openSkyVoxels = 0;
	};
	// Null before its first.
	Volume* cascade(int k) { return current_[k].get(); }
	Volume* level() { return level_.get(); }
	// The sky per direction, averaged over a probe ray's cone: RGB per cell of
	// skyWidth() x skyHeight(), row 0 straight up; empty with no sky. The
	// generation changes with every new sky, gain or level.
	const std::vector<float>& skyCones() const { return skyCones_; }
	int skyWidth() const { return skyWidth_; }
	int skyHeight() const { return skyHeight_; }
	uint32_t skyGeneration() const { return skyGeneration_; }
	// What a display colour is multiplied by to be light on the lightmaps' scale:
	// the sky's calibration, 1 without a sky or open ground to measure it by.
	float skyScale() const { return skyScale_; }

private:
	struct Tri {
		Vec3 p0, e1, e2;
		Vec3 normal; // the front face's, unit
		float uv[3][2]; // lightmap coordinates
		uint32_t material = 0;
	};
	struct SurfaceMaterial {
		Vec3 albedo{1.f, 1.f, 1.f};
		int lightmap = -1;
		float lightScale = 1.f;
	};
	struct Lightmap {
		int w = 0, h = 0;
		std::vector<uint8_t> rgba;
	};
	// The light at one surface voxel, binned by the facing of what wrote it,
	// so the two sides of a wall stay apart.
	struct Surface {
		float light[6][3] = {};
		float weight[6] = {};
	};
	// A cascade's last build, the worker's alone: what a move carries over.
	struct Cache {
		bool valid = false;
		Vec3 origin;
		float albedo = 0.f;
		std::vector<int32_t> index; // per voxel, its surface; -1 none
		std::vector<uint32_t> coords; // per surface, x | y << 10 | z << 20
		std::vector<Surface> surfaces;
	};

	void BuildScene();
	void Worker();
	// A cascade about `centre`, or with cascade -1 the whole level.
	std::unique_ptr<Volume> BuildVolume(int cascade, const Vec3& centre, float albedo);
	Vec3 Radiance(const Tri& t, float a, float b, float albedo) const;
	void BuildSkyCones();

	const MapMesh* map_ = nullptr;
	float worldScale_ = 1.f;
	bool overbright_ = false;
	TextureCache* textures_ = nullptr;
	std::string levelHint_;
	bool sceneBuilt_ = false;

	std::vector<Tri> tris_;
	std::vector<SurfaceMaterial> materials_;
	std::vector<Lightmap> lightmaps_;
	std::unordered_map<int64_t, std::vector<uint32_t>> buckets_;
	Vec3 sceneLo_, sceneHi_;

	std::thread worker_;
	std::mutex mutex_;
	std::condition_variable wake_;
	bool quit_ = false, jobPending_ = false, busy_ = false;
	Vec3 jobCentre_;
	int jobCascade_ = 0;
	float jobAlbedo_ = 0.f;
	Cache caches_[kCascades];
	float albedo_ = 0.f; // SdfAlbedo, main thread
	std::vector<float> sky_; // SkyCapture's map; empty is no sky
	std::vector<float> skyCones_;
	int skyWidth_ = 0, skyHeight_ = 0;
	float skyGain_ = 1.f, skyHighlight_ = 0.f;
	uint32_t skyGeneration_ = 0;
	float skyScale_ = 1.f;
	float levelSkyLight_ = 0.f; // the adopted level field's openSkyLight; 0 none
	size_t levelSkyVoxels_ = 0;
	std::unique_ptr<Volume> finished_; // handed over under mutex_
	std::unique_ptr<Volume> current_[kCascades]; // main thread only
	std::unique_ptr<Volume> level_;
	uint32_t adoptions_ = 0;
	unsigned churn_ = 0; // PAINFUL_SDF_CHURN's next cascade
	bool churnShifted_[kCascades] = {}; // and whether its last rebuild was moved
};

} // namespace painful
