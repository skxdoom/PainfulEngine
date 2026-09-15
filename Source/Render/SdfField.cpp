#include "SdfField.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cstring>

namespace painful {

namespace {

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

} // namespace

bool SdfField::Init() {
	const bgfx::Caps* caps = bgfx::getCaps();
	const bool compute = (caps->supported & BGFX_CAPS_COMPUTE) != 0;
	const bool volumes = (caps->supported & BGFX_CAPS_TEXTURE_3D) != 0 &&
			(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_3D) != 0 &&
			(caps->formats[bgfx::TextureFormat::RG8] & BGFX_CAPS_FORMAT_TEXTURE_3D) != 0;
	if (!compute || !volumes) {
		LogWarn("distance field: needs compute (%s) and R32F and RG8 volumes (%s); RendererType 1 stays off",
				compute ? "yes" : "no", volumes ? "yes" : "no");
		return false;
	}
	sMap_ = bgfx::createUniform("s_sdfMap", bgfx::UniformType::Sampler);
	sBricks_ = bgfx::createUniform("s_sdfBricks", bgfx::UniformType::Sampler);
	sList_ = bgfx::createUniform("s_sdfList", bgfx::UniformType::Sampler);
	sSky_ = bgfx::createUniform("s_sdfSky", bgfx::UniformType::Sampler);
	uVolume_ = bgfx::createUniform("u_sdfVolume", bgfx::UniformType::Vec4);
	uExtent_ = bgfx::createUniform("u_sdfExtent", bgfx::UniformType::Vec4);
	uCells_ = bgfx::createUniform("u_sdfCells", bgfx::UniformType::Vec4);
	uAtlas_ = bgfx::createUniform("u_sdfAtlas", bgfx::UniformType::Vec4);
	uSky_ = bgfx::createUniform("u_sdfSky", bgfx::UniformType::Vec4);
	uFog_ = bgfx::createUniform("u_sdfFog", bgfx::UniformType::Vec4);
	uFogColor_ = bgfx::createUniform("u_sdfFogColor", bgfx::UniformType::Vec4);
	const float none[4] = {-1.f, 0.f, 0.f, 0.f};
	empty3D_ = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::R32F, kPoint, bgfx::copy(none, 4));
	empty2D_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F, kPoint,
			bgfx::copy(none, sizeof(none)));
	ok_ = bgfx::isValid(empty3D_) && bgfx::isValid(empty2D_);
	return ok_;
}

void SdfField::Shutdown() {
	Clear();
	for (bgfx::UniformHandle* u : {&sMap_, &sBricks_, &sList_, &sSky_, &uVolume_, &uExtent_, &uCells_, &uAtlas_,
			&uSky_, &uFog_, &uFogColor_})
		Destroy(*u);
	Destroy(empty3D_);
	Destroy(empty2D_);
	ok_ = false;
}

void SdfField::Release(Volume& v) {
	Destroy(v.map);
	Destroy(v.bricks);
	Destroy(v.list);
	v = Volume();
}

void SdfField::Clear() {
	Release(shown_);
	Release(loading_);
	uploads_.clear();
	Destroy(sky_);
	++lightGeneration_;
	// skyGeneration_ stays: SdfLighting's keeps counting across levels.
}

void SdfField::SetFog(int mode, float start, float end, float density, const Vec3& color255) {
	const float fog[4] = {float(mode), start, end, density};
	const Vec3 color = color255 / 255.f;
	if (std::equal(fog, fog + 4, fog_) && color.x == fogColor_.x && color.y == fogColor_.y &&
			color.z == fogColor_.z)
		return;
	std::copy(fog, fog + 4, fog_);
	fogColor_ = color;
	++lightGeneration_;
	LogInfo("distance field: rays gather fog mode %d from %.1f to %.1f, density %.3f, colour %.2f %.2f %.2f",
			mode, start, end, density, color.x, color.y, color.z);
}

void SdfField::SetFogGain(float gain) {
	if (gain == fogGain_) return;
	fogGain_ = gain;
	++lightGeneration_;
}

void SdfField::Update(SdfLighting& sdf) {
	if (!ok_) return;
	if (sdf.skyScale() != skyScale_) {
		skyScale_ = sdf.skyScale();
		++lightGeneration_;
	}
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
		++lightGeneration_;
	}

	// A finished volume starts loading; one still loading gives way to it.
	if (SdfLighting::Volume* cpu = sdf.volume(); cpu && cpu->id != shown_.id && cpu->id != loading_.id) {
		Release(loading_);
		uploads_.clear();
		loading_.id = cpu->id;
		const int most = int(bgfx::getCaps()->limits.maxTextureSize);
		if (cpu->listHeight > most) {
			LogWarn("distance field: the surface list needs %d rows, more than the %d a texture takes; not loaded",
					cpu->listHeight, most);
			return;
		}
		loading_.origin = cpu->origin;
		loading_.voxel = cpu->voxel;
		for (int a = 0; a < 3; ++a) {
			loading_.dims[a] = cpu->dims[a];
			loading_.cells[a] = cpu->cells[a];
			loading_.atlas[a] = cpu->atlasBricks[a];
		}
		loading_.stored = int(cpu->bricks);
		const int k = SdfLighting::kBrick;
		loading_.map = bgfx::createTexture3D(uint16_t(cpu->cells[0]), uint16_t(cpu->cells[1]), uint16_t(cpu->cells[2]),
				false, bgfx::TextureFormat::R32F, kPoint);
		loading_.bricks = bgfx::createTexture3D(uint16_t(cpu->atlasBricks[0] * k), uint16_t(cpu->atlasBricks[1] * k),
				uint16_t(cpu->atlasBricks[2] * k), false, bgfx::TextureFormat::RG8, kPoint);
		loading_.list = bgfx::createTexture2D(uint16_t(SdfLighting::kListWidth), uint16_t(cpu->listHeight), false, 1,
				bgfx::TextureFormat::RGBA8, kPoint);
		Upload map;
		map.texture = loading_.map;
		map.volume = true;
		map.width = cpu->cells[0];
		map.height = cpu->cells[1];
		map.depth = cpu->cells[2];
		map.texelBytes = sizeof(float);
		map.data.resize(cpu->map.size() * sizeof(float));
		std::memcpy(map.data.data(), cpu->map.data(), map.data.size());
		cpu->map = std::vector<float>();
		Upload bricks;
		bricks.texture = loading_.bricks;
		bricks.volume = true;
		bricks.width = cpu->atlasBricks[0] * k;
		bricks.height = cpu->atlasBricks[1] * k;
		bricks.depth = cpu->atlasBricks[2] * k;
		bricks.texelBytes = 2;
		bricks.data = std::move(cpu->atlas);
		Upload list;
		list.texture = loading_.list;
		list.width = SdfLighting::kListWidth;
		list.height = cpu->listHeight;
		list.texelBytes = 4;
		list.data = std::move(cpu->list);
		uploads_.push_back(std::move(map));
		uploads_.push_back(std::move(bricks));
		uploads_.push_back(std::move(list));
	}
	if (!bgfx::isValid(loading_.map)) return;
	size_t budget = kSlabBytes;
	bool sent = false;
	for (Upload& u : uploads_) {
		const size_t slice = size_t(u.width) * size_t(u.volume ? u.height : 1) * u.texelBytes;
		const int slices = u.volume ? u.depth : u.height;
		while (u.next < slices && budget > 0) {
			const int count = std::min(slices - u.next, std::max(1, int(budget / slice)));
			const bgfx::Memory* mem = bgfx::copy(u.data.data() + size_t(u.next) * slice, uint32_t(slice * size_t(count)));
			if (u.volume)
				bgfx::updateTexture3D(u.texture, 0, 0, 0, uint16_t(u.next), uint16_t(u.width), uint16_t(u.height),
						uint16_t(count), mem);
			else
				bgfx::updateTexture2D(u.texture, 0, 0, 0, uint16_t(u.next), uint16_t(u.width), uint16_t(count), mem);
			u.next += count;
			budget -= std::min(budget, slice * size_t(count));
			sent = true;
		}
		if (u.next < slices) return;
	}
	if (sent) return;
	// Every slab went up in an earlier frame: this frame's traces read it whole.
	size_t bytes = 0;
	for (const Upload& u : uploads_) bytes += u.data.size();
	LogInfo("distance field: %dx%dx%d voxels in %d stored bricks published after uploading %.1f MB in slabs",
			loading_.dims[0], loading_.dims[1], loading_.dims[2], loading_.stored, double(bytes) / 1048576.0);
	Release(shown_);
	shown_ = loading_;
	loading_ = Volume();
	uploads_.clear();
	++lightGeneration_;
}

void SdfField::BindSurfaces(uint8_t first) const {
	if (!ok_) return;
	const bool on = ready();
	const float volume[4] = {shown_.origin.x, shown_.origin.y, shown_.origin.z, on ? shown_.voxel : 0.f};
	const float extent[4] = {float(shown_.dims[0]), float(shown_.dims[1]), float(shown_.dims[2]),
			float(SdfLighting::kListWidth)};
	const float cells[4] = {float(shown_.cells[0]), float(shown_.cells[1]), float(shown_.cells[2]),
			float(shown_.atlas[0])};
	const float atlas[4] = {float(shown_.atlas[1]), float(shown_.stored), 0.f, 0.f};
	const float sky[4] = {bgfx::isValid(sky_) ? 1.f : 0.f, 0.f, 0.f, 0.f};
	const float fogScale = fogGain_ * skyScale_;
	const float fogColor[4] = {fogColor_.x * fogScale, fogColor_.y * fogScale, fogColor_.z * fogScale, 0.f};
	bgfx::setUniform(uVolume_, volume);
	bgfx::setUniform(uExtent_, extent);
	bgfx::setUniform(uCells_, cells);
	bgfx::setUniform(uAtlas_, atlas);
	bgfx::setUniform(uSky_, sky);
	bgfx::setUniform(uFog_, fog_);
	bgfx::setUniform(uFogColor_, fogColor);
	bgfx::setTexture(first, sMap_, on ? shown_.map : empty3D_, kPoint);
	bgfx::setTexture(uint8_t(first + 1), sBricks_, on ? shown_.bricks : empty3D_, kPoint);
	bgfx::setTexture(uint8_t(first + 2), sList_, on ? shown_.list : empty2D_, kPoint);
	bgfx::setTexture(uint8_t(first + 3), sSky_, bgfx::isValid(sky_) ? sky_ : empty2D_);
}

} // namespace painful
