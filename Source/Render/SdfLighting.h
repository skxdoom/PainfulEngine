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

// Pf.RendererType 1, the CPU half: one distance field over the whole level,
// built once on a worker thread for Render/SdfField to upload and the vertex
// traces to march. The world's light is baked, so nothing about it depends on
// the camera: every voxel names its nearest surface voxel, and each surface
// voxel keeps the light arriving on it. Docs/Reference/Lighting.md,
// "Distance field ambient".
class SdfLighting {
public:
	static constexpr float kVoxelStep = 0.25f; // voxel sizes are multiples of this, world units
	static constexpr size_t kVoxelBudget = 16000000; // voxels, at most
	static constexpr int kMaxSide = 1023; // voxels along any axis, at most
	static constexpr int kListWidth = 1024; // the surface list's texture width
	static constexpr int kListTexels = 7; // texels a surface in the list
	static constexpr int kSkyConeRays = 64; // the sky is averaged over a 64th of the sphere a direction

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
	// Per frame while the field is wanted: builds the surface list on first use,
	// then the volume once, and again when SdfAlbedo changes.
	void Update();
	// The worker has a job, queued or running.
	bool building() {
		std::lock_guard<std::mutex> lock(mutex_);
		return busy_ || jobPending_;
	}

	// SdfAlbedo, 0..1: how much of a surface's albedo the field's light keeps.
	// 0 holds the light the lightmap says arrives, 1 what leaves the surface. A
	// change rebuilds the volume.
	void SetAlbedo(float k) { albedo_ = k; }
	// The sky as a light (SkyCapture's map, RGB per cell, row 0 straight up)
	// for a ray that leaves the level: scaled so what it sends onto open ground
	// facing up is what the lightmaps say arrives there, and each direction
	// averaged over a kSkyConeRays-th of the sphere.
	void SetSky(const std::vector<float>& map, int width, int height);
	// SdfSkyGain scales the sky's light on top of that; SdfSkyHighlight lifts
	// its brightest cells, the clipped sun, by up to that factor at white.
	void SetSkyGain(float gain, float highlight);

	// The level's volume as the GPU takes it.
	struct Volume {
		Vec3 origin; // the corner of voxel (0, 0, 0)
		float voxel = kVoxelStep;
		int dims[3] = {0, 0, 0};
		uint32_t id = 0; // unique per adoption
		// Per voxel, the list index of its nearest surface voxel stored as a
		// float's bits (R32F; -1 none). The GPU side moves it out.
		std::vector<int32_t> nearest;
		// RGBA8, kListWidth wide, kListTexels a surface: its voxel (the low 8
		// bits of x, y, z; a: their next 2 bits each), then six light bins as
		// rgb / 2 (+X -X +Y -Y +Z -Z; a: had light). The GPU side moves it out.
		std::vector<uint8_t> list;
		int listHeight = 0;
		size_t surfaces = 0;
		// For the log: 8^3 bricks holding a surface voxel, and those or a neighbour.
		size_t bricks = 0, bricksNear = 0, bricksTotal = 0;
		double voxelizeMs = 0.0, distanceMs = 0.0;
		float albedo = 0.f; // the SdfAlbedo it was voxelized with
		// The median luminance of the untinted light arriving on surface voxels
		// that face up with none above them, and how many.
		float openSkyLight = 0.f;
		size_t openSkyVoxels = 0;
	};
	// Null before the first.
	Volume* volume() { return volume_.get(); }
	// The sky per direction, averaged over a cone: RGB per cell of skyWidth() x
	// skyHeight(), row 0 straight up; empty with no sky. The generation changes
	// with every new sky, gain or level.
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

	void BuildScene();
	void Worker();
	std::unique_ptr<Volume> BuildVolume(float albedo) const;
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
	Vec3 sceneLo_, sceneHi_;

	std::thread worker_;
	std::mutex mutex_;
	std::condition_variable wake_;
	bool quit_ = false, jobPending_ = false, busy_ = false;
	float jobAlbedo_ = 0.f;
	float albedo_ = 0.f; // SdfAlbedo, main thread
	std::vector<float> sky_; // SkyCapture's map; empty is no sky
	std::vector<float> skyCones_;
	int skyWidth_ = 0, skyHeight_ = 0;
	float skyGain_ = 1.f, skyHighlight_ = 0.f;
	uint32_t skyGeneration_ = 0;
	float skyScale_ = 1.f;
	float levelSkyLight_ = 0.f; // the adopted volume's openSkyLight; 0 none
	size_t levelSkyVoxels_ = 0;
	std::unique_ptr<Volume> finished_; // handed over under mutex_
	std::unique_ptr<Volume> volume_; // main thread only
	uint32_t adoptions_ = 0;
};

} // namespace painful
