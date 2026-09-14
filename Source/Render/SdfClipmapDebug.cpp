#include "SdfClipmapDebug.h"
#include "FullScreenPass.h"
#include "ShaderLoad.h"

#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace painful {

namespace {
constexpr int kBinsWidth = 1024;
constexpr float kDistanceMax = 63.75f; // voxels an R8 texel can say, in quarters
} // namespace

bool SdfClipmapDebug::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_sdfclipmap");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		if (bgfx::isValid(vs)) bgfx::destroy(vs);
		if (bgfx::isValid(fs)) bgfx::destroy(fs);
		return false;
	}
	program_ = bgfx::createProgram(vs, fs, true);
	layout_ = PostVertexLayout();
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		const std::string n = std::to_string(k);
		sDistance_[k] = bgfx::createUniform(("s_sdfDistance" + n).c_str(), bgfx::UniformType::Sampler);
		sSurface_[k] = bgfx::createUniform(("s_sdfSurface" + n).c_str(), bgfx::UniformType::Sampler);
		sBins_[k] = bgfx::createUniform(("s_sdfBins" + n).c_str(), bgfx::UniformType::Sampler);
	}
	uInvViewProj_ = bgfx::createUniform("u_sdfInvViewProj", bgfx::UniformType::Mat4);
	uEye_ = bgfx::createUniform("u_sdfEye", bgfx::UniformType::Vec4);
	uWindow_ = bgfx::createUniform("u_sdfWindow", bgfx::UniformType::Vec4, SdfLighting::kCascades);
	uGrid_ = bgfx::createUniform("u_sdfGrid", bgfx::UniformType::Vec4);
	uScreen_ = bgfx::createUniform("u_sdfScreen", bgfx::UniformType::Vec4);
	const uint8_t far = 255;
	empty3D_ = bgfx::createTexture3D(1, 1, 1, false, bgfx::TextureFormat::R8,
			BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP, bgfx::copy(&far, 1));
	const float none[4] = {0.f, 0.f, 0.f, 0.f};
	empty2D_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
			BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP, bgfx::copy(none, sizeof(none)));
	return bgfx::isValid(program_);
}

void SdfClipmapDebug::Release(Cascade& c) {
	for (bgfx::TextureHandle* t : {&c.distance, &c.surface, &c.bins}) {
		if (bgfx::isValid(*t)) bgfx::destroy(*t);
		*t = BGFX_INVALID_HANDLE;
	}
	c.id = 0;
	c.voxel = 0.f;
}

void SdfClipmapDebug::Clear() {
	for (Cascade& c : cascades_) Release(c);
}

void SdfClipmapDebug::Shutdown() {
	Clear();
	for (int k = 0; k < SdfLighting::kCascades; ++k)
		for (bgfx::UniformHandle* u : {&sDistance_[k], &sSurface_[k], &sBins_[k]}) {
			if (bgfx::isValid(*u)) bgfx::destroy(*u);
			*u = BGFX_INVALID_HANDLE;
		}
	for (bgfx::UniformHandle* u : {&uInvViewProj_, &uEye_, &uWindow_, &uGrid_, &uScreen_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	for (bgfx::TextureHandle* t : {&empty3D_, &empty2D_}) {
		if (bgfx::isValid(*t)) bgfx::destroy(*t);
		*t = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
}

void SdfClipmapDebug::Upload(Cascade& cascade, const SdfLighting::Volume& vol) {
	Release(cascade);
	const int n = SdfLighting::kWindow;
	const size_t total = size_t(n) * size_t(n) * size_t(n);
	std::vector<uint8_t> distance(total);
	std::vector<float> surface(total);
	for (int z = 0; z < n; ++z)
	for (int y = 0; y < n; ++y)
	for (int x = 0; x < n; ++x) {
		const size_t i = (size_t(z) * size_t(n) + size_t(y)) * size_t(n) + size_t(x);
		const int32_t s = vol.nearest[i];
		surface[i] = float(s);
		float d = kDistanceMax;
		if (s >= 0) {
			const uint32_t c = vol.coords[size_t(s)];
			const float dx = float(int(c & 127) - x), dy = float(int((c >> 7) & 127) - y),
					dz = float(int((c >> 14) & 127) - z);
			d = std::min(std::sqrt(dx * dx + dy * dy + dz * dz), kDistanceMax);
		}
		distance[i] = uint8_t(d * 4.f + 0.5f);
	}
	const uint16_t side = uint16_t(n);
	cascade.distance = bgfx::createTexture3D(side, side, side, false, bgfx::TextureFormat::R8,
			BGFX_SAMPLER_UVW_CLAMP, bgfx::copy(distance.data(), uint32_t(distance.size())));
	cascade.surface = bgfx::createTexture3D(side, side, side, false, bgfx::TextureFormat::R32F,
			BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
			bgfx::copy(surface.data(), uint32_t(surface.size() * sizeof(float))));

	// Six texels a surface, row-major; alpha says whether that facing had light.
	const size_t texels = std::max<size_t>(vol.surfaces.size() * 6, 1);
	const int height = std::min(int((texels + kBinsWidth - 1) / kBinsWidth),
			int(bgfx::getCaps()->limits.maxTextureSize));
	std::vector<float> bins(size_t(kBinsWidth) * size_t(height) * 4, 0.f);
	const size_t fit = std::min(vol.surfaces.size(), bins.size() / 24);
	for (size_t s = 0; s < fit; ++s)
		for (int k = 0; k < 6; ++k) {
			float* px = &bins[(s * 6 + size_t(k)) * 4];
			for (int c = 0; c < 3; ++c) px[c] = vol.surfaces[s].light[k][c];
			px[3] = vol.surfaces[s].weight[k] > 0.f ? 1.f : 0.f;
		}
	cascade.bins = bgfx::createTexture2D(uint16_t(kBinsWidth), uint16_t(height), false, 1,
			bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
			bgfx::copy(bins.data(), uint32_t(bins.size() * sizeof(float))));
	cascade.origin = vol.origin;
	cascade.voxel = vol.voxel;
	cascade.id = vol.id;
}

void SdfClipmapDebug::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
		const SdfLighting& sdf) {
	if (!bgfx::isValid(program_) || width <= 0 || height <= 0) return;
	float window[4 * SdfLighting::kCascades] = {};
	bool any = false;
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		Cascade& c = cascades_[k];
		const SdfLighting::Volume* vol = sdf.cascade(k);
		if (!vol) {
			if (c.id) Release(c);
			continue;
		}
		if (vol->id != c.id) Upload(c, *vol);
		if (!bgfx::isValid(c.distance) || !bgfx::isValid(c.surface) || !bgfx::isValid(c.bins)) continue;
		window[k * 4] = c.origin.x;
		window[k * 4 + 1] = c.origin.y;
		window[k * 4 + 2] = c.origin.z;
		window[k * 4 + 3] = c.voxel; // 0 marks a cascade not built
		any = true;
	}
	if (!any) return;

	float viewMtx[16], projMtx[16], viewProj[16], inverse[16];
	camera.ViewProj(width, height, camera.farPlane, viewMtx, projMtx);
	bx::mtxMul(viewProj, viewMtx, projMtx);
	bx::mtxInverse(inverse, viewProj);
	const float eye[4] = {camera.pos.x, camera.pos.y, camera.pos.z, 0.f};
	const float grid[4] = {float(SdfLighting::kWindow), float(kBinsWidth), 0.f, 0.f};
	const float screen[4] = {float(width), float(height),
			bgfx::getCaps()->originBottomLeft ? 1.f : 0.f, 0.f};

	FullScreenTriangle(view, layout_, width, height);
	bgfx::setUniform(uInvViewProj_, inverse);
	bgfx::setUniform(uEye_, eye);
	bgfx::setUniform(uWindow_, window, uint16_t(SdfLighting::kCascades));
	bgfx::setUniform(uGrid_, grid);
	bgfx::setUniform(uScreen_, screen);
	for (int k = 0; k < SdfLighting::kCascades; ++k) {
		const Cascade& c = cascades_[k];
		const bool built = window[k * 4 + 3] > 0.f;
		const uint8_t stage = uint8_t(k * 3);
		bgfx::setTexture(stage, sDistance_[k], built ? c.distance : empty3D_, BGFX_SAMPLER_UVW_CLAMP);
		bgfx::setTexture(stage + 1, sSurface_[k], built ? c.surface : empty3D_,
				BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
		bgfx::setTexture(stage + 2, sBins_[k], built ? c.bins : empty2D_,
				BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
	}
	bgfx::submit(view, program_);
}

} // namespace painful
