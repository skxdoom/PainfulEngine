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

class CollisionMesh;
class TextureCache;

// Pf.RendererType 1: a model's ambient traced through a distance field of the
// world about the camera. The world's light is baked, so what reaches a surface
// never changes: nested cascades of voxels hold it with the nearest surface
// voxel per voxel, each twice the size of the last at the same resolution, and
// each model sphere-traces rays from its centre into SH, every step reading the
// finest cascade that holds it. A model outside every cascade takes the box
// ambient. Docs/Reference/Lighting.md, "Distance field ambient".
class SdfLighting {
public:
	static constexpr int kWindow = 128; // voxels a side, every cascade
	static constexpr int kCascades = 3;
	static constexpr float kVoxel0 = 0.25f; // the finest cascade's voxel, world units
	static constexpr int kRays = 32;
	static float VoxelSize(int cascade) { return kVoxel0 * float(1 << cascade); }

	SdfLighting();
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
	// adopts finished cascades, and starts the finest one whose centre the
	// camera is a quarter of its size from.
	void Update(const Vec3& camera);

	bool ready() const {
		for (const auto& c : current_)
			if (c) return true;
		return false;
	}
	// SdfAlbedo, 0..1: how much of a surface's albedo the field's light keeps.
	// 0 holds the light the lightmap says arrives, 1 what leaves the surface. A
	// change rebuilds every cascade.
	void SetAlbedo(float k) { albedo_ = k; }
	// The sky as a light (SkyCapture's map, RGB per cell, row 0 straight up)
	// for a ray that leaves the outer cascade with nothing in `collision` in its
	// way: each ray takes the sky averaged over its own share of the sphere. Any
	// other ray keeps the box ambient. Both are forgotten by Clear, and a new
	// sky re-traces every model.
	void SetSky(const std::vector<float>& map, int width, int height);
	void SetCollision(const CollisionMesh* collision) { collision_ = collision; }
	// SdfSkyGain scales the sky's light; SdfSkyHighlight lifts its brightest
	// cells, the clipped sun, by up to that factor at white. A change re-traces.
	void SetSkyGain(float gain, float highlight);
	// How much of a light `toLight` of it a model inside [lo, hi] sees, 0..1:
	// three rays from low to high on its vertical axis, through the cascades by
	// the trace's rule and on through the collision mesh.
	float SunVisibility(const Vec3& lo, const Vec3& hi, const Vec3& toLight) const;
	// Changes whenever a cascade is adopted, so a cached trace can tell.
	uint32_t generation() const { return generation_; }
	// L2 SH at `pos`, 9 x RGB with the cosine lobe applied, rays that find
	// nothing taking `fallback`. `weight` is 0..1, how far inside the outermost
	// cascade holding pos it is: 0 means the box ambient alone. False while no
	// cascade is ready.
	bool Trace(const Vec3& pos, const Vec3& fallback, float sh[27], float& weight) const;
	// World units from pos to the nearest surface voxel's centre in the finest
	// cascade holding it; negative outside them all, or with no surface there.
	float Distance(const Vec3& pos) const;

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
public:
	// The light at one surface voxel, binned by the facing of what wrote it
	// (+X -X +Y -Y +Z -Z), so the two sides of a wall stay apart.
	struct Surface {
		float light[6][3] = {};
		float weight[6] = {};
	};
	struct Volume {
		Vec3 origin; // the corner of voxel (0, 0, 0)
		Vec3 centre;
		float voxel = kVoxel0;
		int cascade = 0;
		uint32_t id = 0; // unique per adoption, for the debug view's uploads
		std::vector<int32_t> nearest; // per voxel, the closest surface; -1 none
		std::vector<uint32_t> coords; // per surface, x | y << 7 | z << 14
		std::vector<Surface> surfaces;
		double voxelizeMs = 0.0, distanceMs = 0.0;
		float albedo = 0.f; // the SdfAlbedo it was voxelized with
	};
	// One cascade as the traces read it, for the debug views; null before its first.
	const Volume* cascade(int k) const { return current_[k].get(); }

private:
	void BuildScene();
	void Worker();
	std::unique_ptr<Volume> BuildVolume(int cascade, const Vec3& centre, float albedo) const;
	Vec3 Radiance(const Tri& t, float a, float b, float albedo) const;
	// The finest ready cascade holding world point p; -1 none.
	int Containing(const Vec3& p) const;
	// The trace's step and hit rule from world point p along d: the surface hit
	// (p there, `cascade` its cascade), -1 when the ray leaves every cascade (p
	// where), -2 out of steps.
	int March(Vec3& p, const Vec3& d, int& cascade) const;
	bool SeesAlong(const Vec3& from, const Vec3& d) const;
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
	Vec3 dirs_[kRays];

	std::thread worker_;
	std::mutex mutex_;
	std::condition_variable wake_;
	bool quit_ = false, jobPending_ = false, busy_ = false;
	Vec3 jobCentre_;
	int jobCascade_ = 0;
	float jobAlbedo_ = 0.f;
	float albedo_ = 0.f; // SdfAlbedo, main thread
	std::vector<float> sky_; // main thread, as Trace; empty is no sky
	Vec3 skyCone_[kRays]; // the sky over each ray's share of the sphere
	int skyWidth_ = 0, skyHeight_ = 0;
	float skyGain_ = 1.f, skyHighlight_ = 0.f;
	const CollisionMesh* collision_ = nullptr;
	// What the rays found, for the log: a diagnostic, not a rule.
	struct RayStats {
		size_t hit = 0, sky = 0, blocked = 0, capped = 0, noSky = 0, outside = 0;
		size_t start[kCascades] = {}; // traces whose centre the cascade was finest for
	};
	mutable RayStats rays_;
	std::unique_ptr<Volume> finished_; // handed over under mutex_
	std::unique_ptr<Volume> current_[kCascades]; // main thread only
	uint32_t generation_ = 0;
	uint32_t adoptions_ = 0;

	mutable size_t traces_ = 0;
	size_t sayTracesAt_ = 1; // the next trace count the log reports at
	mutable double traceSeconds_ = 0.0;
};

} // namespace painful
