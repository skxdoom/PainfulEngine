#include "SdfProbes.h"
#include "ShaderLoad.h"
#include "../Core/Debug.h"
#include "../Core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace painful {

namespace {

constexpr int kLevel = SdfLighting::kCascades;
constexpr int kBuildSteps = 5; // seed, the three axes, the field
constexpr uint64_t kPoint = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;

// Texels handed to bgfx without a copy: the vector lives until bgfx frees it.
template <typename T>
const bgfx::Memory* Take(std::vector<T>& data) {
	auto* owned = new std::vector<T>(std::move(data));
	return bgfx::makeRef(owned->data(), uint32_t(owned->size() * sizeof(T)),
			[](void*, void* user) { delete static_cast<std::vector<T>*>(user); }, owned);
}

void Destroy(bgfx::TextureHandle& t) {
	if (bgfx::isValid(t)) bgfx::destroy(t);
	t = BGFX_INVALID_HANDLE;
}

void Destroy(bgfx::UniformHandle& u) {
	if (bgfx::isValid(u)) bgfx::destroy(u);
	u = BGFX_INVALID_HANDLE;
}

void Destroy(bgfx::ProgramHandle& p) {
	if (bgfx::isValid(p)) bgfx::destroy(p);
	p = BGFX_INVALID_HANDLE;
}

bool Same(bgfx::TextureHandle a, bgfx::TextureHandle b) {
	return a.idx == b.idx;
}

uint32_t Groups(int n, int per) {
	return uint32_t(std::max(1, (n + per - 1) / per));
}

// The last finished frame's GPU time; 0 where the backend has no timer.
double GpuFrameMs() {
	const bgfx::Stats* s = bgfx::getStats();
	if (!s || s->gpuTimerFreq <= 0 || s->gpuTimeEnd <= s->gpuTimeBegin) return 0.0;
	return double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq);
}

} // namespace

bool SdfProbes::Init(const std::string& shaderDir) {
	const bgfx::Caps* caps = bgfx::getCaps();
	auto has = [caps](bgfx::TextureFormat::Enum f, uint32_t flag) { return (caps->formats[f] & flag) != 0; };
	const bool compute = (caps->supported & BGFX_CAPS_COMPUTE) != 0;
	const bool volumes = (caps->supported & BGFX_CAPS_TEXTURE_3D) != 0 &&
			has(bgfx::TextureFormat::RGBA8, BGFX_CAPS_FORMAT_TEXTURE_3D) &&
			has(bgfx::TextureFormat::R32F, BGFX_CAPS_FORMAT_TEXTURE_3D) &&
			has(bgfx::TextureFormat::RGBA16F, BGFX_CAPS_FORMAT_TEXTURE_3D);
	const bool writes = has(bgfx::TextureFormat::RGBA16F, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) &&
			has(bgfx::TextureFormat::RGBA8, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) &&
			has(bgfx::TextureFormat::R32F, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE);
	if (!compute || !volumes || !writes) {
		LogWarn("distance field: needs compute (%s), RGBA8/R32F/RGBA16F volumes (%s) and image writes to them (%s); "
				"RendererType 1 stays off", compute ? "yes" : "no", volumes ? "yes" : "no", writes ? "yes" : "no");
		return false;
	}
	auto load = [&](const char* name) {
		bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
		const bgfx::ShaderHandle cs = LoadShader(shaderDir, name);
		if (bgfx::isValid(cs)) program = bgfx::createProgram(cs, true);
		return program;
	};
	trace_ = load("cs_sdfprobe");
	dilate_ = load("cs_sdfdilate");
	copy_ = load("cs_sdfprobecopy");
	clear_ = load("cs_sdfclear");
	seed_ = load("cs_sdfseed");
	edt_ = load("cs_sdfedt");
	field_ = load("cs_sdffield");
	ease_ = load("cs_sdfprobeease");

	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		const std::string n = std::to_string(k);
		sField_[k] = bgfx::createUniform(("s_sdfField" + n).c_str(), bgfx::UniformType::Sampler);
		sSurface_[k] = bgfx::createUniform(("s_sdfSurface" + n).c_str(), bgfx::UniformType::Sampler);
		sBins_[k] = bgfx::createUniform(("s_sdfBins" + n).c_str(), bgfx::UniformType::Sampler);
		sProbes_[k] = bgfx::createUniform(("s_sdfProbes" + n).c_str(), bgfx::UniformType::Sampler);
	}
	sLevel_ = bgfx::createUniform("s_sdfLevel", bgfx::UniformType::Sampler);
	sLevelSurface_ = bgfx::createUniform("s_sdfLevelSurface", bgfx::UniformType::Sampler);
	sLevelBins_ = bgfx::createUniform("s_sdfLevelBins", bgfx::UniformType::Sampler);
	sSky_ = bgfx::createUniform("s_sdfSky", bgfx::UniformType::Sampler);
	sTraced_ = bgfx::createUniform("s_sdfTraced", bgfx::UniformType::Sampler);
	sEdtIn_ = bgfx::createUniform("s_sdfEdtIn", bgfx::UniformType::Sampler);
	sList_ = bgfx::createUniform("s_sdfList", bgfx::UniformType::Sampler);
	sFeatures_ = bgfx::createUniform("s_sdfFeatures", bgfx::UniformType::Sampler);
	sOld_ = bgfx::createUniform("s_sdfOld", bgfx::UniformType::Sampler);
	sShownOld_ = bgfx::createUniform("s_sdfShownOld", bgfx::UniformType::Sampler);
	sTarget_ = bgfx::createUniform("s_sdfTarget", bgfx::UniformType::Sampler);
	sCoarser_ = bgfx::createUniform("s_sdfCoarser", bgfx::UniformType::Sampler);
	uEase_ = bgfx::createUniform("u_sdfEase", bgfx::UniformType::Vec4);
	uEaseOld_ = bgfx::createUniform("u_sdfEaseOld", bgfx::UniformType::Vec4);
	uCoarser_ = bgfx::createUniform("u_sdfCoarser", bgfx::UniformType::Vec4);
	uFog_ = bgfx::createUniform("u_sdfFog", bgfx::UniformType::Vec4);
	uFogColor_ = bgfx::createUniform("u_sdfFogColor", bgfx::UniformType::Vec4);
	uVolume_ = bgfx::createUniform("u_sdfVolume", bgfx::UniformType::Vec4, 4);
	uExtent_ = bgfx::createUniform("u_sdfExtent", bgfx::UniformType::Vec4, 4);
	uProbeGrid_ = bgfx::createUniform("u_sdfProbeGrid", bgfx::UniformType::Vec4, SdfLighting::kCascades);
	uBins_ = bgfx::createUniform("u_sdfBins", bgfx::UniformType::Vec4);
	uSky_ = bgfx::createUniform("u_sdfSky", bgfx::UniformType::Vec4);
	uJob_ = bgfx::createUniform("u_sdfJob", bgfx::UniformType::Vec4);
	uJobMin_ = bgfx::createUniform("u_sdfJobMin", bgfx::UniformType::Vec4);
	uJobSize_ = bgfx::createUniform("u_sdfJobSize", bgfx::UniformType::Vec4);
	uEdt_ = bgfx::createUniform("u_sdfEdt", bgfx::UniformType::Vec4);
	uSeed_ = bgfx::createUniform("u_sdfSeed", bgfx::UniformType::Vec4);
	uCopy_ = bgfx::createUniform("u_sdfCopy", bgfx::UniformType::Vec4);

	const uint8_t far[4] = {128, 128, 128, 255};
	empty3D_ = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::RGBA8, kPoint, bgfx::copy(far, 4));
	const float none[4] = {0.f, 0.f, 0.f, 0.f};
	empty2D_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F, kPoint,
			bgfx::copy(none, sizeof(none)));
	bool made = true;
	for (Grid& g : grids_) {
		for (bgfx::TextureHandle* t : {&g.traced, &g.target, &g.shown[0], &g.shown[1]}) {
			*t = bgfx::createTexture3D(uint16_t(kProbes), uint16_t(kProbes), uint16_t(kProbes * kSlabs), false,
					bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_UVW_CLAMP);
			made = made && bgfx::isValid(*t);
		}
	}
	for (bgfx::ProgramHandle p : {trace_, dilate_, copy_, clear_, seed_, edt_, field_, ease_})
		made = made && bgfx::isValid(p);
	ok_ = made;
	if (!ok_) LogWarn("distance field: the compute programs or probe grids did not load; RendererType 1 stays off");
	return ok_;
}

void SdfProbes::Shutdown() {
	Clear();
	for (int v = 0; v < kLevel; ++v) {
		Destroy(volumes_[v].field);
		Destroy(volumes_[v].seed);
	}
	for (bgfx::TextureHandle& t : work_) Destroy(t);
	for (Grid& g : grids_) {
		Destroy(g.traced);
		Destroy(g.target);
		Destroy(g.shown[0]);
		Destroy(g.shown[1]);
	}
	for (int k = 0; k < SdfLighting::kCascades; ++k)
		for (bgfx::UniformHandle* u : {&sField_[k], &sSurface_[k], &sBins_[k], &sProbes_[k]}) Destroy(*u);
	for (bgfx::UniformHandle* u : {&sLevel_, &sLevelSurface_, &sLevelBins_, &sSky_, &sTraced_, &sEdtIn_, &sList_, &sFeatures_, &sOld_, &uVolume_,
			&uExtent_, &uProbeGrid_, &uBins_, &uSky_, &uJob_, &uJobMin_, &uJobSize_, &uEdt_, &uSeed_, &uCopy_, &sShownOld_, &sTarget_, &sCoarser_, &uEase_,
			&uEaseOld_, &uCoarser_, &uFog_, &uFogColor_})
		Destroy(*u);
	Destroy(empty3D_);
	Destroy(empty2D_);
	for (bgfx::ProgramHandle* p : {&trace_, &dilate_, &copy_, &clear_, &seed_, &edt_, &field_, &ease_}) Destroy(*p);
	ok_ = false;
}

void SdfProbes::Clear() {
	if (build_.volume >= 0) {
		const Volume& vol = volumes_[build_.volume];
		Destroy(build_.list);
		if (!Same(build_.field, vol.field)) Destroy(build_.field);
		if (!Same(build_.seed, vol.seed)) Destroy(build_.seed);
	}
	build_ = Build();
	for (Volume& vol : volumes_) {
		Destroy(vol.list);
		vol.voxel = 0.f;
		vol.id = 0;
	}
	// The level field is the level's size; the cascades' textures serve the next.
	Volume& level = volumes_[kLevel];
	Destroy(level.field);
	Destroy(level.seed);
	for (int& d : level.dims) d = 0;
	for (bgfx::TextureHandle& t : levelWork_) Destroy(t);
	for (Grid& g : grids_) {
		g.spacing = g.targetSpacing = 0.f;
		g.settled = true;
	}
	for (int k = 0; k < SdfLighting::kCascades; ++k) pending_[k] = pendingFull_[k] = false;
	job_ = Job();
	lastGrid_ = SdfLighting::kCascades - 1;
	Destroy(sky_);
	// skyGeneration_ stays: SdfLighting's keeps counting across levels.
}

bool SdfProbes::ready() const {
	for (const Grid& g : grids_)
		if (g.spacing > 0.f) return true;
	return false;
}

void SdfProbes::SetFieldUniforms() const {
	float volume[16] = {}, extent[16] = {};
	for (int v = 0; v <= kLevel; ++v) {
		const Volume& g = volumes_[v];
		if (g.voxel <= 0.f) continue;
		volume[v * 4] = g.origin.x;
		volume[v * 4 + 1] = g.origin.y;
		volume[v * 4 + 2] = g.origin.z;
		volume[v * 4 + 3] = g.voxel; // 0 marks a volume not published
		for (int a = 0; a < 3; ++a) extent[v * 4 + a] = float(g.dims[a]);
	}
	bgfx::setUniform(uVolume_, volume, 4);
	bgfx::setUniform(uExtent_, extent, 4);
}

void SdfProbes::BindFields(uint8_t first) const {
	if (!ok_) return;
	SetFieldUniforms();
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		const Volume& v = volumes_[k];
		bgfx::setTexture(uint8_t(first + k), sField_[k], v.voxel > 0.f ? v.field : empty3D_);
	}
	const Volume& level = volumes_[kLevel];
	bgfx::setTexture(uint8_t(first + 3), sLevel_, level.voxel > 0.f ? level.field : empty3D_);
}

void SdfProbes::BindShading(uint8_t first) const {
	if (!ok_) return;
	float grid[4 * SdfLighting::kCascades] = {};
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		const Grid& g = grids_[k];
		bgfx::setTexture(uint8_t(first + k), sProbes_[k], g.spacing > 0.f ? g.shown[g.current] : empty3D_);
		grid[k * 4] = g.corner.x;
		grid[k * 4 + 1] = g.corner.y;
		grid[k * 4 + 2] = g.corner.z;
		grid[k * 4 + 3] = g.spacing;
	}
	bgfx::setUniform(uProbeGrid_, grid, uint16_t(SdfLighting::kCascades));
}

void SdfProbes::SetFog(int mode, float start, float end, float density, const Vec3& color255) {
	const float fog[4] = {float(mode), start, end, density};
	const Vec3 color = color255 / 255.f;
	if (std::equal(fog, fog + 4, fog_) && color.x == fogColor_.x && color.y == fogColor_.y &&
			color.z == fogColor_.z)
		return;
	std::copy(fog, fog + 4, fog_);
	fogColor_ = color;
	for (int k = 0; k < SdfLighting::kCascades; ++k) pending_[k] = pendingFull_[k] = true;
	LogInfo("distance field: probe rays gather fog mode %d from %.1f to %.1f, density %.3f, colour %.2f %.2f %.2f",
			mode, start, end, density, color.x, color.y, color.z);
}

void SdfProbes::SetFogGain(float gain) {
	if (gain == fogGain_) return;
	fogGain_ = gain;
	if (fog_[0] > 0.5f)
		for (int k = 0; k < SdfLighting::kCascades; ++k) pending_[k] = pendingFull_[k] = true;
}

void SdfProbes::BindSurfaces(uint8_t first) const {
	if (!ok_) return;
	SetFieldUniforms();
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		const Volume& v = volumes_[k];
		const bool on = v.voxel > 0.f;
		bgfx::setTexture(uint8_t(first + k), sField_[k], on ? v.field : empty3D_);
		bgfx::setTexture(uint8_t(first + 4 + k), sSurface_[k], on ? v.seed : empty3D_, kPoint);
		bgfx::setTexture(uint8_t(first + 7 + k), sBins_[k], on ? v.list : empty2D_, kPoint);
	}
	const Volume& level = volumes_[kLevel];
	const bool levelOn = level.voxel > 0.f;
	bgfx::setTexture(uint8_t(first + 3), sLevel_, levelOn ? level.field : empty3D_);
	bgfx::setTexture(uint8_t(first + 10), sSky_, bgfx::isValid(sky_) ? sky_ : empty2D_);
	bgfx::setTexture(uint8_t(first + 11), sLevelSurface_, levelOn ? level.seed : empty3D_, kPoint);
	bgfx::setTexture(uint8_t(first + 12), sLevelBins_, levelOn ? level.list : empty2D_, kPoint);
	const float bins[4] = {float(SdfLighting::kListWidth), 0.f, 0.f, 0.f};
	const float sky[4] = {bgfx::isValid(sky_) ? 1.f : 0.f, 0.f, 0.f, 0.f};
	const float fogScale = fogGain_ * skyScale_;
	const float fogColor[4] = {fogColor_.x * fogScale, fogColor_.y * fogScale, fogColor_.z * fogScale, 0.f};
	bgfx::setUniform(uBins_, bins);
	bgfx::setUniform(uSky_, sky);
	bgfx::setUniform(uFog_, fog_);
	bgfx::setUniform(uFogColor_, fogColor);
}

void SdfProbes::Update(SdfLighting& sdf, bgfx::ViewId computeView) {
	if (!ok_) return;
	using Clock = std::chrono::steady_clock;
	const int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
			Clock::now().time_since_epoch()).count();
	const double frameMs = lastUpdateUs_ ? double(nowUs - lastUpdateUs_) / 1000.0 : 0.0;
	lastUpdateUs_ = nowUs;
	if (frameMs > 0.0) {
		if (frameMsTypical_ > 0.0 && frameMs > frameMsTypical_ * 2.0 && frameMs > 8.0)
			LogInfo("distance field: frame of %.1f ms against %.1f typical; it submitted: %s; worker %s", frameMs,
					frameMsTypical_, frameNote_.empty() ? "nothing" : frameNote_.c_str(),
					sdf.building() ? "busy" : "idle");
		frameMsTypical_ = frameMsTypical_ > 0.0
				? frameMsTypical_ * 0.95 + std::min(frameMs, frameMsTypical_ * 3.0) * 0.05 : frameMs;
	}
	frameNote_.clear();
	skyScale_ = sdf.skyScale();

	// The field the last frame finished goes live before this frame draws.
	if (build_.volume >= 0 && build_.step >= kBuildSteps) PublishBuild();

	if (sdf.skyGeneration() != skyGeneration_) {
		skyGeneration_ = sdf.skyGeneration();
		Destroy(sky_);
		const std::vector<float>& cones = sdf.skyCones();
		if (!cones.empty()) {
			std::vector<float> rgba(cones.size() / 3 * 4, 1.f);
			for (size_t i = 0; i * 3 < cones.size(); ++i)
				for (int c = 0; c < 3; ++c) rgba[i * 4 + c] = cones[i * 3 + c];
			sky_ = bgfx::createTexture2D(uint16_t(sdf.skyWidth()), uint16_t(sdf.skyHeight()), false, 1,
					bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_V_CLAMP, Take(rgba));
		}
		for (int k = 0; k < SdfLighting::kCascades; ++k) pending_[k] = pendingFull_[k] = true;
	}

	if (build_.volume < 0) {
		// The volume that has waited longest, its id the oldest: a cascade that
		// keeps moving cannot starve the others.
		int next = -1;
		SdfLighting::Volume* nextCpu = nullptr;
		for (int v = 0; v <= kLevel; ++v) {
			SdfLighting::Volume* cpu = v == kLevel ? sdf.level() : sdf.cascade(v);
			if (!cpu || cpu->id == volumes_[v].id) continue;
			if (!nextCpu || cpu->id < nextCpu->id) {
				next = v;
				nextCpu = cpu;
			}
		}
		if (nextCpu) StartBuild(next, *nextCpu);
	}
	// A trace dilated last frame becomes its grid's target, and every grid eases
	// toward its target before the probe step can overwrite one. The probe step
	// then reads the fields as the frame drew them, before a build step changes one.
	if (job_.stage == Stage::kPublish) PublishJob();
	EaseGrids(computeView, float(frameMs / 1000.0));
	StepJob(computeView);
	if (build_.volume >= 0) StepBuild(computeView);
}

void SdfProbes::StartBuild(int v, SdfLighting::Volume& cpu) {
	build_ = Build();
	build_.volume = v;
	build_.id = cpu.id;
	build_.origin = cpu.origin;
	build_.voxel = cpu.voxel;
	for (int a = 0; a < 3; ++a) build_.dims[a] = cpu.dims[a];
	build_.surfaces = cpu.surfaces;
	build_.kept = cpu.kept;
	build_.incremental = cpu.incremental;
	build_.list = bgfx::createTexture2D(uint16_t(SdfLighting::kListWidth), uint16_t(cpu.listHeight), false, 1,
			bgfx::TextureFormat::RGBA8, kPoint, Take(cpu.list));

	const Volume& vol = volumes_[v];
	const uint16_t nx = uint16_t(cpu.dims[0]), ny = uint16_t(cpu.dims[1]), nz = uint16_t(cpu.dims[2]);
	const bool sameSize = bgfx::isValid(vol.field) && vol.dims[0] == nx && vol.dims[1] == ny && vol.dims[2] == nz;
	// Written in place when the size holds: the old field is read until the
	// frame the new one is finished in, and published after it.
	build_.field = sameSize ? vol.field : bgfx::createTexture3D(nx, ny, nz, false, bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_UVW_CLAMP);
	build_.seed = sameSize && bgfx::isValid(vol.seed) ? vol.seed : bgfx::createTexture3D(nx, ny, nz, false,
			bgfx::TextureFormat::R32F, BGFX_TEXTURE_COMPUTE_WRITE | kPoint);
	bgfx::TextureHandle* work = v == kLevel ? levelWork_ : work_;
	if (v == kLevel) for (bgfx::TextureHandle& t : levelWork_) Destroy(t);
	for (int i = 0; i < 2; ++i)
		if (!bgfx::isValid(work[i]))
			work[i] = bgfx::createTexture3D(nx, ny, nz, false, bgfx::TextureFormat::R32F,
					BGFX_TEXTURE_COMPUTE_WRITE | kPoint);
}

void SdfProbes::DispatchClear(bgfx::ViewId view, bgfx::TextureHandle target, const int dims[3]) {
	const float d[4] = {float(dims[0]), float(dims[1]), float(dims[2]), 0.f};
	bgfx::setUniform(uEdt_, d);
	bgfx::setImage(0, target, 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);
	bgfx::dispatch(view, clear_, Groups(dims[0], 8), Groups(dims[1], 8), Groups(dims[2], 8));
}

void SdfProbes::DispatchSeed(bgfx::ViewId view, bgfx::TextureHandle target, bool indices) {
	if (build_.surfaces == 0) return;
	const float seed[4] = {float(build_.surfaces), indices ? 1.f : 0.f, float(SdfLighting::kListWidth), 0.f};
	bgfx::setUniform(uSeed_, seed);
	bgfx::setImage(0, target, 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);
	bgfx::setTexture(1, sList_, build_.list, kPoint);
	bgfx::dispatch(view, seed_, Groups(int(build_.surfaces), 64), 1, 1);
}

void SdfProbes::StepBuild(bgfx::ViewId view) {
	Build& b = build_;
	++b.frames;
	bgfx::TextureHandle* work = b.volume == kLevel ? levelWork_ : work_;
	const float dims[4] = {float(b.dims[0]), float(b.dims[1]), float(b.dims[2]), 0.f};
	char note[48];
	std::snprintf(note, sizeof(note), "field %d step %d ", b.volume, b.step);
	frameNote_ += note;
	switch (b.step) {
	case 0:
		// Every surface voxel as its own feature.
		DispatchClear(view, work[0], b.dims);
		DispatchSeed(view, work[0], false);
		break;
	case 1:
	case 2:
	case 3: {
		// One axis a frame, x then y then z, back and forth between the two.
		const int axis = b.step - 1;
		const bgfx::TextureHandle in = work[axis == 1 ? 1 : 0], out = work[axis == 1 ? 0 : 1];
		const float edt[4] = {dims[0], dims[1], dims[2], float(axis)};
		bgfx::setUniform(uEdt_, edt);
		bgfx::setImage(0, out, 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);
		bgfx::setTexture(1, sEdtIn_, in, kPoint);
		const int across = axis == 0 ? 1 : 0, deep = axis == 2 ? 1 : 2;
		bgfx::dispatch(view, edt_, Groups(b.dims[across], 8), Groups(b.dims[deep], 8), 1);
		break;
	}
	case 4:
		// The field from the features, and the surfaces by index for the light.
		bgfx::setUniform(uEdt_, dims);
		bgfx::setImage(0, b.field, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
		bgfx::setTexture(1, sFeatures_, work[1], kPoint);
		bgfx::dispatch(view, field_, Groups(b.dims[0], 8), Groups(b.dims[1], 8), Groups(b.dims[2], 8));
		DispatchClear(view, b.seed, b.dims);
		DispatchSeed(view, b.seed, true);
		break;
	}
	++b.step;
}

void SdfProbes::PublishBuild() {
	Build& b = build_;
	Volume& vol = volumes_[b.volume];
	if (!Same(vol.field, b.field)) Destroy(vol.field);
	if (!Same(vol.seed, b.seed)) Destroy(vol.seed);
	Destroy(vol.list);
	vol.field = b.field;
	vol.seed = b.seed;
	vol.list = b.list;
	vol.origin = b.origin;
	vol.voxel = b.voxel;
	for (int a = 0; a < 3; ++a) vol.dims[a] = b.dims[a];
	vol.id = b.id;
	if (b.volume == kLevel) {
		for (bgfx::TextureHandle& t : levelWork_) Destroy(t);
		for (int k = 0; k < SdfLighting::kCascades; ++k) pending_[k] = pendingFull_[k] = true;
	} else {
		// Only its own grid: a finer grid's rays that leave through it barely change.
		pending_[b.volume] = true;
		if (!b.incremental) pendingFull_[b.volume] = true;
	}
	static const bool kChurn = DebugFlag("PAINFUL_SDF_CHURN");
	static unsigned said[kLevel + 1] = {};
	if (++said[b.volume] <= 2 || said[b.volume] % 20 == 0 || kChurn)
		LogInfo("distance field: %s %d field built on the GPU in %d frames from %zu surface voxels (%zu kept)",
				b.volume == kLevel ? "level" : "cascade", b.volume, b.frames, b.surfaces, b.kept);
	build_ = Build();
}

void SdfProbes::StartJob(int k) {
	const Volume& vol = volumes_[k];
	const Grid& grid = grids_[k];
	job_ = Job();
	job_.cascade = k;
	job_.spacing = vol.voxel * 4.f;
	job_.corner = vol.origin + Vec3(job_.spacing * 0.5f);
	bool keep = !pendingFull_[k] && grid.targetSpacing == job_.spacing;
	if (keep) {
		const Vec3 moved = (job_.corner - grid.targetCorner) / job_.spacing;
		job_.offset[0] = int(std::lround(moved.x));
		job_.offset[1] = int(std::lround(moved.y));
		job_.offset[2] = int(std::lround(moved.z));
		for (int a = 0; a < 3; ++a)
			if (std::abs(job_.offset[a]) >= kProbes) keep = false;
	}
	pending_[k] = pendingFull_[k] = false;
	if (!keep) {
		job_.boxes.push_back(Box{{0, 0, 0}, {kProbes, kProbes, kProbes}});
		job_.stage = Stage::kTrace;
		return;
	}
	// The slab each axis moved brought in; where two overlap it is traced twice.
	for (int a = 0; a < 3; ++a) {
		const int o = job_.offset[a];
		if (o == 0) continue;
		Box b{{0, 0, 0}, {kProbes, kProbes, kProbes}};
		b.min[a] = o > 0 ? kProbes - o : 0;
		b.size[a] = std::abs(o);
		job_.boxes.push_back(b);
	}
	job_.stage = Stage::kCopy;
}

void SdfProbes::StepJob(bgfx::ViewId view) {
	const double gpuMs = GpuFrameMs();
	if (job_.stage == Stage::kIdle) {
		idleGpuMs_ = idleGpuMs_ > 0.0 ? idleGpuMs_ * 0.95 + gpuMs * 0.05 : gpuMs;
		// The grids in turn, so a cascade that keeps moving cannot starve the others.
		for (int i = 1; i <= SdfLighting::kCascades; ++i) {
			const int k = (lastGrid_ + i) % SdfLighting::kCascades;
			if (!pending_[k] || volumes_[k].voxel <= 0.f) continue;
			lastGrid_ = k;
			StartJob(k);
			break;
		}
	} else {
		// A job always finishes: its probes are world-space, still right if the
		// field moved meanwhile, and the move queued the grid again.
		++job_.frames;
		job_.gpuMs += gpuMs;
	}
	Grid& grid = grids_[job_.cascade];
	switch (job_.stage) {
	case Stage::kIdle:
		return;
	case Stage::kCopy: {
		const float offset[4] = {float(job_.offset[0]), float(job_.offset[1]), float(job_.offset[2]), 0.f};
		bgfx::setUniform(uCopy_, offset);
		bgfx::setImage(0, grid.traced, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
		bgfx::setTexture(1, sOld_, grid.target, kPoint);
		bgfx::dispatch(view, copy_, Groups(kProbes, 8), Groups(kProbes, 8), Groups(kProbes, 8));
		job_.stage = job_.boxes.empty() ? Stage::kDilate : Stage::kTrace;
		frameNote_ += "copy ";
		return;
	}
	case Stage::kTrace: {
		const Box& b = job_.boxes[job_.box];
		const int area = b.size[0] * b.size[1];
		const int layers = std::clamp(kProbesPerFrame / area, 1, b.size[2] - job_.layer);
		BindSurfaces(1);
		const float at[4] = {job_.corner.x, job_.corner.y, job_.corner.z, job_.spacing};
		const float min[4] = {float(b.min[0]), float(b.min[1]), float(b.min[2] + job_.layer), 0.f};
		const float size[4] = {float(b.size[0]), float(b.size[1]), float(layers), 0.f};
		bgfx::setUniform(uJob_, at);
		bgfx::setUniform(uJobMin_, min);
		bgfx::setUniform(uJobSize_, size);
		bgfx::setImage(0, grid.traced, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
		bgfx::dispatch(view, trace_, Groups(b.size[0], 8), Groups(b.size[1], 8), uint32_t(layers));
		job_.traced += area * layers;
		job_.layer += layers;
		if (job_.layer >= b.size[2]) {
			job_.layer = 0;
			if (++job_.box >= job_.boxes.size()) job_.stage = Stage::kDilate;
		}
		frameNote_ += "trace ";
		return;
	}
	case Stage::kDilate:
		frameNote_ += "dilate ";
		bgfx::setTexture(0, sTraced_, grid.traced, kPoint);
		bgfx::setImage(1, grid.target, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
		bgfx::dispatch(view, dilate_, Groups(kProbes, 8), Groups(kProbes, 8), Groups(kProbes, 8));
		job_.stage = Stage::kPublish;
		return;
	case Stage::kPublish:
		return; // PublishJob, next frame
	}
}

void SdfProbes::PublishJob() {
	// The dilation ran at the end of the last frame: the target holds the new trace.
	Grid& grid = grids_[job_.cascade];
	grid.targetCorner = job_.corner;
	grid.targetSpacing = job_.spacing;
	grid.sinceChange = 0.f;
	grid.settled = false;
	static const bool kChurn = DebugFlag("PAINFUL_SDF_CHURN");
	static unsigned said[SdfLighting::kCascades] = {};
	if (++said[job_.cascade] <= 2 || said[job_.cascade] % 20 == 0 || kChurn)
		LogInfo("distance field: probe grid %d at %.0f %.0f %.0f published after %d frames, %d probes traced, "
				"GPU frame %.1f ms while working, %.1f ms idle", job_.cascade, job_.corner.x, job_.corner.y,
				job_.corner.z, job_.frames, job_.traced, job_.gpuMs / double(std::max(job_.frames, 1)), idleGpuMs_);
	job_.stage = Stage::kIdle;
}

// What the models sample eases toward each grid's target, a step a frame on a
// kEaseSeconds time constant and exact after eight of them; a settled grid
// costs nothing.
void SdfProbes::EaseGrids(bgfx::ViewId view, float seconds) {
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		Grid& g = grids_[k];
		if (g.targetSpacing <= 0.f || g.settled) continue;
		g.sinceChange += seconds;
		const bool last = g.sinceChange >= kEaseSeconds * 8.f;
		const float step = last ? 1.f : 1.f - std::exp(-seconds / kEaseSeconds);
		// The sampled grid scrolls by whole probes when the target has moved.
		const bool hasOld = g.spacing == g.targetSpacing;
		float ease[4] = {0.f, 0.f, 0.f, step};
		if (hasOld) {
			const Vec3 moved = (g.targetCorner - g.corner) / g.spacing;
			ease[0] = float(std::lround(moved.x));
			ease[1] = float(std::lround(moved.y));
			ease[2] = float(std::lround(moved.z));
		}
		const float old[4] = {0.f, 0.f, 0.f, hasOld ? 1.f : 0.f};
		// A cell it did not cover starts from the next grid out, as sampled now.
		float coarser[4] = {0.f, 0.f, 0.f, 0.f};
		bgfx::TextureHandle coarserTexture = empty3D_;
		if (k + 1 < SdfLighting::kCascades && grids_[k + 1].spacing > 0.f) {
			const Grid& c = grids_[k + 1];
			const Vec3 at = (g.targetCorner - c.corner) / c.spacing + Vec3(0.5f);
			coarser[0] = at.x;
			coarser[1] = at.y;
			coarser[2] = at.z;
			coarser[3] = g.targetSpacing / c.spacing;
			coarserTexture = c.shown[c.current];
		}
		bgfx::setUniform(uEase_, ease);
		bgfx::setUniform(uEaseOld_, old);
		bgfx::setUniform(uCoarser_, coarser);
		bgfx::setImage(0, g.shown[1 - g.current], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
		bgfx::setTexture(1, sShownOld_, hasOld ? g.shown[g.current] : empty3D_, kPoint);
		bgfx::setTexture(2, sTarget_, g.target, kPoint);
		bgfx::setTexture(3, sCoarser_, coarserTexture);
		bgfx::dispatch(view, ease_, Groups(kProbes, 8), Groups(kProbes, 8), Groups(kProbes, 8));
		g.current = 1 - g.current;
		g.corner = g.targetCorner;
		g.spacing = g.targetSpacing;
		if (last) g.settled = true;
		frameNote_ += "ease ";
	}
}

} // namespace painful
