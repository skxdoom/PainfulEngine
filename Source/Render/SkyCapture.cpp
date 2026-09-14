#include "SkyCapture.h"
#include "Camera.h"
#include "SkyRenderer.h"
#include "../Core/Log.h"

#include <bx/math.h>
#include <algorithm>
#include <cmath>

namespace painful {

bool SkyCapture::Init() {
	const uint64_t caps = bgfx::getCaps()->supported;
	if (!(caps & BGFX_CAPS_TEXTURE_BLIT) || !(caps & BGFX_CAPS_TEXTURE_READ_BACK)) {
		LogWarn("sky capture: no texture blit or read-back on this backend");
		return false;
	}
	const uint16_t s = uint16_t(kFace);
	const uint64_t sampler = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
	target_ = bgfx::createTexture2D(s, s, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | sampler);
	readback_ = bgfx::createTexture2D(s, s, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | sampler);
	if (bgfx::isValid(target_)) framebuffer_ = bgfx::createFrameBuffer(1, &target_, false);
	pixels_.assign(size_t(kFace) * size_t(kFace) * 4, 0);
	Clear();
	if (!bgfx::isValid(framebuffer_) || !bgfx::isValid(readback_)) {
		LogWarn("sky capture: no targets");
		Shutdown();
		return false;
	}
	return true;
}

void SkyCapture::Shutdown() {
	if (bgfx::isValid(framebuffer_)) bgfx::destroy(framebuffer_);
	if (bgfx::isValid(target_)) bgfx::destroy(target_);
	if (bgfx::isValid(readback_)) bgfx::destroy(readback_);
	framebuffer_ = BGFX_INVALID_HANDLE;
	target_ = readback_ = BGFX_INVALID_HANDLE;
}

void SkyCapture::Clear() {
	face_ = 0;
	waiting_ = false;
	sum_.assign(size_t(kWidth) * size_t(kHeight) * 3, 0.0);
	weight_.assign(size_t(kWidth) * size_t(kHeight), 0.0);
	map_.clear();
}

void SkyCapture::Tick(bgfx::ViewId drawView, bgfx::ViewId blitView, SkyRenderer* sky,
		float timeSeconds, uint32_t frameNumber) {
	if (done() || !bgfx::isValid(framebuffer_)) return;
	if (!sky) {
		face_ = 6;
		map_.clear();
		return;
	}
	if (waiting_) {
		if (frameNumber < readyFrame_) return;
		waiting_ = false;
		Accumulate();
		if (++face_ >= 6) {
			Finish();
			return;
		}
	}

	// Six 90-degree faces along the axes. Each texel's direction comes back
	// through the face's own matrices, so only the coverage has to be right.
	const float half = kPi * 0.5f;
	const float yaw[6] = {0.f, kPi, half, -half, 0.f, 0.f};
	const float pitch[6] = {0.f, 0.f, 0.f, 0.f, half, -half};
	Camera eye;
	eye.pos = Vec3{0.f, 0.f, 0.f};
	eye.fovDegrees = 90.f;
	eye.yaw = yaw[face_];
	eye.pitch = pitch[face_];
	eye.up = face_ >= 4 ? Vec3{0.f, 0.f, 1.f} : Vec3{0.f, 1.f, 0.f};

	const uint16_t s = uint16_t(kFace);
	bgfx::setViewFrameBuffer(drawView, framebuffer_);
	bgfx::setViewRect(drawView, 0, 0, s, s);
	bgfx::setViewClear(drawView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
	bgfx::setViewMode(drawView, bgfx::ViewMode::Sequential);
	bgfx::touch(drawView);
	sky->Draw(drawView, eye, kFace, kFace, timeSeconds);
	float viewMtx[16], projMtx[16], viewProj[16];
	eye.ViewProj(kFace, kFace, 2000.f, viewMtx, projMtx); // SkyRenderer::Draw's far plane
	bx::mtxMul(viewProj, viewMtx, projMtx);
	bx::mtxInverse(inverse_, viewProj);

	// The blit runs in a later view, after the face is drawn.
	bgfx::setViewRect(blitView, 0, 0, s, s);
	bgfx::touch(blitView);
	bgfx::blit(blitView, readback_, 0, 0, target_);
	readyFrame_ = bgfx::readTexture(readback_, pixels_.data());
	waiting_ = true;
}

void SkyCapture::Accumulate() {
	const bool bottomUp = bgfx::getCaps()->originBottomLeft;
	for (int j = 0; j < kFace; ++j) {
		for (int i = 0; i < kFace; ++i) {
			const float x = (float(i) + 0.5f) / float(kFace) * 2.f - 1.f;
			const float row = (float(j) + 0.5f) / float(kFace) * 2.f - 1.f;
			const float y = bottomUp ? row : -row;
			const float clip[4] = {x, y, 1.f, 1.f};
			float far[4];
			bx::vec4MulMtx(far, clip, inverse_);
			if (std::fabs(far[3]) < 1e-12f) continue;
			const Vec3 d = Vec3{far[0] / far[3], far[1] / far[3], far[2] / far[3]}.Normalized();
			// A texel's solid angle on a 90-degree face, up to a constant.
			const double w = 1.0 / std::pow(1.0 + double(x) * x + double(y) * y, 1.5);
			const int u = std::clamp(int((std::atan2(d.z, d.x) / (2.f * kPi) + 0.5f) * float(kWidth)), 0,
					kWidth - 1);
			const int v = std::clamp(int((0.5f - std::asin(std::clamp(d.y, -1.f, 1.f)) / kPi) * float(kHeight)),
					0, kHeight - 1);
			const size_t cell = size_t(v) * size_t(kWidth) + size_t(u);
			const uint8_t* px = &pixels_[(size_t(j) * size_t(kFace) + size_t(i)) * 4];
			for (int c = 0; c < 3; ++c) sum_[cell * 3 + size_t(c)] += w * px[c] / 255.0;
			weight_[cell] += w;
		}
	}
}

void SkyCapture::Finish() {
	map_.assign(size_t(kWidth) * size_t(kHeight) * 3, 0.f);
	double total[3] = {0.0, 0.0, 0.0}, totalWeight = 0.0;
	for (size_t cell = 0; cell < weight_.size(); ++cell) {
		for (int c = 0; c < 3; ++c) total[c] += sum_[cell * 3 + size_t(c)];
		totalWeight += weight_[cell];
	}
	// A cell no texel landed in - the cells shrink toward the poles - takes the
	// nearest filled cell in its own column.
	for (int u = 0; u < kWidth; ++u) {
		for (int v = 0; v < kHeight; ++v) {
			int from = -1;
			for (int step = 0; step < kHeight && from < 0; ++step)
				for (int vv : {v - step, v + step})
					if (from < 0 && vv >= 0 && vv < kHeight && weight_[size_t(vv) * size_t(kWidth) + size_t(u)] > 0.0)
						from = vv;
			if (from < 0) continue;
			const size_t cell = size_t(v) * size_t(kWidth) + size_t(u);
			const size_t src = size_t(from) * size_t(kWidth) + size_t(u);
			for (int c = 0; c < 3; ++c)
				map_[cell * 3 + size_t(c)] = float(sum_[src * 3 + size_t(c)] / weight_[src]);
		}
	}
	// Luminance over a band of rows: the top eighth, the middle, the bottom eighth.
	auto band = [&](int v0, int v1) {
		double sum = 0.0;
		for (int v = v0; v < v1; ++v)
			for (int u = 0; u < kWidth; ++u) {
				const float* p = &map_[(size_t(v) * size_t(kWidth) + size_t(u)) * 3];
				sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
			}
		return sum / double((v1 - v0) * kWidth);
	};
	LogInfo("sky capture: mean light %.3f %.3f %.3f; luminance up %.3f, horizon %.3f, down %.3f",
			totalWeight > 0.0 ? total[0] / totalWeight : 0.0, totalWeight > 0.0 ? total[1] / totalWeight : 0.0,
			totalWeight > 0.0 ? total[2] / totalWeight : 0.0, band(0, kHeight / 8),
			band(kHeight * 7 / 16, kHeight * 9 / 16), band(kHeight * 7 / 8, kHeight));
}

} // namespace painful
