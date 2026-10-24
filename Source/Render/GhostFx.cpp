#include "GhostFx.h"
#include "FullScreenPass.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cmath>

namespace painful {

namespace {

bgfx::ProgramHandle MakeProgram(const std::string& shaderDir, const char* fs) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle f = LoadShader(shaderDir, fs);
	if (!bgfx::isValid(vs) || !bgfx::isValid(f)) {
		if (bgfx::isValid(vs)) bgfx::destroy(vs);
		if (bgfx::isValid(f)) bgfx::destroy(f);
		return BGFX_INVALID_HANDLE;
	}
	return bgfx::createProgram(vs, f, true);
}

} // namespace

bool GhostFx::Init(const std::string& shaderDir) {
	layout_ = PostVertexLayout();
	compose_ = MakeProgram(shaderDir, "fs_compose");
	copy_ = MakeProgram(shaderDir, "fs_post_copy");
	if (!ready()) {
		LogWarn("ghost fx: shaders missing, off");
		Shutdown();
		return false;
	}
	const char* names[3] = {"s_tex0", "s_tex1", "s_tex2"};
	for (int i = 0; i < 3; ++i) sTex_[i] = bgfx::createUniform(names[i], bgfx::UniformType::Sampler);
	uWeights_ = bgfx::createUniform("u_composeWeights", bgfx::UniformType::Vec4);
	uShift_ = bgfx::createUniform("u_composeShift", bgfx::UniformType::Vec4, 2);
	sScene_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
	return true;
}

void GhostFx::Shutdown() {
	ReleaseTargets();
	for (bgfx::UniformHandle* u : {&sTex_[0], &sTex_[1], &sTex_[2], &uWeights_, &uShift_, &sScene_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	for (bgfx::ProgramHandle* p : {&compose_, &copy_}) {
		if (bgfx::isValid(*p)) bgfx::destroy(*p);
		*p = BGFX_INVALID_HANDLE;
	}
}

void GhostFx::ReleaseTargets() {
	if (bgfx::isValid(ghostFb_)) bgfx::destroy(ghostFb_);
	ghostFb_ = BGFX_INVALID_HANDLE;
	ghostColor_ = BGFX_INVALID_HANDLE;
	for (int i = 0; i < 2; ++i) {
		if (bgfx::isValid(pingFb_[i])) bgfx::destroy(pingFb_[i]);
		pingFb_[i] = BGFX_INVALID_HANDLE;
		ping_[i] = BGFX_INVALID_HANDLE;
	}
	width_ = height_ = halfW_ = halfH_ = 0;
	fresh_ = true;
}

bool GhostFx::BuildTargets(int width, int height) {
	ReleaseTargets();
	const uint16_t w = uint16_t(std::max(width / 2, 1));
	const uint16_t h = uint16_t(std::max(height / 2, 1));
	const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	// The ghosts' own depth: the world is not in it, so walls do not hide them.
	ghostColor_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_RT | clamp);
	const bgfx::TextureHandle depth = bgfx::createTexture2D(w, h, false, 1,
			bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
	for (int i = 0; i < 2; ++i)
		ping_[i] = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8,
				BGFX_TEXTURE_RT | clamp);
	if (!bgfx::isValid(ghostColor_) || !bgfx::isValid(depth) || !bgfx::isValid(ping_[0]) ||
			!bgfx::isValid(ping_[1])) {
		LogWarn("ghost fx: no targets at %dx%d, off", int(w), int(h));
		if (bgfx::isValid(depth)) bgfx::destroy(depth);
		if (bgfx::isValid(ghostColor_)) bgfx::destroy(ghostColor_);
		for (bgfx::TextureHandle& t : ping_)
			if (bgfx::isValid(t)) bgfx::destroy(t);
		ReleaseTargets();
		return false;
	}
	const bgfx::TextureHandle ghost[] = {ghostColor_, depth};
	ghostFb_ = bgfx::createFrameBuffer(2, ghost, true);
	for (int i = 0; i < 2; ++i) pingFb_[i] = bgfx::createFrameBuffer(1, &ping_[i], true);
	width_ = width;
	height_ = height;
	halfW_ = w;
	halfH_ = h;
	fresh_ = true;
	return true;
}

void GhostFx::BeginFrame(int width, int height, bool enabled, bgfx::ViewId entityView) {
	const bool was = active_;
	active_ = false;
	if (enabled && ready() && ((width == width_ && height == height_) || BuildTargets(width, height)))
		active_ = true;
	if (!was && active_) fresh_ = true; // no last frame to trail from
	bgfx::setViewFrameBuffer(entityView,
			active_ ? ghostFb_ : bgfx::FrameBufferHandle(BGFX_INVALID_HANDLE));
	bgfx::setViewRect(entityView, 0, 0, uint16_t(std::max(halfW_, 1)), uint16_t(std::max(halfH_, 1)));
	bgfx::setViewClear(entityView, active_ ? BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH : BGFX_CLEAR_NONE,
			0x000000ff, 1.f, 0);
	if (active_) bgfx::touch(entityView);
}

// The original mixes 0.2 of this frame into 0.8 of the last, per frame; here
// the old weight is raised to dt * 60 and the new one takes the rest, which
// is the same at 60 a second and keeps the trail's length on the clock.
void GhostFx::Draw(bgfx::ViewId trailView, bgfx::ViewId addView, float dt) {
	if (!active_) return;
	const int prev = current_ ^ 1;
	const float prevW = fresh_ ? 0.f : std::pow(0.8f, std::max(dt, 0.f) * 60.f);
	const float weights[4] = {1.f - prevW, prevW, 0.f, 0.f};
	bgfx::setViewFrameBuffer(trailView, pingFb_[current_]);
	bgfx::setTexture(0, sTex_[0], ghostColor_);
	bgfx::setTexture(1, sTex_[1], ping_[prev]);
	bgfx::setTexture(2, sTex_[2], ping_[prev]);
	bgfx::setUniform(uWeights_, weights);
	const float noShift[8] = {};
	bgfx::setUniform(uShift_, noShift, 2);
	FullScreenTriangle(trailView, layout_, halfW_, halfH_);
	bgfx::submit(trailView, compose_);

	// hud_add: the trail added over the frame, under the HUD.
	bgfx::setViewFrameBuffer(addView, BGFX_INVALID_HANDLE);
	bgfx::setTexture(0, sScene_, ping_[current_]);
	FullScreenTriangle(addView, layout_, width_, height_);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD);
	bgfx::submit(addView, copy_);

	current_ = prev;
	fresh_ = false;
}

} // namespace painful
