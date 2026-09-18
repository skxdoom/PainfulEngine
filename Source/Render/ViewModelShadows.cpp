#include "ViewModelShadows.h"
#include "ShaderLoad.h"
#include "../Core/Check.h"
#include "../Core/Log.h"

#include <algorithm>
#include <bx/math.h>
#include <cmath>

namespace painful {

namespace {

// A roll for a view that is stable and never parallel to it.
Vec3 UpFor(const Vec3& dir) {
	return std::abs(dir[1]) > 0.9f ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
}

} // namespace

bool ViewModelShadows::Init(const std::string& shaderDir, int cellSize) {
	if (cellSize <= 0) return false;
	const bgfx::Caps* caps = bgfx::getCaps();
	if (!(caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL)) return false;
	const int width = kCols * cellSize, height = kRows * cellSize;
	if (width > int(caps->limits.maxTextureSize) || height > int(caps->limits.maxTextureSize)) {
		LogWarn("view model shadows: %dx%d is past the texture limit, off", width, height);
		return false;
	}
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
		depth_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, formats[i], flags);
	}
	if (!bgfx::isValid(depth_)) {
		Shutdown();
		return false;
	}
	fb_ = bgfx::createFrameBuffer(1, &depth_, true);
	cellSize_ = cellSize;
	texelUv_[0] = 1.f / float(width);
	texelUv_[1] = 1.f / float(height);
	BeginFrame();
	LogInfo("view model shadows: %d cells of %d, %dx%d", 1 + kLights, cellSize, width, height);
	return true;
}

void ViewModelShadows::Shutdown() {
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	fb_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
	cellSize_ = 0;
	lights_ = 0;
	for (Cell& c : cells_) c.active = false;
}

void ViewModelShadows::BeginFrame() {
	for (Cell& c : cells_) c.active = false;
	lights_ = 0;
	if (!ready()) return;
	float identity[16];
	bx::mtxIdentity(identity);
	bgfx::setViewFrameBuffer(view_, fb_);
	bgfx::setViewRect(view_, 0, 0, uint16_t(kCols * cellSize_), uint16_t(kRows * cellSize_));
	bgfx::setViewClear(view_, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
	// Each draw carries its cell's whole transform; the view adds nothing.
	bgfx::setViewTransform(view_, identity, identity);
	bgfx::touch(view_);
}

// The cell `index` of the atlas: its clip-space crop, the receiver's texture
// matrix, the inset uv rect and the pixel scissor.
void ViewModelShadows::Place(Cell& cell, int index, const float view[16], const float proj[16]) {
	const bgfx::Caps* caps = bgfx::getCaps();
	const int col = index % kCols, row = index / kCols;
	const float sx = 1.f / float(kCols), sy = 1.f / float(kRows);
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
	bx::mtxMul(cell.drawMatrix, viewProj, clipCrop);
	cell.frustum = Frustum::FromViewProj(view, proj);

	const float uvY = caps->originBottomLeft ? 0.5f : -0.5f;
	const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
	const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
	const float uvCrop[16] = {
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, uvY, 0.0f, 0.0f,
		0.0f, 0.0f, sz, 0.0f,
		0.5f, 0.5f, tz, 1.0f,
	};
	bx::mtxMul(cell.receiverMatrix, cell.drawMatrix, uvCrop);

	const float u0 = 0.5f * (ox - sx) + 0.5f, u1 = 0.5f * (ox + sx) + 0.5f;
	const float va = uvY * (oy - sy) + 0.5f, vb = uvY * (oy + sy) + 0.5f;
	cell.rect[0] = u0 + 1.5f * texelUv_[0];
	cell.rect[1] = std::min(va, vb) + 1.5f * texelUv_[1];
	cell.rect[2] = u1 - 1.5f * texelUv_[0];
	cell.rect[3] = std::max(va, vb) - 1.5f * texelUv_[1];
	cell.scissor[0] = uint16_t(col * cellSize_);
	cell.scissor[1] = uint16_t(row * cellSize_);
	cell.scissor[2] = uint16_t(cellSize_);
	cell.scissor[3] = uint16_t(cellSize_);
}

void ViewModelShadows::Begin(const Vec3& toLightIn, const Vec3& centre, float radius) {
	if (!ready() || radius <= 0.f) return;
	Vec3 toLight = toLightIn;
	if (toLight.LengthSq() < 1e-12f) toLight = Vec3{0.f, 1.f, 0.f};
	toLight /= toLight.Length();
	// Just the sphere, with a margin: the weapon is the only caster.
	const float extent = radius * 1.1f;
	const float back = radius * 2.f;
	const Vec3 up = UpFor(toLight);
	const bx::Vec3 eye = {centre[0] + toLight[0] * back, centre[1] + toLight[1] * back,
			centre[2] + toLight[2] * back};
	float view[16], proj[16];
	bx::mtxLookAt(view, eye, {centre[0], centre[1], centre[2]}, {up[0], up[1], up[2]},
			bx::Handedness::Right);
	bx::mtxOrtho(proj, -extent, extent, -extent, extent, 0.f, back + radius * 1.5f, 0.f,
			bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
	Cell& cell = cells_[0];
	Place(cell, 0, view, proj);
	cell.texel = 2.f * extent / float(cellSize_);
	cell.lightId = 0;
	cell.active = true;
}

bool ViewModelShadows::AddLight(int lightId, const Vec3& lightPos, const Vec3& centre,
		float radius) {
	if (!ready() || lights_ >= kLights || radius <= 0.f) return false;
	const Vec3 toCentre = centre - lightPos;
	const float d = toCentre.Length();
	const float r = radius * 1.1f;
	// A light inside the sphere (a muzzle flash at the barrel) has no single
	// view that frames the weapon.
	if (d <= r * 1.1f) return false;
	const float half = std::asin(std::min(r / d, 0.98f));
	const Vec3 dir = toCentre / d;
	const Vec3 up = UpFor(dir);
	float view[16], proj[16];
	bx::mtxLookAt(view, {lightPos[0], lightPos[1], lightPos[2]}, {centre[0], centre[1], centre[2]},
			{up[0], up[1], up[2]}, bx::Handedness::Right);
	bx::mtxProj(proj, 2.f * bx::toDeg(half), 1.f, std::max(d - r, d * 0.02f), d + r,
			bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
	const int index = 1 + lights_;
	Cell& cell = cells_[index];
	Place(cell, index, view, proj);
	cell.texel = 2.f * std::tan(half) / float(cellSize_);
	cell.lightId = lightId;
	cell.lightPos = lightPos;
	cell.active = true;
	// The weapon's centre has to land inside its cell, in front of the far plane.
	const float c[4] = {centre[0], centre[1], centre[2], 1.f};
	float o[4];
	bx::vec4MulMtx(o, c, cell.receiverMatrix);
	const float u = o[0] / o[3], v = o[1] / o[3], z = o[2] / o[3];
	PAINFUL_CHECK(o[3] > 0.f && u >= cell.rect[0] && u <= cell.rect[2] && v >= cell.rect[1] &&
			v <= cell.rect[3] && z > 0.f && z < 1.f,
			"view model light shadow: centre maps to (%.3f, %.3f, %.3f), cell %.3f..%.3f x %.3f..%.3f",
			u, v, z, cell.rect[0], cell.rect[2], cell.rect[1], cell.rect[3]);
	++lights_;
	return true;
}

} // namespace painful
