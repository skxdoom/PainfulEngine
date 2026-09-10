#include "ViewModelShadows.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <bx/math.h>
#include <cmath>

namespace painful {

bool ViewModelShadows::Init(const std::string& shaderDir, int size) {
	if (size <= 0) return false;
	const bgfx::Caps* caps = bgfx::getCaps();
	if (!(caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL)) return false;
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_shadow");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_shadow");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) return false;
	program_ = bgfx::createProgram(vs, fs, true);

	const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
			BGFX_SAMPLER_UVW_CLAMP;
	const bgfx::TextureFormat::Enum formats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	for (size_t i = 0; i < 3 && !bgfx::isValid(depth_); ++i) {
		if (!bgfx::isTextureValid(0, false, 1, formats[i], flags)) continue;
		depth_ = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1, formats[i], flags);
	}
	if (!bgfx::isValid(depth_)) {
		Shutdown();
		return false;
	}
	fb_ = bgfx::createFrameBuffer(1, &depth_, true);
	size_ = size;
	texel_ = 1.f / float(size);
	active_ = false;
	LogInfo("view model shadows: %dx%d", size, size);
	return true;
}

void ViewModelShadows::Shutdown() {
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	fb_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
	size_ = 0;
	active_ = false;
}

void ViewModelShadows::Begin(const Vec3& toLightIn, const Vec3& centre, float radius) {
	if (!ready() || radius <= 0.f) return;
	Vec3 toLight = toLightIn;
	if (toLight.LengthSq() < 1e-12f) toLight = Vec3{0.f, 1.f, 0.f};
	toLight /= toLight.Length();
	// Just the sphere, with a margin: the weapon is the only caster.
	const float extent = radius * 1.1f;
	const float back = radius * 2.f;
	const Vec3 up = std::abs(toLight[1]) > 0.9f ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
	const bx::Vec3 eye = {centre[0] + toLight[0] * back, centre[1] + toLight[1] * back,
			centre[2] + toLight[2] * back};
	const bx::Vec3 at = {centre[0], centre[1], centre[2]};
	const bgfx::Caps* caps = bgfx::getCaps();
	float view[16], proj[16];
	bx::mtxLookAt(view, eye, at, {up[0], up[1], up[2]}, bx::Handedness::Right);
	bx::mtxOrtho(proj, -extent, extent, -extent, extent, 0.f, back + radius * 1.5f, 0.f,
			caps->homogeneousDepth, bx::Handedness::Right);
	frustum_ = Frustum::FromViewProj(view, proj);

	const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
	const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
	const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
	const float crop[16] = {
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, sy, 0.0f, 0.0f,
		0.0f, 0.0f, sz, 0.0f,
		0.5f, 0.5f, tz, 1.0f,
	};
	float projCrop[16];
	bx::mtxMul(projCrop, proj, crop);
	bx::mtxMul(matrix_, view, projCrop);
	light_[0] = light_[1] = light_[2] = 0.f;
	light_[3] = 2.f * extent / float(size_);

	bgfx::setViewFrameBuffer(view_, fb_);
	bgfx::setViewRect(view_, 0, 0, uint16_t(size_), uint16_t(size_));
	bgfx::setViewClear(view_, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
	bgfx::setViewTransform(view_, view, proj);
	bgfx::touch(view_);
	active_ = true;
}

} // namespace painful
