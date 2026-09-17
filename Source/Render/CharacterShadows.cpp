#include "CharacterShadows.h"
#include "ShaderLoad.h"
#include "ShadowMap.h"
#include "../Core/Check.h"
#include "../Core/Log.h"

#include <algorithm>
#include <bx/math.h>
#include <cmath>

namespace painful {

namespace {

// How far the view sits past the caster's bounding sphere, toward the light.
constexpr float kPad = 0.5f;

// A roll for the light's view that is stable and never parallel to it.
Vec3 UpFor(const Vec3& toLight) {
	return std::abs(toLight[1]) > 0.9f ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
}

Vec3 UnitToLight(const Vec3& toLight) {
	return toLight.LengthSq() < 1e-12f ? Vec3{0.f, 1.f, 0.f} : toLight / toLight.Length();
}

// The bounding sphere, quantised so an animation that breathes the bounds does
// not rescale the slot every frame.
float SphereRadius(const Vec3& lo, const Vec3& hi) {
	const float r = ((hi - lo) * 0.5f).Length();
	return std::max(std::ceil(r * 4.f) / 4.f, 0.25f);
}

float FadeLength(const Vec3& lo, const Vec3& hi) {
	return CharacterShadows::kFadeHeights * std::max(hi[1] - lo[1], 0.1f);
}

} // namespace

bool CharacterShadows::Init(const std::string& shaderDir, int slotSize, int slots) {
	if (slotSize <= 0 || slots <= 0) return false;
	const bgfx::Caps* caps = bgfx::getCaps();
	if (!(caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL)) {
		LogWarn("character shadows: %s has no hardware depth compare, off",
				bgfx::getRendererName(bgfx::getRendererType()));
		return false;
	}
	cols_ = int(std::ceil(std::sqrt(float(slots))));
	const int maxCols = std::max(int(caps->limits.maxTextureSize) / slotSize, 1);
	cols_ = std::min(cols_, maxCols);
	rows_ = cols_; // square, so one filter step serves both axes
	slots_ = std::min(slots, cols_ * rows_);
	slotSize_ = slotSize;
	const int width = cols_ * slotSize, height = rows_ * slotSize;

	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_shadow");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_shadow");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) return false;
	program_ = bgfx::createProgram(vs, fs, true);

	const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
			BGFX_SAMPLER_UVW_CLAMP;
	const bgfx::TextureFormat::Enum formats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	const char* names[] = {"D24S8", "D32F", "D16"};
	const char* chosen = nullptr;
	for (size_t i = 0; i < 3 && !bgfx::isValid(depth_); ++i) {
		if (!bgfx::isTextureValid(0, false, 1, formats[i], flags)) continue;
		depth_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
				formats[i], flags);
		chosen = names[i];
	}
	if (!bgfx::isValid(depth_)) {
		LogWarn("character shadows: no sampleable depth format, off");
		Shutdown();
		return false;
	}
	fb_ = bgfx::createFrameBuffer(1, &depth_, true);
	// One atlas texel, the filter step on both axes.
	texelUv_ = 1.f / float(width);
	casters_.reserve(size_t(slots_));
	LogInfo("character shadows: %d slots of %d, %dx%d %s", slots_, slotSize, width, height, chosen);
	return true;
}

void CharacterShadows::Shutdown() {
	// The framebuffer owns the depth texture.
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	fb_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
	casters_.clear();
	slots_ = cols_ = rows_ = slotSize_ = 0;
}

void CharacterShadows::BeginFrame(bgfx::ViewId view) {
	casters_.clear();
	view_ = view;
	if (!ready()) return;
	float identity[16];
	bx::mtxIdentity(identity);
	bgfx::setViewFrameBuffer(view, fb_);
	bgfx::setViewRect(view, 0, 0, uint16_t(cols_ * slotSize_), uint16_t(rows_ * slotSize_));
	bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
	// Each draw carries its slot's whole transform; the view adds nothing.
	bgfx::setViewTransform(view, identity, identity);
	// Cleared even with no casters, so the receivers never read last frame.
	bgfx::touch(view);
}

void CharacterShadows::Reach(const Vec3& lo, const Vec3& hi, const Vec3& toLightIn,
		Vec3& outLo, Vec3& outHi) {
	const Vec3 l = UnitToLight(toLightIn);
	const Vec3 c = (lo + hi) * 0.5f;
	const float r = SphereRadius(lo, hi);
	const float reach = FadeLength(lo, hi);
	// The slot's box: r about the axis, from past the sphere toward the light to
	// the end of the fade away from it.
	for (int a = 0; a < 3; ++a) {
		const float nearEnd = c[a] + l[a] * (r + kPad);
		const float farEnd = c[a] - l[a] * (r + reach);
		const float side = r * 1.4143f * std::sqrt(std::max(1.f - l[a] * l[a], 0.f));
		outLo[a] = std::min(nearEnd, farEnd) - side;
		outHi[a] = std::max(nearEnd, farEnd) + side;
	}
}

bool CharacterShadows::Add(int instance, const Vec3& lo, const Vec3& hi, const Vec3& toLightIn,
		float strength) {
	if (!ready() || int(casters_.size()) >= slots_) return false;
	Caster cast;
	cast.instance = instance;
	cast.strength = strength;
	const Vec3 l = UnitToLight(toLightIn);
	cast.toLight = l;
	const Vec3 c = (lo + hi) * 0.5f;
	const float r = SphereRadius(lo, hi);
	const float reach = FadeLength(lo, hi);

	float view[16], proj[16];
	const Vec3 up = UpFor(l);
	const bx::Vec3 eye = {c[0] + l[0] * (r + kPad), c[1] + l[1] * (r + kPad), c[2] + l[2] * (r + kPad)};
	bx::mtxLookAt(view, eye, {c[0], c[1], c[2]}, {up[0], up[1], up[2]}, bx::Handedness::Right);
	// Snapped to the slot's texel grid, as ShadowMap::BeginOrtho does, so a caster
	// standing still keeps a still edge.
	const float texel = 2.f * r / float(slotSize_);
	const float rx = view[12] - std::round(view[12] / texel) * texel;
	const float ry = view[13] - std::round(view[13] / texel) * texel;
	const bgfx::Caps* caps = bgfx::getCaps();
	bx::mtxOrtho(proj, -r + rx, r + rx, -r + ry, r + ry, 0.f, kPad + 2.f * r + reach, 0.f,
			caps->homogeneousDepth, bx::Handedness::Right);

	// The slot in clip space: the atlas cell `index`, top-left first in view pixels.
	const int index = int(casters_.size());
	const int col = index % cols_, row = index / cols_;
	const float sx = 1.f / float(cols_), sy = 1.f / float(rows_);
	const float ox = -1.f + float(2 * col + 1) * sx;
	const float oy = 1.f - float(2 * row + 1) * sy;
	const float clipCrop[16] = {
		sx, 0.f, 0.f, 0.f,
		0.f, sy, 0.f, 0.f,
		0.f, 0.f, 1.f, 0.f,
		ox, oy, 0.f, 1.f,
	};
	float viewProj[16];
	bx::mtxMul(viewProj, view, proj);
	bx::mtxMul(cast.drawMatrix, viewProj, clipCrop);

	// Clip -> texture, per backend, as the flashlight's map does it.
	const float uvY = caps->originBottomLeft ? 0.5f : -0.5f;
	const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
	const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
	const float uvCrop[16] = {
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, uvY, 0.0f, 0.0f,
		0.0f, 0.0f, sz, 0.0f,
		0.5f, 0.5f, tz, 1.0f,
	};
	bx::mtxMul(cast.receiverMatrix, cast.drawMatrix, uvCrop);

	// The cell in uv, inset past the 3x3 filter so no tap reads a neighbour.
	const float u0 = 0.5f * (ox - sx) + 0.5f, u1 = 0.5f * (ox + sx) + 0.5f;
	const float va = uvY * (oy - sy) + 0.5f, vb = uvY * (oy + sy) + 0.5f;
	const float inset = 1.5f * texelUv_;
	cast.rect[0] = u0 + inset;
	cast.rect[1] = std::min(va, vb) + inset;
	cast.rect[2] = u1 - inset;
	cast.rect[3] = std::max(va, vb) - inset;
	// The caster's own centre has to land inside its cell, in front of the far plane.
	const bx::Vec3 at = bx::mulH({c[0], c[1], c[2]}, cast.receiverMatrix);
	PAINFUL_CHECK(at.x >= cast.rect[0] && at.x <= cast.rect[2] && at.y >= cast.rect[1] &&
			at.y <= cast.rect[3] && at.z > 0.f && at.z < 1.f,
			"character shadow: centre maps to (%.3f, %.3f, %.3f), cell %.3f..%.3f x %.3f..%.3f",
			at.x, at.y, at.z, cast.rect[0], cast.rect[2], cast.rect[1], cast.rect[3]);
	cast.scissor[0] = uint16_t(col * slotSize_);
	cast.scissor[1] = uint16_t(row * slotSize_);
	cast.scissor[2] = uint16_t(slotSize_);
	cast.scissor[3] = uint16_t(slotSize_);

	// The fade starts at the bounds' corner nearest the light; the original's plane
	// runs through the max corner, which is that corner for a light from above.
	float start = -1e30f;
	for (int k = 0; k < 8; ++k) {
		const Vec3 corner{k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2]};
		start = std::max(start, Dot(corner, l));
	}
	cast.fadeStart = start;
	cast.fadeRate = 1.f / reach;
	cast.normalOffset = ShadowMap::kNormalOffset * texel;
	cast.lightOffset = ShadowMap::kLightOffset * texel;
	Reach(lo, hi, l, cast.reachLo, cast.reachHi);
	casters_.push_back(cast);
	return true;
}

} // namespace painful
