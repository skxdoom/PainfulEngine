#include "LightShadowAtlas.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <bx/math.h>
#include <cmath>

namespace painful {

namespace {

// The six faces of a point light: forward and up. The receiver in
// shared_lights.sh carries the SAME table, and right = cross(forward, up)
// on both sides - change one and the other.
const Vec3 kFaceForward[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const Vec3 kFaceUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

// A right-handed view down `forward` with `up`, row-vector form: x right,
// y up, z toward the viewer, so a point ahead has view z = -dot(d, forward).
void FaceView(float out[16], const Vec3& eye, const Vec3& forward, const Vec3& up) {
	const Vec3 right = Cross(forward, up);
	out[0] = right[0]; out[1] = up[0]; out[2] = -forward[0]; out[3] = 0.f;
	out[4] = right[1]; out[5] = up[1]; out[6] = -forward[1]; out[7] = 0.f;
	out[8] = right[2]; out[9] = up[2]; out[10] = -forward[2]; out[11] = 0.f;
	out[12] = -Dot(eye, right); out[13] = -Dot(eye, up); out[14] = Dot(eye, forward); out[15] = 1.f;
}

// The receiver never sees a matrix: shared_lights.sh finds a point's face
// uv and depth analytically. This runs the same arithmetic on the CPU
// against the matrices the faces actually render with, so a sign slip in
// either shows up in the log rather than as shadows in the wrong place.
float LookupError(const bgfx::Caps* caps, float cot) {
	const float kNear = 0.1f, kFar = 12.f;
	const float fovDegrees = 2.f * std::atan(1.f / cot) * 180.f / 3.14159265f;
	const float diff = kFar - kNear;
	const float a = caps->homogeneousDepth ? 0.5f * (kFar + kNear) / diff + 0.5f : kFar / diff;
	const float b = caps->homogeneousDepth ? -kFar * kNear / diff : -kNear * kFar / diff;
	const float sy = caps->originBottomLeft ? 1.f : -1.f;
	const Vec3 eye{3.f, 2.f, -1.f};
	const Vec3 points[] = {{5.f, 2.5f, -0.5f}, {1.f, 3.f, 0.5f}, {3.5f, 6.f, -1.5f},
			{2.5f, -2.f, -0.5f}, {3.2f, 1.5f, 4.f}, {2.8f, 2.6f, -6.f}};
	float worst = 0.f;
	for (int f = 0; f < 6; ++f) {
		float view[16], proj[16], vp[16];
		FaceView(view, eye, kFaceForward[f], kFaceUp[f]);
		bx::mtxProj(proj, fovDegrees, 1.f, kNear, kFar, caps->homogeneousDepth, bx::Handedness::Right);
		bx::mtxMul(vp, view, proj);
		const Vec3& p = points[f];
		float clip[4];
		for (int c = 0; c < 4; ++c)
			clip[c] = p[0] * vp[c] + p[1] * vp[4 + c] + p[2] * vp[8 + c] + vp[12 + c];
		const float mu = clip[0] / clip[3] * 0.5f + 0.5f;
		const float mv = 0.5f + sy * 0.5f * clip[1] / clip[3];
		const float mz = caps->homogeneousDepth ? clip[2] / clip[3] * 0.5f + 0.5f : clip[2] / clip[3];
		// The shader's way.
		const Vec3 d = p - eye;
		const Vec3 F = kFaceForward[f], U = kFaceUp[f], R = Cross(F, U);
		const float dist = Dot(d, F);
		const float su = 0.5f + 0.5f * Dot(d, R) * cot / dist;
		const float sv = 0.5f + 0.5f * sy * Dot(d, U) * cot / dist;
		const float sz = a + b / dist;
		worst = std::max(worst, std::max(std::abs(mu - su), std::max(std::abs(mv - sv), std::abs(mz - sz))));
	}
	return worst;
}

} // namespace

bool LightShadowAtlas::Init(const std::string& shaderDir, int faceSize, int slots) {
	if (faceSize <= 0 || slots <= 0) return false;
	slots = std::min(slots, kMaxSlots);
	const bgfx::Caps* caps = bgfx::getCaps();
	if (!(caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL)) {
		LogWarn("light shadows: no hardware depth compare, off");
		return false;
	}
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_shadow");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_shadow");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) return false;
	program_ = bgfx::createProgram(vs, fs, true);

	const int width = faceSize * 3, height = faceSize * 2 * slots;
	if (width > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize) {
		LogWarn("light shadows: %dx%d atlas is over the %u limit, off", width, height,
				caps->limits.maxTextureSize);
		Shutdown();
		return false;
	}
	const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
			BGFX_SAMPLER_UVW_CLAMP;
	const bgfx::TextureFormat::Enum formats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	for (size_t i = 0; i < 3 && !bgfx::isValid(depth_); ++i) {
		if (!bgfx::isTextureValid(0, false, 1, formats[i], flags)) continue;
		depth_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
				formats[i], flags);
	}
	if (!bgfx::isValid(depth_)) {
		LogWarn("light shadows: no sampleable depth format, off");
		Shutdown();
		return false;
	}
	fb_ = bgfx::createFrameBuffer(1, &depth_, true);
	faceSize_ = faceSize;
	slots_ = slots;
	info_[0] = 1.f / float(2 * slots);
	info_[1] = caps->originBottomLeft ? 1.f : -1.f;
	info_[2] = 1.f / float(width);
	info_[3] = 1.f / float(height);
	slotFaces_.assign(size_t(slots), 0);
	faceFrustum_.assign(size_t(slots) * 6, Frustum{});
	slotParams_.assign(size_t(slots) * 4, 0.f);
	BeginFrame();
	const float err = LookupError(caps, pointCot());
	LogInfo("light shadows: %d lights, %d-texel faces, %dx%d atlas, lookup check %s (%.2g)",
			slots, faceSize, width, height, err < 1e-4f ? "ok" : "FAILED", err);
	return true;
}

void LightShadowAtlas::Shutdown() {
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	fb_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
	slots_ = 0;
	faceSize_ = 0;
}

void LightShadowAtlas::BeginFrame() {
	for (int s = 0; s < slots_; ++s) {
		slotFaces_[s] = 0;
		slotParams_[s * 4] = -1.f;
	}
}

int LightShadowAtlas::Begin(int slot, const LightSource& light) {
	if (!ready() || slot < 0 || slot >= slots_) return 0;
	const bgfx::Caps* caps = bgfx::getCaps();
	constexpr float kNear = 0.1f;
	const float farPlane = std::max(light.range, kNear * 2.f);
	const bool spot = light.type == LightSource::kSpot && light.coneOuterCos > -1.f;

	// The map's 0..1 depth of a point `dist` along the face: A + B / dist,
	// from bx::mtxProj's right-handed z (aa - bb / dist), remapped when the
	// backend's clip depth is -1..1.
	const float diff = farPlane - kNear;
	float a, b;
	if (caps->homogeneousDepth) {
		a = 0.5f * (farPlane + kNear) / diff + 0.5f;
		b = -farPlane * kNear / diff;
	} else {
		a = farPlane / diff;
		b = -kNear * farPlane / diff;
	}

	const int faces = spot ? 1 : 6;
	float cot = 0.f;
	for (int f = 0; f < faces; ++f) {
		Vec3 forward, up;
		float fovDegrees = 90.f;
		if (spot) {
			forward = light.dir;
			if (forward.LengthSq() < 1e-12f) forward = Vec3{0.f, 0.f, -1.f};
			forward /= forward.Length();
			// PackLight's basis, and the receiver's: up from the axis.
			const Vec3 pick = std::abs(forward[1]) > 0.9f ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
			Vec3 right = Cross(forward, pick);
			if (right.LengthSq() > 1e-12f) right /= right.Length();
			up = Cross(right, forward);
			const float cosOuter = std::min(std::max(light.coneOuterCos, 0.2f), 0.999f);
			fovDegrees = 2.f * std::acos(cosOuter) * 180.f / 3.14159265f;
			cot = 1.f / std::tan(std::acos(cosOuter));
		} else {
			forward = kFaceForward[f];
			up = kFaceUp[f];
			// The guard band: kGuardTexels wider than 90 degrees each side.
			fovDegrees = 2.f * std::atan(1.f / pointCot()) * 180.f / 3.14159265f;
		}
		float view[16], proj[16];
		FaceView(view, light.pos, forward, up);
		bx::mtxProj(proj, fovDegrees, 1.f, kNear, farPlane, caps->homogeneousDepth,
				bx::Handedness::Right);
		faceFrustum_[size_t(slot) * 6 + f] = Frustum::FromViewProj(view, proj);

		const bgfx::ViewId id = viewId(slot, f);
		bgfx::setViewFrameBuffer(id, fb_);
		bgfx::setViewRect(id, uint16_t((f % 3) * faceSize_),
				uint16_t((slot * 2 + f / 3) * faceSize_), uint16_t(faceSize_), uint16_t(faceSize_));
		bgfx::setViewClear(id, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
		bgfx::setViewTransform(id, view, proj);
		bgfx::touch(id);
	}
	(void)cot; // the receiver derives a spot's from its cone, a point's from the guard
	slotFaces_[slot] = faces;
	float* p = &slotParams_[size_t(slot) * 4];
	p[0] = float(slot);
	p[1] = a;
	p[2] = b;
	p[3] = 0.f;
	return faces;
}

void LightShadowAtlas::ReceiverParams(int slot, float out[4]) const {
	if (slot < 0 || slot >= slots_ || slotFaces_[slot] == 0) {
		out[0] = -1.f; out[1] = out[2] = out[3] = 0.f;
		return;
	}
	for (int i = 0; i < 4; ++i) out[i] = slotParams_[size_t(slot) * 4 + i];
}

} // namespace painful
