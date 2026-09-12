#include "DemonFx.h"
#include "FullScreenPass.h"
#include "SceneTargets.h"
#include "ShaderLoad.h"
#include "TextureCache.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace painful {

namespace {

bgfx::ProgramHandle MakeProgram(const std::string& shaderDir, bgfx::ShaderHandle vs, const char* fs) {
	bgfx::ShaderHandle f = LoadShader(shaderDir, fs);
	if (!bgfx::isValid(f)) return BGFX_INVALID_HANDLE;
	const bgfx::ProgramHandle p = bgfx::createProgram(vs, f, false);
	bgfx::destroy(f);
	return p;
}

} // namespace

bool DemonFx::Init(const std::string& shaderDir, TextureCache& textures) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	if (!bgfx::isValid(vs)) return false;
	gray_ = MakeProgram(shaderDir, vs, "fs_demon_gray");
	warpProgram_ = MakeProgram(shaderDir, vs, "fs_demon_warp");
	bgfx::destroy(vs);
	if (!bgfx::isValid(gray_) || !bgfx::isValid(warpProgram_)) {
		LogWarn("demon fx: shaders missing, off");
		Shutdown();
		return false;
	}
	sScene_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
	sDudv_ = bgfx::createUniform("s_dudv", bgfx::UniformType::Sampler);
	sPrev_ = bgfx::createUniform("s_prev", bgfx::UniformType::Sampler);
	uGray_ = bgfx::createUniform("u_demonGray", bgfx::UniformType::Vec4);
	uWarp_ = bgfx::createUniform("u_demonWarp", bgfx::UniformType::Vec4);

	// The three textures of the effect, from skin.shader's palskinned_fresnel
	// and the pass itself. The cache owns the first two; the dudv map is
	// built here as a SIGNED texture, the way the original's bump-format
	// load reads its bytes, so the sampler filters it in signed space.
	detail_ = textures.Get("special/fresnel_detail", "");
	ramp_ = textures.Get("special/fresnel_func", "");
	dudv_ = LoadDudv(textures.Resolve("special/warp_dudv", ""));
	if (!bgfx::isValid(detail_) || !bgfx::isValid(ramp_) || !bgfx::isValid(dudv_))
		LogWarn("demon fx: a special/ texture is missing (fresnel_detail, fresnel_func, warp_dudv)");
	return true;
}

// warp_dudv.tga: 24- or 32-bit uncompressed. Its bytes are two's complement
// (0 is no offset, 255 is -1), which a linear sampler on an unsigned texture
// would blend THROUGH 128 = -128 at every zero crossing. Docs/Reference/DemonFx.md.
bgfx::TextureHandle DemonFx::LoadDudv(const std::string& path) {
	std::vector<uint8_t> tga;
	if (path.empty() || !ReadFile(path, tga) || tga.size() < 18 || tga[2] != 2) return BGFX_INVALID_HANDLE;
	const int w = tga[12] | (tga[13] << 8), h = tga[14] | (tga[15] << 8);
	const int bpp = tga[16] / 8;
	const bool topDown = (tga[17] & 0x20) != 0;
	const size_t start = 18 + tga[0];
	if ((bpp != 3 && bpp != 4) || w <= 0 || h <= 0 || tga.size() < start + size_t(w) * h * bpp)
		return BGFX_INVALID_HANDLE;
	const bgfx::Memory* mem = bgfx::alloc(uint32_t(w * h * 2));
	for (int y = 0; y < h; ++y) {
		const int src = topDown ? y : h - 1 - y;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = &tga[start + (size_t(src) * w + x) * bpp]; // BGR(A)
			mem->data[(size_t(y) * w + x) * 2 + 0] = p[2]; // R = du
			mem->data[(size_t(y) * w + x) * 2 + 1] = p[1]; // G = dv
		}
	}
	return bgfx::createTexture2D(uint16_t(w), uint16_t(h), false, 1, bgfx::TextureFormat::RG8S,
			BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
}

void DemonFx::Shutdown() {
	ReleaseTargets();
	for (bgfx::ProgramHandle* p : {&gray_, &warpProgram_}) {
		if (bgfx::isValid(*p)) bgfx::destroy(*p);
		*p = BGFX_INVALID_HANDLE;
	}
	for (bgfx::UniformHandle* u : {&sScene_, &sDudv_, &sPrev_, &uGray_, &uWarp_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	// The detail and the ramp belong to the cache; the dudv map is ours.
	if (bgfx::isValid(dudv_)) bgfx::destroy(dudv_);
	detail_ = ramp_ = dudv_ = BGFX_INVALID_HANDLE;
	active_ = false;
}

void DemonFx::ReleaseTargets() {
	// grayFb_ borrows the scene depth, so it owns nothing; the trail pair
	// owns its textures (createFrameBuffer's destroy flag).
	if (bgfx::isValid(grayFb_)) bgfx::destroy(grayFb_);
	grayFb_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(grayColor_)) bgfx::destroy(grayColor_);
	grayColor_ = BGFX_INVALID_HANDLE;
	for (int i = 0; i < 2; ++i) {
		if (bgfx::isValid(pingFb_[i])) bgfx::destroy(pingFb_[i]);
		pingFb_[i] = BGFX_INVALID_HANDLE;
		ping_[i] = BGFX_INVALID_HANDLE;
	}
	width_ = height_ = halfW_ = halfH_ = 0;
	msaa_ = -1;
	builtDepth_ = BGFX_INVALID_HANDLE;
	fresh_ = true;
}

bool DemonFx::BuildTargets(const SceneTargets& scene) {
	ReleaseTargets();
	const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const uint64_t msaa = SceneTargets::MsaaTextureFlag(scene.msaa());
	// The grayscale target is multisampled like the scene, whose depth it
	// shares: the demonic models are depth-tested against the world they
	// were left out of.
	grayColor_ = bgfx::createTexture2D(uint16_t(scene.width()), uint16_t(scene.height()), false,
			1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | msaa | clamp);
	// The warp and the trail run at half size, for the original's softness.
	for (int i = 0; i < 2; ++i)
		ping_[i] = bgfx::createTexture2D(uint16_t(scene.halfWidth()), uint16_t(scene.halfHeight()),
				false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | clamp);
	if (!bgfx::isValid(grayColor_) || !bgfx::isValid(ping_[0]) || !bgfx::isValid(ping_[1])) {
		LogWarn("demon fx: no targets at %dx%d, off", scene.width(), scene.height());
		ReleaseTargets();
		return false;
	}
	const bgfx::TextureHandle gray[] = {grayColor_, scene.depth()};
	grayFb_ = bgfx::createFrameBuffer(2, gray, false);
	for (int i = 0; i < 2; ++i) pingFb_[i] = bgfx::createFrameBuffer(1, &ping_[i], true);
	if (!bgfx::isValid(grayFb_) || !bgfx::isValid(pingFb_[0]) || !bgfx::isValid(pingFb_[1])) {
		LogWarn("demon fx: no framebuffers at %dx%d, off", scene.width(), scene.height());
		ReleaseTargets();
		return false;
	}
	width_ = scene.width();
	height_ = scene.height();
	halfW_ = scene.halfWidth();
	halfH_ = scene.halfHeight();
	msaa_ = scene.msaa();
	builtDepth_ = scene.depth();
	fresh_ = true;
	LogInfo("demon fx: gray %dx%d msaa x%d, trail %dx%d", width_, height_, msaa_, halfW_, halfH_);
	return true;
}

void DemonFx::SetParams(float scale, float bias, float keep, float mblur) {
	scale_ = scale;
	bias_ = bias;
	keep_ = keep;
	mblur_ = std::max(0.f, std::min(mblur, 0.999f));
}

void DemonFx::BeginFrame(const SceneTargets& scene, bool enabled, bgfx::ViewId entityView) {
	const bool was = active_;
	active_ = false;
	if (enabled && ready() && scene.active()) {
		const bool stale = scene.width() != width_ || scene.height() != height_ ||
				scene.msaa() != msaa_ || scene.depth().idx != builtDepth_.idx;
		if (!stale || BuildTargets(scene)) active_ = true;
	}
	if (!was && active_) fresh_ = true; // no last frame to trail from
	bgfx::FrameBufferHandle gray = BGFX_INVALID_HANDLE;
	if (active_) gray = grayFb_;
	bgfx::setViewFrameBuffer(entityView, gray);
	bgfx::setViewRect(entityView, 0, 0, uint16_t(scene.width()), uint16_t(scene.height()));
	bgfx::setViewClear(entityView, BGFX_CLEAR_NONE);
}

// The trail weight is the level's MBlur PER FRAME in the original, which is a
// frame-rate: here it is raised to dt * 60 so the trail lasts the same time
// at any rate, and this frame's weight keeps the level's ratio to it.
void DemonFx::Draw(const SceneTargets& scene, bgfx::ViewId grayView, bgfx::ViewId warpView,
		bgfx::ViewId copyView, float dt) {
	if (!active_) return;
	const int prev = current_ ^ 1;

	const float grayParams[4] = {scale_, bias_, 0.f, 0.f};
	bgfx::setViewFrameBuffer(grayView, grayFb_);
	bgfx::setTexture(0, sScene_, scene.color());
	bgfx::setUniform(uGray_, grayParams);
	FullScreenTriangle(grayView, scene.layout(), width_, height_);
	bgfx::submit(grayView, gray_);

	float prevW = 0.f;
	if (mblur_ > 0.f && !fresh_) prevW = std::pow(mblur_, std::max(dt, 0.f) * 60.f);
	const float newW = mblur_ < 1.f ? keep_ * (1.f - prevW) / (1.f - mblur_) : 1.f - prevW;
	const float warpParams[4] = {warp_, newW, prevW, 0.f};
	bgfx::setViewFrameBuffer(warpView, pingFb_[current_]);
	bgfx::setTexture(0, sScene_, grayColor_);
	bgfx::setTexture(1, sDudv_, bgfx::isValid(dudv_) ? dudv_ : grayColor_);
	bgfx::setTexture(2, sPrev_, ping_[prev]);
	bgfx::setUniform(uWarp_, warpParams);
	FullScreenTriangle(warpView, scene.layout(), halfW_, halfH_);
	bgfx::submit(warpView, warpProgram_);

	// Up onto the backbuffer, bilinear, where the HUD then draws.
	bgfx::setViewFrameBuffer(copyView, BGFX_INVALID_HANDLE);
	bgfx::setTexture(0, scene.sceneSampler(), ping_[current_]);
	FullScreenTriangle(copyView, scene.layout(), width_, height_);
	bgfx::submit(copyView, scene.copyProgram());

	current_ = prev;
	fresh_ = false;
}

} // namespace painful
