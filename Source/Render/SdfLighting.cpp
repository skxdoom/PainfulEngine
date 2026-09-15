#include "SdfLighting.h"
#include "TextureCache.h"
#include "../Core/Debug.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>

namespace painful {

extern bx::DefaultAllocator g_allocator;

namespace {

using Clock = std::chrono::steady_clock;
constexpr float kBucket = 8.f; // world units, for finding the triangles in a cascade
constexpr float kSkyHighlightFrom = 0.6f; // sky luminance SdfSkyHighlight starts lifting at
const Vec3 kAxes[6] = {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0},
		Vec3{0, 0, 1}, Vec3{0, 0, -1}};

double MsSince(Clock::time_point t) {
	return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

int64_t BucketKey(int x, int y, int z) {
	return (int64_t(x & 0x1fffff) << 42) | (int64_t(y & 0x1fffff) << 21) | int64_t(z & 0x1fffff);
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

uint8_t Unorm(float v) {
	return uint8_t(std::clamp(v * 255.f + 0.5f, 0.f, 255.f));
}

} // namespace

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
	for (Cache& c : caches_) c = Cache();
	level_.reset();
	tris_.clear();
	materials_.clear();
	lightmaps_.clear();
	buckets_.clear();
	sceneBuilt_ = false;
	map_ = nullptr;
	textures_ = nullptr;
	levelHint_.clear();
	sky_.clear();
	skyWidth_ = skyHeight_ = 0;
	levelSkyLight_ = 0.f;
	levelSkyVoxels_ = 0;
	BuildSkyCones();
	// adoptions_ keeps counting, so an id uploaded for the last level never
	// matches a volume of this one.
}

void SdfLighting::BuildScene() {
	const Clock::time_point t0 = Clock::now();
	const MapMesh& map = *map_;
	const float sign = WindingSign(map);
	std::map<std::string, Vec3> albedoOf;
	std::map<std::string, int> lightmapOf;
	size_t lightmapBytes = 0;
	sceneLo_ = Vec3(1e30f);
	sceneHi_ = Vec3(-1e30f);

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
				sceneLo_ = Min(sceneLo_, lo);
				sceneHi_ = Max(sceneHi_, hi);
				for (int z = int(std::floor(lo.z / kBucket)); z <= int(std::floor(hi.z / kBucket)); ++z)
					for (int y = int(std::floor(lo.y / kBucket)); y <= int(std::floor(hi.y / kBucket)); ++y)
						for (int x = int(std::floor(lo.x / kBucket)); x <= int(std::floor(hi.x / kBucket)); ++x)
							buckets_[BucketKey(x, y, z)].push_back(index);
			}
		}
	}
	if (tris_.empty()) sceneLo_ = sceneHi_ = Vec3(0.f);
	LogInfo("distance field: %zu triangles, %zu materials, %zu lightmaps (%.1f MB), bounds %.0f %.0f %.0f to "
			"%.0f %.0f %.0f, in %.0f ms",
			tris_.size(), materials_.size(), lightmaps_.size(), double(lightmapBytes) / 1048576.0,
			sceneLo_.x, sceneLo_.y, sceneLo_.z, sceneHi_.x, sceneHi_.y, sceneHi_.z, MsSince(t0));
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
		float albedo) {
	auto vol = std::make_unique<Volume>();
	vol->albedo = albedo;
	vol->cascade = cascade;
	const bool lit = cascade >= 0;
	if (lit) {
		const float voxel = VoxelSize(cascade);
		vol->voxel = voxel;
		// Snapped to four of its own voxels, so every cascade's grid lines up,
		// a move shifts whole voxels and a probe grid's probes keep their places.
		const float snap = 4.f * voxel;
		vol->centre = Vec3{std::floor(centre.x / snap) * snap, std::floor(centre.y / snap) * snap,
				std::floor(centre.z / snap) * snap};
		vol->origin = vol->centre - Vec3(float(kWindow) * voxel * 0.5f);
	} else {
		// The whole level at a power of two units, a voxel spare on every face.
		const Vec3 ext = sceneHi_ - sceneLo_;
		const float longest = std::max({ext.x, ext.y, ext.z});
		float voxel = VoxelSize(kCascades - 1);
		while (longest / voxel + 2.f > float(kLevelSide)) voxel *= 2.f;
		vol->voxel = voxel;
		vol->origin = sceneLo_ - Vec3(voxel);
		vol->centre = (sceneLo_ + sceneHi_) * 0.5f;
		vol->dims[0] = std::min(int(std::ceil(ext.x / voxel)) + 2, kLevelSide);
		vol->dims[1] = std::min(int(std::ceil(ext.y / voxel)) + 2, kLevelSide);
		vol->dims[2] = std::min(int(std::ceil(ext.z / voxel)) + 2, kLevelSide);
	}
	const float voxel = vol->voxel;
	const int nx = vol->dims[0], ny = vol->dims[1], nz = vol->dims[2];
	const Vec3 lo = vol->origin;
	const Vec3 hi = lo + Vec3{float(nx) * voxel, float(ny) * voxel, float(nz) * voxel};
	const size_t total = size_t(nx) * size_t(ny) * size_t(nz);
	auto voxelIndex = [nx, ny](int x, int y, int z) {
		return (size_t(z) * size_t(ny) + size_t(y)) * size_t(nx) + size_t(x);
	};
	auto inside = [nx, ny, nz](int x, int y, int z) {
		return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz;
	};

	// A cascade keeps its last build's surfaces where the two overlap - the
	// voxels line up - and voxelizes only the voxels that entered it.
	const Clock::time_point t0 = Clock::now();
	Cache scratch;
	Cache& cache = lit ? caches_[cascade] : scratch;
	int shift[3] = {0, 0, 0}; // a voxel here is voxel + shift in the last build
	bool reuse = lit && cache.valid && cache.albedo == albedo;
	if (reuse) {
		const Vec3 moved = (vol->origin - cache.origin) / voxel;
		shift[0] = int(std::lround(moved.x));
		shift[1] = int(std::lround(moved.y));
		shift[2] = int(std::lround(moved.z));
		for (int a = 0; a < 3; ++a)
			if (std::abs(shift[a]) >= kWindow) reuse = false;
	}
	std::vector<uint32_t> coords; // per surface, x | y << 10 | z << 20
	std::vector<Surface> surfaces;
	struct Up {
		Vec3 light{0.f, 0.f, 0.f};
		float weight = 0.f;
	};
	std::vector<Up> up; // the level field's, per surface
	cache.index.assign(total, -1);
	if (reuse) {
		for (size_t s = 0; s < cache.coords.size(); ++s) {
			const uint32_t c = cache.coords[s];
			const int x = int(c & 1023) - shift[0], y = int((c >> 10) & 1023) - shift[1],
					z = int((c >> 20) & 1023) - shift[2];
			if (!inside(x, y, z)) continue;
			cache.index[voxelIndex(x, y, z)] = int32_t(coords.size());
			coords.push_back(uint32_t(x) | uint32_t(y) << 10 | uint32_t(z) << 20);
			surfaces.push_back(cache.surfaces[s]);
		}
	}
	const size_t kept = coords.size();
	const Vec3 oldLo = cache.origin, oldHi = cache.origin + Vec3(float(kWindow) * voxel);

	auto voxelize = [&](const Tri& t) {
		const Vec3 p1 = t.p0 + t.e1, p2 = t.p0 + t.e2;
		const Vec3 tlo = Min(Min(t.p0, p1), p2), thi = Max(Max(t.p0, p1), p2);
		if (thi.x < lo.x || thi.y < lo.y || thi.z < lo.z || tlo.x > hi.x || tlo.y > hi.y || tlo.z > hi.z)
			return;
		// Wholly inside the last build: every sample it has is already there.
		if (reuse && tlo.x >= oldLo.x && tlo.y >= oldLo.y && tlo.z >= oldLo.z && thi.x < oldHi.x &&
				thi.y < oldHi.y && thi.z < oldHi.z)
			return;
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
			if (!inside(x, y, z)) return;
			if (reuse && inside(x + shift[0], y + shift[1], z + shift[2])) return;
			int32_t& s = cache.index[voxelIndex(x, y, z)];
			if (s < 0) {
				s = int32_t(coords.size());
				coords.push_back(uint32_t(x) | uint32_t(y) << 10 | uint32_t(z) << 20);
				surfaces.emplace_back();
				if (!lit) up.emplace_back();
			}
			const Vec3 light = Radiance(t, a, b, albedo);
			Surface& surface = surfaces[size_t(s)];
			for (int k = 0; k < 6; ++k) {
				if (facing[k] <= 0.f) continue;
				surface.light[k][0] += light.x * facing[k];
				surface.light[k][1] += light.y * facing[k];
				surface.light[k][2] += light.z * facing[k];
				surface.weight[k] += facing[k];
			}
			// The level field also keeps the untinted light arriving on what faces
			// up, for SetSky to measure the sky against.
			if (!lit && facing[2] > 0.f) {
				const Vec3 arriving = albedo > 0.f ? Radiance(t, a, b, 0.f) : light;
				up[size_t(s)].light += arriving * facing[2];
				up[size_t(s)].weight += facing[2];
			}
		};
		// Two samples per cell of an n x n split of the triangle.
		for (int i = 0; i < n; ++i) {
			for (int j = 0; i + j < n; ++j) {
				splat((float(i) + 0.3333f) * inv, (float(j) + 0.3333f) * inv);
				if (i + j + 1 < n) splat((float(i) + 0.6667f) * inv, (float(j) + 0.6667f) * inv);
			}
		}
	};
	if (lit) {
		std::vector<uint8_t> seen(tris_.size(), 0);
		for (int bz = int(std::floor(lo.z / kBucket)); bz <= int(std::floor(hi.z / kBucket)); ++bz)
		for (int by = int(std::floor(lo.y / kBucket)); by <= int(std::floor(hi.y / kBucket)); ++by)
		for (int bx = int(std::floor(lo.x / kBucket)); bx <= int(std::floor(hi.x / kBucket)); ++bx) {
			const auto bucket = buckets_.find(BucketKey(bx, by, bz));
			if (bucket == buckets_.end()) continue;
			for (uint32_t index : bucket->second) {
				if (seen[index]) continue;
				seen[index] = 1;
				voxelize(tris_[index]);
			}
		}
	} else {
		for (const Tri& t : tris_) voxelize(t);
	}
	for (size_t s = kept; s < surfaces.size(); ++s)
		for (int k = 0; k < 6; ++k)
			if (surfaces[s].weight[k] > 0.f)
				for (int c = 0; c < 3; ++c) surfaces[s].light[k][c] /= surfaces[s].weight[k];
	if (!lit) {
		// Surfaces facing up with no surface voxel above them: the median light the
		// lightmaps say arrives on them is what the open sky gives.
		std::vector<float> lum;
		for (size_t s = 0; s < coords.size(); ++s) {
			if (up[s].weight <= 0.f) continue;
			const uint32_t c = coords[s];
			const int x = int(c & 1023), y = int((c >> 10) & 1023), z = int((c >> 20) & 1023);
			bool open = true;
			for (int above = y + 1; above < ny && open; ++above)
				open = cache.index[voxelIndex(x, above, z)] < 0;
			if (!open) continue;
			const Vec3 e = up[s].light / up[s].weight;
			lum.push_back(0.299f * e.x + 0.587f * e.y + 0.114f * e.z);
		}
		if (!lum.empty()) {
			std::nth_element(lum.begin(), lum.begin() + ptrdiff_t(lum.size() / 2), lum.end());
			vol->openSkyLight = lum[lum.size() / 2];
		}
		vol->openSkyVoxels = lum.size();
	}

	// The list the GPU seeds its distance transform from.
	vol->surfaces = coords.size();
	vol->kept = kept;
	vol->incremental = reuse;
	const size_t texels = std::max<size_t>(coords.size() * kListTexels, 1);
	vol->listHeight = int((texels + kListWidth - 1) / kListWidth);
	vol->list.assign(size_t(kListWidth) * size_t(vol->listHeight) * 4, 0);
	for (size_t s = 0; s < coords.size(); ++s) {
		uint8_t* px = &vol->list[s * kListTexels * 4];
		const uint32_t c = coords[s];
		px[0] = uint8_t(c & 1023);
		px[1] = uint8_t((c >> 10) & 1023);
		px[2] = uint8_t((c >> 20) & 1023);
		px[3] = 255;
		for (int k = 0; k < 6; ++k) {
			uint8_t* bin = px + (1 + k) * 4;
			for (int ch = 0; ch < 3; ++ch) bin[ch] = Unorm(surfaces[s].light[k][ch] * 0.5f);
			bin[3] = surfaces[s].weight[k] > 0.f ? 255 : 0;
		}
	}
	if (lit) {
		cache.valid = true;
		cache.origin = vol->origin;
		cache.albedo = albedo;
		cache.coords = std::move(coords);
		cache.surfaces = std::move(surfaces);
	}
	vol->voxelizeMs = MsSince(t0);
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
	static const bool kChurn = DebugFlag("PAINFUL_SDF_CHURN");
	if (adopted) {
		adopted->id = ++adoptions_;
		const Volume& v = *adopted;
		if (v.cascade < 0) {
			LogInfo("distance field: level field %dx%dx%d at %.2f voxels, %zu surface voxels (%zu facing an open sky, "
					"median light arriving %.3f), voxelized in %.0f ms", v.dims[0], v.dims[1], v.dims[2], v.voxel,
					v.surfaces, v.openSkyVoxels, v.openSkyLight, v.voxelizeMs);
		} else {
			static unsigned said[kCascades] = {};
			if (++said[v.cascade] <= 2 || said[v.cascade] % 20 == 0 || kChurn)
				LogInfo("distance field: cascade %d (%.2f voxels, %.0f units) at %.0f %.0f %.0f, %zu surface voxels "
						"(%zu kept), voxelized in %.0f ms",
						v.cascade, v.voxel, float(kWindow) * v.voxel, v.centre.x, v.centre.y, v.centre.z, v.surfaces,
						v.kept, v.voxelizeMs);
		}
		if (v.cascade < 0) {
			levelSkyLight_ = v.openSkyLight;
			levelSkyVoxels_ = v.openSkyVoxels;
			level_ = std::move(adopted);
			if (!sky_.empty()) BuildSkyCones();
		} else {
			current_[v.cascade] = std::move(adopted);
		}
	}
	if (!idle) return;
	int job = -2;
	if (!level_) {
		job = -1;
	} else {
		for (int k = kCascades - 1; k >= 0 && job == -2; --k)
			if (!current_[k]) job = k;
		for (int k = 0; k < kCascades && job == -2; ++k) {
			const Volume& v = *current_[k];
			const float quarter = float(kWindow) * VoxelSize(k) * 0.25f;
			if (v.albedo != albedo_ || std::fabs(camera.x - v.centre.x) > quarter ||
					std::fabs(camera.y - v.centre.y) > quarter || std::fabs(camera.z - v.centre.z) > quarter)
				job = k;
		}
	}
	Vec3 centre = camera;
	if (job == -2 && kChurn) {
		// Every other rebuild a quarter of the cascade along x, so the moves and
		// the carried-over surfaces are exercised with the camera still.
		job = int(churn_++ % unsigned(kCascades));
		if ((churnShifted_[job] = !churnShifted_[job])) centre.x += float(kWindow) * VoxelSize(job) * 0.25f;
	}
	if (job == -2) return;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		jobCentre_ = centre;
		jobAlbedo_ = albedo_;
		jobCascade_ = job;
		jobPending_ = true;
	}
	wake_.notify_one();
}

void SdfLighting::SetSky(const std::vector<float>& map, int width, int height) {
	sky_.clear();
	skyWidth_ = skyHeight_ = 0;
	if (width > 0 && height > 0 && map.size() == size_t(width) * size_t(height) * 3) {
		sky_ = map;
		skyWidth_ = width;
		skyHeight_ = height;
	}
	BuildSkyCones();
}

void SdfLighting::SetSkyGain(float gain, float highlight) {
	if (gain == skyGain_ && highlight == skyHighlight_) return;
	skyGain_ = gain;
	skyHighlight_ = highlight;
	if (!sky_.empty()) BuildSkyCones();
}

// Each cell takes the sky within the cone a probe ray stands for (a
// kProbeRays-th of the sphere), by solid angle, so a small bright sun between
// two rays still reaches the nearer.
void SdfLighting::BuildSkyCones() {
	++skyGeneration_;
	skyCones_.clear();
	skyScale_ = 1.f;
	if (sky_.empty()) return;
	const int width = skyWidth_, height = skyHeight_;
	const size_t cells = size_t(width) * size_t(height);
	// The sky is drawn in display colours, the lightmaps hold the light arriving:
	// it is scaled so what it sends onto open ground facing up (irradiance / pi,
	// the SH's unit) is what the level field found arriving there.
	double fromAbove = 0.0;
	for (int v = 0; v < height; ++v) {
		const double lat = kPi * (0.5 - (double(v) + 0.5) / double(height));
		if (lat <= 0.0) continue;
		for (int u = 0; u < width; ++u) {
			const float* c = &sky_[(size_t(v) * size_t(width) + size_t(u)) * 3];
			fromAbove += (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) * std::sin(lat) * std::cos(lat);
		}
	}
	fromAbove *= (2.0 * kPi / double(width)) * (kPi / double(height)) / kPi;
	const float calibration = levelSkyLight_ > 0.f && fromAbove > 1e-4 ? float(levelSkyLight_ / fromAbove) : 1.f;
	skyScale_ = calibration;
	std::vector<Vec3> dir(cells), colour(cells);
	std::vector<float> area(cells);
	for (int v = 0; v < height; ++v) {
		const float lat = kPi * (0.5f - (float(v) + 0.5f) / float(height));
		const float cosLat = std::cos(lat);
		for (int u = 0; u < width; ++u) {
			const size_t i = size_t(v) * size_t(width) + size_t(u);
			const float lon = 2.f * kPi * ((float(u) + 0.5f) / float(width) - 0.5f);
			dir[i] = Vec3{cosLat * std::cos(lon), std::sin(lat), cosLat * std::sin(lon)};
			const float* c = &sky_[i * 3];
			// The sky is drawn in 0..1, so a painted sun is clipped at white:
			// the highlight lifts the brightest cells above the rest.
			const float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
			const float t = std::clamp((lum - kSkyHighlightFrom) / (1.f - kSkyHighlightFrom), 0.f, 1.f);
			colour[i] = Vec3{c[0], c[1], c[2]} * (calibration * skyGain_ * (1.f + skyHighlight_ * t * t));
			area[i] = cosLat;
		}
	}
	const float cosCone = 1.f - 2.f / float(kProbeRays);
	skyCones_.assign(cells * 3, 0.f);
	float lo = 1e9f, hi = 0.f;
	for (size_t o = 0; o < cells; ++o) {
		Vec3 sum{0.f, 0.f, 0.f};
		float weight = 0.f;
		for (size_t i = 0; i < cells; ++i) {
			if (Dot(dir[o], dir[i]) < cosCone) continue;
			sum += colour[i] * area[i];
			weight += area[i];
		}
		const Vec3 c = weight > 0.f ? sum / weight : colour[o];
		skyCones_[o * 3] = c.x;
		skyCones_[o * 3 + 1] = c.y;
		skyCones_[o * 3 + 2] = c.z;
		const float lum = 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
		lo = std::min(lo, lum);
		hi = std::max(hi, lum);
	}
	LogInfo("distance field: sky from above %.3f, open ground %.3f (%zu voxels): scaled x%.2f; over %d-ray cones "
			"at gain %.2f, highlight %.2f: luminance %.3f to %.3f", fromAbove, levelSkyLight_, levelSkyVoxels_,
			calibration, kProbeRays, skyGain_, skyHighlight_, lo, hi);
}

} // namespace painful
