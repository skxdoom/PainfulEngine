#include "SdfLighting.h"
#include "TextureCache.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>

namespace painful {

extern bx::DefaultAllocator g_allocator;

namespace {

using Clock = std::chrono::steady_clock;
constexpr float kSkyHighlightFrom = 0.6f; // sky luminance SdfSkyHighlight starts lifting at
constexpr int kBrick = 8; // voxels a side of a brick, for the log's sparse estimate
const Vec3 kAxes[6] = {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0},
		Vec3{0, 0, 1}, Vec3{0, 0, -1}};

double MsSince(Clock::time_point t) {
	return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
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

// Every voxel's nearest surface voxel, exactly: the separable transform of
// Felzenszwalb and Huttenlocher ("Distance Transforms of Sampled Functions"), an
// axis at a time, each voxel taking the feature that minimises (p - q)^2 + f(q),
// f(q) the squared distance to q's feature over the axes done before. An axis's
// lines are split across threads. `nearest` holds a surface index or -1.
void NearestSurfaces(std::vector<int32_t>& nearest, const std::vector<uint32_t>& coords, int nx, int ny,
		int nz) {
	const int dims[3] = {nx, ny, nz};
	const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
	for (int axis = 0; axis < 3; ++axis) {
		const int n = dims[axis];
		const int across = axis == 0 ? 1 : 0, deep = axis == 2 ? 1 : 2;
		const int lines = dims[across] * dims[deep];
		auto run = [&](int from, int to) {
			std::vector<int32_t> feature(size_t(n) + 1);
			std::vector<double> f(size_t(n) + 1), z(size_t(n) + 2);
			std::vector<int> v(size_t(n) + 1);
			int cell[3] = {0, 0, 0};
			auto index = [&]() {
				return (size_t(cell[2]) * size_t(ny) + size_t(cell[1])) * size_t(nx) + size_t(cell[0]);
			};
			for (int line = from; line < to; ++line) {
				cell[across] = line % dims[across];
				cell[deep] = line / dims[across];
				int k = -1;
				for (int q = 0; q < n; ++q) {
					cell[axis] = q;
					const int32_t s = nearest[index()];
					feature[size_t(q)] = s;
					if (s < 0) continue;
					const uint32_t c = coords[size_t(s)];
					const double dx = double(int(c & 1023) - cell[0]);
					const double dy = double(int((c >> 10) & 1023) - cell[1]);
					const double dz = double(int((c >> 20) & 1023) - cell[2]);
					f[size_t(q)] = dx * dx + dy * dy + dz * dz + double(q) * double(q);
					double meet = -1e30;
					while (k >= 0) {
						meet = (f[size_t(q)] - f[size_t(v[size_t(k)])]) / (2.0 * double(q - v[size_t(k)]));
						if (meet > z[size_t(k)]) break;
						--k;
					}
					++k;
					v[size_t(k)] = q;
					z[size_t(k)] = k == 0 ? -1e30 : meet;
					z[size_t(k) + 1] = 1e30;
				}
				int j = 0;
				for (int p = 0; p < n; ++p) {
					int32_t out = -1;
					if (k >= 0) {
						while (j < k && z[size_t(j) + 1] < double(p)) ++j;
						out = feature[size_t(v[size_t(j)])];
					}
					cell[axis] = p;
					nearest[index()] = out;
				}
			}
		};
		std::vector<std::thread> pool;
		const int per = (lines + threads - 1) / threads;
		for (int t = 0; t < threads; ++t) {
			const int from = t * per, to = std::min(lines, from + per);
			if (from < to) pool.emplace_back(run, from, to);
		}
		for (std::thread& t : pool) t.join();
	}
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
	volume_.reset();
	tris_.clear();
	materials_.clear();
	lightmaps_.clear();
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
				tris_.push_back(tri);
				sceneLo_ = Min(sceneLo_, Min(Min(p[0], p[1]), p[2]));
				sceneHi_ = Max(sceneHi_, Max(Max(p[0], p[1]), p[2]));
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

std::unique_ptr<SdfLighting::Volume> SdfLighting::BuildVolume(float albedo) const {
	auto vol = std::make_unique<Volume>();
	vol->albedo = albedo;
	// The level's bounds, a voxel spare on every face, at the finest multiple of
	// kVoxelStep the voxel budget and the side limit allow.
	const Vec3 ext = sceneHi_ - sceneLo_;
	float voxel = kVoxelStep;
	int nx = 0, ny = 0, nz = 0;
	for (;; voxel += kVoxelStep) {
		nx = int(std::ceil(ext.x / voxel)) + 2;
		ny = int(std::ceil(ext.y / voxel)) + 2;
		nz = int(std::ceil(ext.z / voxel)) + 2;
		if (size_t(nx) * size_t(ny) * size_t(nz) <= kVoxelBudget && nx <= kMaxSide && ny <= kMaxSide &&
				nz <= kMaxSide)
			break;
	}
	vol->voxel = voxel;
	vol->origin = sceneLo_ - Vec3(voxel);
	vol->dims[0] = nx;
	vol->dims[1] = ny;
	vol->dims[2] = nz;
	const Vec3 lo = vol->origin;
	const size_t total = size_t(nx) * size_t(ny) * size_t(nz);
	auto voxelIndex = [nx, ny](int x, int y, int z) {
		return (size_t(z) * size_t(ny) + size_t(y)) * size_t(nx) + size_t(x);
	};

	// Surfaces: every triangle sampled at half a voxel.
	Clock::time_point t0 = Clock::now();
	std::vector<int32_t>& nearest = vol->nearest;
	nearest.assign(total, -1);
	std::vector<uint32_t> coords; // per surface, x | y << 10 | z << 20
	std::vector<Surface> surfaces;
	struct Up {
		Vec3 light{0.f, 0.f, 0.f};
		float weight = 0.f;
	};
	std::vector<Up> up; // per surface, the untinted light arriving on what faces up
	for (const Tri& t : tris_) {
		float facing[6];
		for (int k = 0; k < 6; ++k) facing[k] = std::max(0.f, Dot(t.normal, kAxes[k]));
		const float edge = std::max({t.e1.Length(), t.e2.Length(), (t.e2 - t.e1).Length()});
		const int n = std::clamp(int(std::ceil(edge / (voxel * 0.5f))), 1, 4096);
		const float inv = 1.f / float(n);
		auto splat = [&](float a, float b) {
			const Vec3 p = t.p0 + t.e1 * a + t.e2 * b;
			const int x = int(std::floor((p.x - lo.x) / voxel));
			const int y = int(std::floor((p.y - lo.y) / voxel));
			const int z = int(std::floor((p.z - lo.z) / voxel));
			if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return;
			int32_t& s = nearest[voxelIndex(x, y, z)];
			if (s < 0) {
				s = int32_t(coords.size());
				coords.push_back(uint32_t(x) | uint32_t(y) << 10 | uint32_t(z) << 20);
				surfaces.emplace_back();
				up.emplace_back();
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
			if (facing[2] > 0.f) {
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
	}
	for (Surface& s : surfaces)
		for (int k = 0; k < 6; ++k)
			if (s.weight[k] > 0.f)
				for (int c = 0; c < 3; ++c) s.light[k][c] /= s.weight[k];
	vol->voxelizeMs = MsSince(t0);

	// Surfaces facing up with no surface voxel above them: the median light the
	// lightmaps say arrives on them is what the open sky gives.
	{
		std::vector<float> lum;
		for (size_t s = 0; s < coords.size(); ++s) {
			if (up[s].weight <= 0.f) continue;
			const uint32_t c = coords[s];
			const int x = int(c & 1023), y = int((c >> 10) & 1023), z = int((c >> 20) & 1023);
			bool open = true;
			for (int above = y + 1; above < ny && open; ++above) open = nearest[voxelIndex(x, above, z)] < 0;
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
	// For the log: how much of the volume a sparse layout of bricks would hold.
	{
		const int bx = (nx + kBrick - 1) / kBrick, by = (ny + kBrick - 1) / kBrick, bz = (nz + kBrick - 1) / kBrick;
		std::vector<uint8_t> brick(size_t(bx) * size_t(by) * size_t(bz), 0);
		for (uint32_t c : coords)
			brick[(size_t(((c >> 20) & 1023) / kBrick) * size_t(by) + size_t(((c >> 10) & 1023) / kBrick)) *
					size_t(bx) + size_t((c & 1023) / kBrick)] = 1;
		for (int z = 0; z < bz; ++z)
		for (int y = 0; y < by; ++y)
		for (int x = 0; x < bx; ++x) {
			bool near = false;
			for (int dz = -1; dz <= 1 && !near; ++dz)
			for (int dy = -1; dy <= 1 && !near; ++dy)
			for (int dx = -1; dx <= 1 && !near; ++dx) {
				const int ox = x + dx, oy = y + dy, oz = z + dz;
				if (ox < 0 || oy < 0 || oz < 0 || ox >= bx || oy >= by || oz >= bz) continue;
				near = brick[(size_t(oz) * size_t(by) + size_t(oy)) * size_t(bx) + size_t(ox)] != 0;
			}
			vol->bricks += brick[(size_t(z) * size_t(by) + size_t(y)) * size_t(bx) + size_t(x)];
			vol->bricksNear += near ? 1 : 0;
		}
		vol->bricksTotal = brick.size();
	}

	t0 = Clock::now();
	NearestSurfaces(nearest, coords, nx, ny, nz);
	// The GPU reads the index as a float (R32F), exact to 2^24.
	for (int32_t& s : nearest) {
		const float f = float(s);
		std::memcpy(&s, &f, sizeof(f));
	}
	vol->distanceMs = MsSince(t0);

	// The list the traces read a hit's light from.
	vol->surfaces = coords.size();
	const size_t texels = std::max<size_t>(coords.size() * kListTexels, 1);
	vol->listHeight = int((texels + kListWidth - 1) / kListWidth);
	vol->list.assign(size_t(kListWidth) * size_t(vol->listHeight) * 4, 0);
	for (size_t s = 0; s < coords.size(); ++s) {
		uint8_t* px = &vol->list[s * kListTexels * 4];
		const uint32_t c = coords[s];
		const uint32_t x = c & 1023, y = (c >> 10) & 1023, z = (c >> 20) & 1023;
		px[0] = uint8_t(x & 255);
		px[1] = uint8_t(y & 255);
		px[2] = uint8_t(z & 255);
		px[3] = uint8_t((x >> 8) | (y >> 8) << 2 | (z >> 8) << 4);
		for (int k = 0; k < 6; ++k) {
			uint8_t* bin = px + (1 + k) * 4;
			for (int ch = 0; ch < 3; ++ch) bin[ch] = Unorm(surfaces[s].light[k][ch] * 0.5f);
			bin[3] = surfaces[s].weight[k] > 0.f ? 255 : 0;
		}
	}
	return vol;
}

void SdfLighting::Worker() {
	for (;;) {
		float albedo = 0.f;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [this] { return quit_ || jobPending_; });
			if (quit_) return;
			albedo = jobAlbedo_;
			jobPending_ = false;
			busy_ = true;
		}
		std::unique_ptr<Volume> vol = BuildVolume(albedo);
		std::lock_guard<std::mutex> lock(mutex_);
		finished_ = std::move(vol);
		busy_ = false;
	}
}

void SdfLighting::Update() {
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
		adopted->id = ++adoptions_;
		const Volume& v = *adopted;
		const double mb = double(v.nearest.size() * sizeof(int32_t) + v.list.size()) / 1048576.0;
		LogInfo("distance field: %dx%dx%d voxels of %.2f units, %zu surface voxels (%zu facing an open sky, median "
				"light arriving %.3f), voxelized in %.0f ms, distances in %.0f ms, %.1f MB",
				v.dims[0], v.dims[1], v.dims[2], v.voxel, v.surfaces, v.openSkyVoxels, v.openSkyLight, v.voxelizeMs,
				v.distanceMs, mb);
		LogInfo("distance field: %zu of %zu 8^3 bricks hold a surface voxel, %zu with their neighbours (%.1f%%)",
				v.bricks, v.bricksTotal, v.bricksNear, 100.0 * double(v.bricksNear) / double(std::max<size_t>(v.bricksTotal, 1)));
		levelSkyLight_ = v.openSkyLight;
		levelSkyVoxels_ = v.openSkyVoxels;
		volume_ = std::move(adopted);
		if (!sky_.empty()) BuildSkyCones();
	}
	if (!idle) return;
	if (volume_ && volume_->albedo == albedo_) return;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		jobAlbedo_ = albedo_;
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

// Each cell takes the sky within a cone of a kSkyConeRays-th of the sphere, by
// solid angle, so a small bright sun between two rays still reaches the nearer.
void SdfLighting::BuildSkyCones() {
	++skyGeneration_;
	skyCones_.clear();
	skyScale_ = 1.f;
	if (sky_.empty()) return;
	const int width = skyWidth_, height = skyHeight_;
	const size_t cells = size_t(width) * size_t(height);
	// The sky is drawn in display colours, the lightmaps hold the light arriving:
	// it is scaled so what it sends onto open ground facing up (irradiance / pi,
	// the traces' unit) is what the volume found arriving there.
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
	const float cosCone = 1.f - 2.f / float(kSkyConeRays);
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
			calibration, kSkyConeRays, skyGain_, skyHighlight_, lo, hi);
}

} // namespace painful
