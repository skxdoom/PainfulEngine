#include "ShadowMap.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <bx/math.h>
#include <cmath>

namespace painful {

bool ShadowMap::Init(const std::string& shaderDir, int size) {
	if (size <= 0) return false;
	const bgfx::Caps* caps = bgfx::getCaps();
	if (!(caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL)) {
		LogWarn("shadow map: %s has no hardware depth compare, flashlight shadows off",
				bgfx::getRendererName(bgfx::getRendererType()));
		return false;
	}
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_shadow");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_shadow");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) return false;
	program_ = bgfx::createProgram(vs, fs, true);

	// The compare flag lives on the texture: setTexture with default flags
	// then samples it through a comparison sampler, which is what
	// SAMPLER2DSHADOW in the receivers expects.
	const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
			BGFX_SAMPLER_UVW_CLAMP;
	const bgfx::TextureFormat::Enum formats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	const char* names[] = {"D24S8", "D32F", "D16"};
	const char* chosen = nullptr;
	for (size_t i = 0; i < 3 && !bgfx::isValid(depth_); ++i) {
		if (!bgfx::isTextureValid(0, false, 1, formats[i], flags)) continue;
		depth_ = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1,
				formats[i], flags);
		chosen = names[i];
	}
	if (!bgfx::isValid(depth_)) {
		LogWarn("shadow map: no sampleable depth format, flashlight shadows off");
		Shutdown();
		return false;
	}
	fb_ = bgfx::createFrameBuffer(1, &depth_, true);
	size_ = size;
	LogInfo("shadow map: %dx%d %s", size, size, chosen);
	return true;
}

void ShadowMap::Shutdown() {
	// The framebuffer owns the depth texture (createFrameBuffer's destroy
	// flag), so the texture handle is only dropped.
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	fb_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
	active_ = false;
	size_ = 0;
}

void ShadowMap::Begin(bgfx::ViewId view, const LightSource& light) {
	active_ = ready();
	if (!active_) return;

	Vec3 dir = light.dir;
	if (dir.LengthSq() < 1e-12f) dir = Vec3{0.f, 0.f, -1.f};
	dir /= dir.Length();
	// The same up rule PackLight uses for the cookie basis: the roll is
	// arbitrary, it only has to be stable and not parallel to the axis.
	const Vec3 up = std::abs(dir[1]) > 0.9f ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
	const bx::Vec3 eye = {light.pos[0], light.pos[1], light.pos[2]};
	const bx::Vec3 at = {light.pos[0] + dir[0], light.pos[1] + dir[1], light.pos[2] + dir[2]};
	bx::mtxLookAt(view_, eye, at, {up[0], up[1], up[2]}, bx::Handedness::Right);

	// Light::UpdateProj (0x101d4d90): half-fov acos(coneAngleCos), aspect 1,
	// near 0.1, far Range. The cosine is floored where LightReach floors it.
	constexpr float kNear = 0.1f;
	const float cosOuter = std::min(std::max(light.coneOuterCos, 0.2f), 0.999f);
	const float fovDegrees = 2.f * std::acos(cosOuter) * 180.f / 3.14159265f;
	const float farPlane = std::max(light.range, kNear * 2.f);
	const bgfx::Caps* caps = bgfx::getCaps();
	bx::mtxProj(proj_, fovDegrees, 1.f, kNear, farPlane, caps->homogeneousDepth,
			bx::Handedness::Right);
	frustum_ = Frustum::FromViewProj(view_, proj_);

	// Clip space -> texture space, per backend: y down unless the origin is
	// bottom-left, z from [-1,1] when the depth range is homogeneous.
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
	bx::mtxMul(projCrop, proj_, crop);
	bx::mtxMul(matrix_, view_, projCrop);

	bgfx::setViewFrameBuffer(view, fb_);
	bgfx::setViewRect(view, 0, 0, uint16_t(size_), uint16_t(size_));
	bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
	bgfx::setViewTransform(view, view_, proj_);
	// Cleared even if nothing casts, so the receivers never read last frame.
	bgfx::touch(view);
}

} // namespace painful
