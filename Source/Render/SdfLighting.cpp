#include "SdfLighting.h"
#include "TextureCache.h"
#include "../World/CollisionMesh.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <map>

namespace painful {

extern bx::DefaultAllocator g_allocator;

namespace {

using Clock = std::chrono::steady_clock;
constexpr float kBucket = 8.f; // world units, for finding the triangles in a cascade
constexpr float kSkyReach = 4000.f; // how far past the cascades a ray is tested for the sky
constexpr float kSkyHighlightFrom = 0.6f; // sky luminance SdfSkyHighlight starts lifting at
const Vec3 kAxes[6] = {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0},
		Vec3{0, 0, 1}, Vec3{0, 0, -1}};

double MsSince(Clock::time_point t) {
	return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

int64_t BucketKey(int x, int y, int z) {
	return (int64_t(x & 0x1fffff) << 42) | (int64_t(y & 0x1fffff) << 21) | int64_t(z & 0x1fffff);
}

// Real L2 SH, in the order and scale of Shaders/shared_sh.sh.
void ShBasis(const Vec3& d, float out[9]) {
	out[0] = 0.282095f;
	out[1] = 0.488603f * d.y;
	out[2] = 0.488603f * d.z;
	out[3] = 0.488603f * d.x;
	out[4] = 1.092548f * d.x * d.y;
	out[5] = 1.092548f * d.y * d.z;
	out[6] = 0.315392f * (3.f * d.z * d.z - 1.f);
	out[7] = 1.092548f * d.x * d.z;
	out[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

// One mip as RGBA8: the smallest still 8 texels wide (an average), or the
// full image (a lightmap).
bool DecodeTexture(TextureCache& textures, const std::string& name, const std::string& hint,
		bool smallest, int& w, int& h, std::vector<uint8_t>& rgba) {
	const std::string path = textures.Resolve(name, hint);
	std::vector<uint8_t> data;
	if (path.empty() || !ReadFile(path, data) || data.empty()) return false;
	bimg::ImageContainer* image =
		bimg::imageParse(&g_allocator, data.data(), uint32_t(data.size()));
	if (!image) return false;
	uint8_t lod = 0;
	if (smallest)
		while (lod + 1 < image->m_numMips &&
				std::min(image->m_width >> (lod + 1), image->m_height >> (lod + 1)) >= 8)
			++lod;
	bimg::ImageMip mip;
	const bool ok = bimg::imageGetRawData(*image, 0, lod, image->m_data, image->m_size, mip);
	if (ok) {
		w = int(mip.m_width);
		h = int(mip.m_height);
		rgba.resize(size_t(w) * size_t(h) * 4);
		bimg::imageDecodeToRgba8(&g_allocator, rgba.data(), mip.m_data, mip.m_width, mip.m_height,
				mip.m_width * 4, mip.m_format);
	}
	bimg::imageFree(image);
	return ok;
}

bool Inside(const SdfLighting::Volume& v, const Vec3& p) {
	const float span = float(SdfLighting::kWindow) * v.voxel;
	return p.x >= v.origin.x && p.y >= v.origin.y && p.z >= v.origin.z && p.x < v.origin.x + span &&
			p.y < v.origin.y + span && p.z < v.origin.z + span;
}

} // namespace

SdfLighting::SdfLighting() {
	// Spherical Fibonacci: kRays directions spread evenly over the sphere.
	const float golden = kPi * (3.f - std::sqrt(5.f));
	for (int i = 0; i < kRays; ++i) {
		const float y = 1.f - (float(i) + 0.5f) * 2.f / float(kRays);
		const float r = std::sqrt(std::max(0.f, 1.f - y * y));
		const float a = golden * float(i);
		dirs_[i] = Vec3{std::cos(a) * r, y, std::sin(a) * r};
	}
}

void SdfLighting::SetLevel(const MapMesh* map, float worldScale, bool overbright,
		TextureCache* textures, const std::string& levelHint) {
	Clear();
	map_ = map;
	worldScale_ = worldScale;
	overbright_ = overbright;
	textures_ = textures;
	levelHint_ = levelHint;
}

void SdfLighting::Clear() {
	if (worker_.joinable()) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			quit_ = true;
		}
		wake_.notify_all();
		worker_.join();
	}
	quit_ = jobPending_ = busy_ = false;
	finished_.reset();
	for (auto& c : current_) c.reset();
	tris_.clear();
	materials_.clear();
	lightmaps_.clear();
	buckets_.clear();
	sceneBuilt_ = false;
	map_ = nullptr;
	textures_ = nullptr;
	levelHint_.clear();
	sky_.clear();
	collision_ = nullptr;
	// generation_ keeps counting, so a trace cached against the last level
	// never matches a cascade of this one.
}

void SdfLighting::BuildScene() {
	const Clock::time_point t0 = Clock::now();
	const MapMesh& map = *map_;
	const float sign = WindingSign(map);
	std::map<std::string, Vec3> albedoOf;
	std::map<std::string, int> lightmapOf;
	size_t lightmapBytes = 0;

	auto albedo = [&](const std::string& name) {
		if (name.empty()) return Vec3{1.f, 1.f, 1.f};
		const auto it = albedoOf.find(name);
		if (it != albedoOf.end()) return it->second;
		Vec3 avg{1.f, 1.f, 1.f};
		int w = 0, h = 0;
		std::vector<uint8_t> px;
		if (DecodeTexture(*textures_, name, levelHint_, true, w, h, px) && w > 0 && h > 0) {
			double sum[3] = {0.0, 0.0, 0.0};
			const size_t n = size_t(w) * size_t(h);
			for (size_t i = 0; i < n; ++i)
				for (int c = 0; c < 3; ++c) sum[c] += px[i * 4 + c];
			avg = Vec3{float(sum[0] / (255.0 * n)), float(sum[1] / (255.0 * n)),
					float(sum[2] / (255.0 * n))};
		}
		albedoOf[name] = avg;
		return avg;
	};
	auto lightmap = [&](const std::string& name) {
		const auto it = lightmapOf.find(name);
		if (it != lightmapOf.end()) return it->second;
		Lightmap lm;
		int index = -1;
		if (DecodeTexture(*textures_, name, levelHint_, false, lm.w, lm.h, lm.rgba)) {
			index = int(lightmaps_.size());
			lightmapBytes += lm.rgba.size();
			lightmaps_.push_back(std::move(lm));
		}
		lightmapOf[name] = index;
		return index;
	};

	for (const MapObject& o : map.objects) {
		if (o.vertexCount() == 0 || o.indices.empty()) continue;
		// What WorldRenderer::Upload draws as solid world: no helpers, no
		// bodies (EntityRenderer draws those), no water or see-through layers.
		if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone") ||
				o.nameHas("vollight") || o.nameHas("volfog") || o.nameHas("barrier") ||
				o.nameHas("water") || o.nameHas("trans") || o.nameHas("decal") || o.isActiveMesh())
			continue;

		struct Run {
			size_t first, count;
			uint32_t material;
		};
		std::vector<Run> runs;
		if (o.materials.empty()) {
			runs.push_back({0, o.indices.size(), uint32_t(materials_.size())});
			materials_.push_back(SurfaceMaterial());
		}
		for (const Material& m : o.materials) {
			SurfaceMaterial sm;
			// The slot choice WorldRenderer::Upload makes: four filled slots with
			// a tiled second or third are a terrain blend.
			int lightSlot = 1, blendSlot = -1;
			if (o.uvChannels == 2 && !m.slots[0].empty() && !m.slots[1].empty() &&
					!m.slots[2].empty() && !m.slots[3].empty()) {
				blendSlot = m.slots[2].scaleU > 1.5f ? 2 : (m.slots[1].scaleU > 1.5f ? 1 : -1);
				if (blendSlot > 0) lightSlot = blendSlot == 2 ? 1 : 3;
			}
			sm.albedo = albedo(m.slots[0].name);
			if (blendSlot > 0) sm.albedo = (sm.albedo + albedo(m.slots[blendSlot].name)) * 0.5f;
			if (o.uvChannels == 2 && !m.slots[lightSlot].empty()) {
				sm.lightmap = lightmap(m.slots[lightSlot].name);
				sm.lightScale = overbright_ ? 2.f : 1.f;
			}
			runs.push_back({m.firstIndex, size_t(m.triangleCount) * 3, uint32_t(materials_.size())});
			materials_.push_back(sm);
		}

		for (const Run& run : runs) {
			const size_t end = std::min(run.first + run.count, o.indices.size());
			for (size_t t = run.first; t + 2 < end; t += 3) {
				Vec3 p[3];
				Tri tri;
				bool ok = true;
				for (int c = 0; c < 3; ++c) {
					const size_t vi = o.indices[t + c];
					if (vi >= o.vertexCount()) { ok = false; break; }
					o.position(vi, p[c]);
					p[c] = o.transform.TransformPoint(p[c]) * worldScale_;
					if (o.uvChannels == 2) o.uv1(vi, tri.uv[c]);
					else tri.uv[c][0] = tri.uv[c][1] = 0.f;
				}
				if (!ok) continue;
				tri.p0 = p[0];
				tri.e1 = p[1] - p[0];
				tri.e2 = p[2] - p[0];
				const Vec3 n = Cross(tri.e1, tri.e2) * sign;
				const float len = n.Length();
				if (len < 1e-8f) continue;
				tri.normal = n / len;
				tri.material = run.material;
				const uint32_t index = uint32_t(tris_.size());
				tris_.push_back(tri);
				const Vec3 lo = Min(Min(p[0], p[1]), p[2]), hi = Max(Max(p[0], p[1]), p[2]);
				for (int z = int(std::floor(lo.z / kBucket)); z <= int(std::floor(hi.z / kBucket)); ++z)
					for (int y = int(std::floor(lo.y / kBucket)); y <= int(std::floor(hi.y / kBucket)); ++y)
						for (int x = int(std::floor(lo.x / kBucket)); x <= int(std::floor(hi.x / kBucket)); ++x)
							buckets_[BucketKey(x, y, z)].push_back(index);
			}
		}
	}
	LogInfo("distance field: %zu triangles, %zu materials, %zu lightmaps (%.1f MB) in %.0f ms",
			tris_.size(), materials_.size(), lightmaps_.size(), double(lightmapBytes) / 1048576.0,
			MsSince(t0));
}

// The light a surface sends: its lightmap (what arrives) tinted toward its
// albedo by `albedo`. Unlightmapped geometry is drawn at full albedo and sends
// that. Docs/Reference/Lighting.md, "Distance field ambient".
Vec3 SdfLighting::Radiance(const Tri& t, float a, float b, float albedo) const {
	const SurfaceMaterial& m = materials_[t.material];
	if (m.lightmap < 0) return m.albedo;
	const Lightmap& lm = lightmaps_[size_t(m.lightmap)];
	const float u = t.uv[0][0] + (t.uv[1][0] - t.uv[0][0]) * a + (t.uv[2][0] - t.uv[0][0]) * b;
	const float v = t.uv[0][1] + (t.uv[1][1] - t.uv[0][1]) * a + (t.uv[2][1] - t.uv[0][1]) * b;
	const int x = std::clamp(int(std::floor(u * float(lm.w))), 0, lm.w - 1);
	const int y = std::clamp(int(std::floor(v * float(lm.h))), 0, lm.h - 1);
	const uint8_t* px = &lm.rgba[(size_t(y) * size_t(lm.w) + size_t(x)) * 4];
	const float k = m.lightScale / 255.f;
	const Vec3 tint = Vec3{1.f, 1.f, 1.f} + (m.albedo - Vec3{1.f, 1.f, 1.f}) * albedo;
	return Vec3{tint.x * px[0] * k, tint.y * px[1] * k, tint.z * px[2] * k};
}

std::unique_ptr<SdfLighting::Volume> SdfLighting::BuildVolume(int cascade, const Vec3& centre,
		float albedo) const {
	auto vol = std::make_unique<Volume>();
	vol->albedo = albedo;
	vol->cascade = cascade;
	const float voxel = VoxelSize(cascade);
	vol->voxel = voxel;
	const int N = kWindow;
	const float span = float(N) * voxel;
	// Snapped to four of its own voxels, so every cascade's grid lines up.
	const float snap = 4.f * voxel;
	vol->centre = Vec3{std::floor(centre.x / snap) * snap, std::floor(centre.y / snap) * snap,
			std::floor(centre.z / snap) * snap};
	vol->origin = vol->centre - Vec3(span * 0.5f);
	const Vec3 lo = vol->origin, hi = vol->origin + Vec3(span);
	const size_t total = size_t(N) * size_t(N) * size_t(N);
	auto voxelIndex = [N](int x, int y, int z) {
		return (size_t(z) * size_t(N) + size_t(y)) * size_t(N) + size_t(x);
	};

	// Surfaces: every triangle in the cascade sampled at half a voxel.
	Clock::time_point t0 = Clock::now();
	std::vector<int32_t>& nearest = vol->nearest;
	nearest.assign(total, -1);
	std::vector<uint8_t> seen(tris_.size(), 0);
	for (int bz = int(std::floor(lo.z / kBucket)); bz <= int(std::floor(hi.z / kBucket)); ++bz)
	for (int by = int(std::floor(lo.y / kBucket)); by <= int(std::floor(hi.y / kBucket)); ++by)
	for (int bx = int(std::floor(lo.x / kBucket)); bx <= int(std::floor(hi.x / kBucket)); ++bx) {
		const auto bucket = buckets_.find(BucketKey(bx, by, bz));
		if (bucket == buckets_.end()) continue;
		for (uint32_t index : bucket->second) {
			if (seen[index]) continue;
			seen[index] = 1;
			const Tri& t = tris_[index];
			const Vec3 p1 = t.p0 + t.e1, p2 = t.p0 + t.e2;
			const Vec3 tlo = Min(Min(t.p0, p1), p2), thi = Max(Max(t.p0, p1), p2);
			if (thi.x < lo.x || thi.y < lo.y || thi.z < lo.z || tlo.x > hi.x || tlo.y > hi.y ||
					tlo.z > hi.z)
				continue;
			float facing[6];
			for (int k = 0; k < 6; ++k) facing[k] = std::max(0.f, Dot(t.normal, kAxes[k]));
			const float edge = std::max({t.e1.Length(), t.e2.Length(), (t.e2 - t.e1).Length()});
			const int n = std::clamp(int(std::ceil(edge / (voxel * 0.5f))), 1, 1024);
			const float inv = 1.f / float(n);
			auto splat = [&](float a, float b) {
				const Vec3 p = t.p0 + t.e1 * a + t.e2 * b;
				const int x = int(std::floor((p.x - lo.x) / voxel));
				const int y = int(std::floor((p.y - lo.y) / voxel));
				const int z = int(std::floor((p.z - lo.z) / voxel));
				if (x < 0 || y < 0 || z < 0 || x >= N || y >= N || z >= N) return;
				int32_t& s = nearest[voxelIndex(x, y, z)];
				if (s < 0) {
					s = int32_t(vol->surfaces.size());
					vol->surfaces.emplace_back();
					vol->coords.push_back(uint32_t(x) | uint32_t(y) << 7 | uint32_t(z) << 14);
				}
				const Vec3 light = Radiance(t, a, b, albedo);
				Surface& surface = vol->surfaces[size_t(s)];
				for (int k = 0; k < 6; ++k) {
					if (facing[k] <= 0.f) continue;
					surface.light[k][0] += light.x * facing[k];
					surface.light[k][1] += light.y * facing[k];
					surface.light[k][2] += light.z * facing[k];
					surface.weight[k] += facing[k];
				}
			};
			// Two samples per cell of an n x n split of the triangle.
			for (int i = 0; i < n; ++i) {
				for (int j = 0; i + j < n; ++j) {
					splat((float(i) + 0.3333f) * inv, (float(j) + 0.3333f) * inv);
					if (i + j + 1 < n) splat((float(i) + 0.6667f) * inv, (float(j) + 0.6667f) * inv);
				}
			}
		}
	}
	for (Surface& s : vol->surfaces)
		for (int k = 0; k < 6; ++k)
			if (s.weight[k] > 0.f)
				for (int c = 0; c < 3; ++c) s.light[k][c] /= s.weight[k];
	vol->voxelizeMs = MsSince(t0);

	// The nearest surface voxel for every voxel: two sweeps over the 26
	// neighbours, each voxel taking a neighbour's surface when it is closer.
	t0 = Clock::now();
	const std::vector<uint32_t>& coords = vol->coords;
	auto dist2 = [&](int32_t s, int x, int y, int z) {
		const uint32_t c = coords[size_t(s)];
		const int dx = int(c & 127) - x, dy = int((c >> 7) & 127) - y, dz = int((c >> 14) & 127) - z;
		return dx * dx + dy * dy + dz * dz;
	};
	// The 13 neighbours that come earlier in z, y, x order.
	int offsets[13][3];
	{
		int k = 0;
		for (int dz = -1; dz <= 0; ++dz)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (dz < 0 || (dy < 0) || (dy == 0 && dx < 0)) {
						if (dz == 0 && dy > 0) continue;
						offsets[k][0] = dx; offsets[k][1] = dy; offsets[k][2] = dz;
						++k;
					}
	}
	auto sweep = [&](int dir) {
		const int start = dir > 0 ? 0 : N - 1, stop = dir > 0 ? N : -1;
		for (int z = start; z != stop; z += dir)
		for (int y = start; y != stop; y += dir)
		for (int x = start; x != stop; x += dir) {
			int32_t& self = nearest[voxelIndex(x, y, z)];
			int bestD2 = self >= 0 ? dist2(self, x, y, z) : INT_MAX;
			if (bestD2 == 0) continue;
			for (const int* o : offsets) {
				const int nx = x + o[0] * dir, ny = y + o[1] * dir, nz = z + o[2] * dir;
				if (nx < 0 || ny < 0 || nz < 0 || nx >= N || ny >= N || nz >= N) continue;
				const int32_t s = nearest[voxelIndex(nx, ny, nz)];
				if (s < 0) continue;
				const int d2 = dist2(s, x, y, z);
				if (d2 < bestD2) {
					bestD2 = d2;
					self = s;
				}
			}
		}
	};
	sweep(1);
	sweep(-1);
	vol->distanceMs = MsSince(t0);
	return vol;
}

void SdfLighting::Worker() {
	for (;;) {
		Vec3 centre;
		float albedo = 0.f;
		int cascade = 0;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [this] { return quit_ || jobPending_; });
			if (quit_) return;
			centre = jobCentre_;
			albedo = jobAlbedo_;
			cascade = jobCascade_;
			jobPending_ = false;
			busy_ = true;
		}
		std::unique_ptr<Volume> vol = BuildVolume(cascade, centre, albedo);
		std::lock_guard<std::mutex> lock(mutex_);
		finished_ = std::move(vol);
		busy_ = false;
	}
}

void SdfLighting::Update(const Vec3& camera) {
	if (!map_ || !textures_) return;
	if (!sceneBuilt_) {
		// On the main thread: the texture reads go through the VFS.
		sceneBuilt_ = true;
		BuildScene();
		worker_ = std::thread([this] { Worker(); });
	}
	std::unique_ptr<Volume> adopted;
	bool idle = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		adopted = std::move(finished_);
		idle = !busy_ && !jobPending_;
	}
	if (adopted) {
		const int k = adopted->cascade;
		adopted->id = ++adoptions_;
		current_[k] = std::move(adopted);
		++generation_;
		const Volume& v = *current_[k];
		static unsigned said[kCascades] = {};
		if (++said[k] <= 2 || said[k] % 20 == 0)
			LogInfo("distance field: cascade %d (%.2f voxels, %.0f units) at %.0f %.0f %.0f, %zu surface voxels, "
					"voxelized in %.0f ms, distances in %.0f ms",
					k, v.voxel, float(kWindow) * v.voxel, v.centre.x, v.centre.y, v.centre.z, v.surfaces.size(),
					v.voxelizeMs, v.distanceMs);
	}
	if (idle) {
		// The finest cascade that is missing, stale, or a quarter of its size off.
		for (int k = 0; k < kCascades; ++k) {
			const Volume* v = current_[k].get();
			const float quarter = float(kWindow) * VoxelSize(k) * 0.25f;
			const bool due = !v || v->albedo != albedo_ || std::fabs(camera.x - v->centre.x) > quarter ||
					std::fabs(camera.y - v->centre.y) > quarter || std::fabs(camera.z - v->centre.z) > quarter;
			if (!due) continue;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				jobCentre_ = camera;
				jobAlbedo_ = albedo_;
				jobCascade_ = k;
				jobPending_ = true;
			}
			wake_.notify_one();
			break;
		}
	}
	if (traces_ >= sayTracesAt_) {
		const double rays = double(std::max<size_t>(1, rays_.hit + rays_.sky + rays_.blocked +
				rays_.capped + rays_.noSky));
		LogInfo("distance field: %zu traces at %.1f us each (%zu outside, started in cascades %zu/%zu/%zu); "
				"rays: %.0f%% hit, %.0f%% sky, %.0f%% blocked by the level, %.0f%% out of steps, %.0f%% no sky",
				traces_, traceSeconds_ * 1e6 / double(traces_), rays_.outside, rays_.start[0], rays_.start[1],
				rays_.start[2], 100.0 * rays_.hit / rays, 100.0 * rays_.sky / rays, 100.0 * rays_.blocked / rays,
				100.0 * rays_.capped / rays, 100.0 * rays_.noSky / rays);
		sayTracesAt_ *= 16;
	}
}

// Each ray's share of the sphere is the map's cells nearer its direction than
// any other ray's; it takes their mean by solid angle. A point sample in the
// ray's direction would drop a small bright sun that falls between two rays.
void SdfLighting::SetSky(const std::vector<float>& map, int width, int height) {
	// The log's counts start again, so they describe traces against this sky.
	rays_ = RayStats();
	traces_ = 0;
	traceSeconds_ = 0.0;
	sayTracesAt_ = 1;
	sky_.clear();
	skyWidth_ = skyHeight_ = 0;
	if (width > 0 && height > 0 && map.size() == size_t(width) * size_t(height) * 3) {
		sky_ = map;
		skyWidth_ = width;
		skyHeight_ = height;
	}
	BuildSkyCones();
	++generation_; // every cached trace took the box ambient instead
}

void SdfLighting::SetSkyGain(float gain, float highlight) {
	if (gain == skyGain_ && highlight == skyHighlight_) return;
	skyGain_ = gain;
	skyHighlight_ = highlight;
	if (sky_.empty()) return;
	BuildSkyCones();
	++generation_;
}

void SdfLighting::BuildSkyCones() {
	for (Vec3& c : skyCone_) c = Vec3{0.f, 0.f, 0.f};
	if (sky_.empty()) return;
	const int width = skyWidth_, height = skyHeight_;
	const std::vector<float>& map = sky_;
	float weight[kRays] = {};
	for (int v = 0; v < height; ++v) {
		const float lat = kPi * (0.5f - (float(v) + 0.5f) / float(height));
		const float cosLat = std::cos(lat);
		for (int u = 0; u < width; ++u) {
			const float lon = 2.f * kPi * ((float(u) + 0.5f) / float(width) - 0.5f);
			const Vec3 d{cosLat * std::cos(lon), std::sin(lat), cosLat * std::sin(lon)};
			int best = 0;
			float bestDot = -2.f;
			for (int r = 0; r < kRays; ++r) {
				const float dot = Dot(d, dirs_[r]);
				if (dot > bestDot) { bestDot = dot; best = r; }
			}
			const float* c = &map[(size_t(v) * size_t(width) + size_t(u)) * 3];
			// The sky is drawn in 0..1, so a painted sun is clipped at white:
			// the highlight lifts the brightest cells above the rest.
			const float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
			const float t = std::clamp((lum - kSkyHighlightFrom) / (1.f - kSkyHighlightFrom), 0.f, 1.f);
			const float boost = skyGain_ * (1.f + skyHighlight_ * t * t);
			skyCone_[best] += Vec3{c[0], c[1], c[2]} * (cosLat * boost);
			weight[best] += cosLat;
		}
	}
	float lo = 1e9f, hi = 0.f;
	for (int r = 0; r < kRays; ++r) {
		if (weight[r] > 0.f) skyCone_[r] = skyCone_[r] / weight[r];
		const float lum = 0.299f * skyCone_[r].x + 0.587f * skyCone_[r].y + 0.114f * skyCone_[r].z;
		lo = std::min(lo, lum);
		hi = std::max(hi, lum);
	}
	LogInfo("distance field: sky over %d rays at gain %.2f, highlight %.2f: luminance %.3f to %.3f",
			kRays, skyGain_, skyHighlight_, lo, hi);
}

int SdfLighting::Containing(const Vec3& p) const {
	for (int k = 0; k < kCascades; ++k)
		if (current_[k] && Inside(*current_[k], p)) return k;
	return -1;
}

int SdfLighting::March(Vec3& p, const Vec3& d, int& cascade) const {
	const int n = kWindow;
	for (int step = 0; step < 384; ++step) {
		const int k = Containing(p);
		if (k < 0) return -1;
		const Volume& vol = *current_[k];
		const Vec3 q = (p - vol.origin) / vol.voxel;
		const int x = std::min(int(q.x), n - 1), y = std::min(int(q.y), n - 1), z = std::min(int(q.z), n - 1);
		// Voxels to this cascade's far face along d.
		auto toFace = [n](float at, float dir) {
			return dir > 1e-6f ? (float(n) - at) / dir : (dir < -1e-6f ? -at / dir : 1e9f);
		};
		const float exit = std::min({toFace(q.x, d.x), toFace(q.y, d.y), toFace(q.z, d.z)});
		const int32_t s = vol.nearest[(size_t(z) * size_t(n) + size_t(y)) * size_t(n) + size_t(x)];
		float stride = 8.f;
		if (s >= 0) {
			const uint32_t c = vol.coords[size_t(s)];
			const Vec3 delta{float(c & 127) + 0.5f - q.x, float((c >> 7) & 127) + 0.5f - q.y,
					float((c >> 14) & 127) + 0.5f - q.z};
			const float dist = delta.Length();
			// A hit is a surface voxel beside the ray and not behind its start.
			if (dist < 1.f && Dot(delta, d) > -0.5f) {
				cascade = k;
				return s;
			}
			stride = std::max(dist - 1.f, 0.5f);
		}
		// Never past this cascade's face: the next point reads whichever holds it.
		p += d * (std::min(stride, exit + 0.01f) * vol.voxel);
	}
	return -2;
}

// Through the cascades by the trace's rule, then on through the collision mesh
// from wherever the ray left them, or stopped.
bool SdfLighting::SeesAlong(const Vec3& from, const Vec3& d) const {
	Vec3 p = from;
	int cascade = 0;
	if (March(p, d, cascade) >= 0) return false;
	return !collision_ || !collision_->Occluded(p, p + d * kSkyReach);
}

float SdfLighting::SunVisibility(const Vec3& lo, const Vec3& hi, const Vec3& toLight) const {
	const float length = toLight.Length();
	if (length < 1e-6f) return 0.f;
	const Vec3 d = toLight / length;
	int seen = 0;
	for (int i = 0; i < 3; ++i) {
		const float f = 0.25f + 0.25f * float(i);
		const Vec3 from{(lo.x + hi.x) * 0.5f, lo.y + (hi.y - lo.y) * f, (lo.z + hi.z) * 0.5f};
		if (SeesAlong(from, d)) ++seen;
	}
	return float(seen) / 3.f;
}

float SdfLighting::Distance(const Vec3& pos) const {
	const int k = Containing(pos);
	if (k < 0) return -1.f;
	const Volume& vol = *current_[k];
	const Vec3 q = (pos - vol.origin) / vol.voxel;
	const int x = std::min(int(q.x), kWindow - 1), y = std::min(int(q.y), kWindow - 1),
			z = std::min(int(q.z), kWindow - 1);
	const int32_t s = vol.nearest[(size_t(z) * size_t(kWindow) + size_t(y)) * size_t(kWindow) + size_t(x)];
	if (s < 0) return -1.f;
	const uint32_t c = vol.coords[size_t(s)];
	const Vec3 delta{float(c & 127) + 0.5f - q.x, float((c >> 7) & 127) + 0.5f - q.y,
			float((c >> 14) & 127) + 0.5f - q.z};
	return delta.Length() * vol.voxel;
}

bool SdfLighting::Trace(const Vec3& pos, const Vec3& fallback, float sh[27], float& weight) const {
	if (!ready()) return false;
	const Clock::time_point t0 = Clock::now();
	std::fill(sh, sh + 27, 0.f);
	// Full weight from 6 units inside the outermost cascade holding pos, none within 2.
	weight = 0.f;
	for (int k = kCascades - 1; k >= 0; --k) {
		const Volume* v = current_[k].get();
		if (!v || !Inside(*v, pos)) continue;
		const Vec3 far = v->origin + Vec3(float(kWindow) * v->voxel);
		const float margin = std::min({pos.x - v->origin.x, pos.y - v->origin.y, pos.z - v->origin.z,
				far.x - pos.x, far.y - pos.y, far.z - pos.z});
		weight = std::clamp((margin - 2.f) / 4.f, 0.f, 1.f);
		break;
	}
	if (weight <= 0.f) {
		++rays_.outside;
		return true;
	}
	++rays_.start[std::max(Containing(pos), 0)];

	for (int r = 0; r < kRays; ++r) {
		const Vec3& d = dirs_[r];
		Vec3 light = fallback;
		Vec3 p = pos;
		int cascade = 0;
		const int s = March(p, d, cascade);
		if (s >= 0) {
			const Surface& surface = current_[cascade]->surfaces[size_t(s)];
			Vec3 sum{0.f, 0.f, 0.f};
			float w = 0.f;
			for (int k = 0; k < 6; ++k) {
				const float f = -Dot(kAxes[k], d);
				if (f <= 0.f || surface.weight[k] <= 0.f) continue;
				sum += Vec3{surface.light[k][0], surface.light[k][1], surface.light[k][2]} * f;
				w += f;
			}
			// Seen only from behind: a back face, which sends nothing.
			light = w > 0.f ? sum / w : Vec3{0.f, 0.f, 0.f};
			++rays_.hit;
		} else if (s == -2) {
			// Out of steps: the box ambient.
			++rays_.capped;
		} else if (sky_.empty()) {
			++rays_.noSky;
		} else {
			// Out of every cascade: the sky over this ray's share of the sphere,
			// when nothing in the level is in the way.
			if (!collision_ || !collision_->Occluded(p, p + d * kSkyReach)) {
				light = skyCone_[r];
				++rays_.sky;
			} else {
				++rays_.blocked;
			}
		}
		float basis[9];
		ShBasis(d, basis);
		for (int k = 0; k < 9; ++k) {
			sh[k * 3] += light.x * basis[k];
			sh[k * 3 + 1] += light.y * basis[k];
			sh[k * 3 + 2] += light.z * basis[k];
		}
	}
	// Monte Carlo over the sphere, then the cosine lobe per band (shared_sh.sh).
	for (int k = 0; k < 9; ++k) {
		const float lobe = k == 0 ? 1.f : (k < 4 ? 2.f / 3.f : 0.25f);
		for (int c = 0; c < 3; ++c) sh[k * 3 + c] *= 4.f * kPi / float(kRays) * lobe;
	}
	++traces_;
	traceSeconds_ += std::chrono::duration<double>(Clock::now() - t0).count();
	return true;
}

} // namespace painful
