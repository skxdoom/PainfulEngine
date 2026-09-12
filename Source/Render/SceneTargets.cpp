#include "SceneTargets.h"
#include "FullScreenPass.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <algorithm>

namespace painful {

uint64_t SceneTargets::MsaaTextureFlag(int samples) {
	if (samples >= 16) return BGFX_TEXTURE_RT_MSAA_X16;
	if (samples >= 6) return BGFX_TEXTURE_RT_MSAA_X8;
	if (samples >= 4) return BGFX_TEXTURE_RT_MSAA_X4;
	if (samples >= 2) return BGFX_TEXTURE_RT_MSAA_X2;
	return 0;
}

bool SceneTargets::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_post_copy");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		LogWarn("scene targets: shaders missing, post-processing off");
		return false;
	}
	copy_ = bgfx::createProgram(vs, fs, true);
	sScene_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
	layout_ = PostVertexLayout();
	return bgfx::isValid(copy_);
}

void SceneTargets::Shutdown() {
	ReleaseTargets();
	if (bgfx::isValid(copy_)) bgfx::destroy(copy_);
	copy_ = BGFX_INVALID_HANDLE;
	if (bgfx::isValid(sScene_)) bgfx::destroy(sScene_);
	sScene_ = BGFX_INVALID_HANDLE;
	active_ = false;
}

void SceneTargets::ReleaseTargets() {
	// The framebuffers own their textures (createFrameBuffer's destroy flag).
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	if (bgfx::isValid(halfFb_)) bgfx::destroy(halfFb_);
	fb_ = halfFb_ = BGFX_INVALID_HANDLE;
	color_ = depth_ = half_ = BGFX_INVALID_HANDLE;
	width_ = height_ = halfW_ = halfH_ = 0;
}

bool SceneTargets::BuildTargets(int width, int height) {
	ReleaseTargets();
	const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const uint64_t msaa = MsaaTextureFlag(msaa_);
	const uint16_t w = uint16_t(width), h = uint16_t(height);
	// Multisampled like the backbuffer; bgfx resolves it when it is sampled
	// (no BGFX_TEXTURE_MSAA_SAMPLE), so readers see a plain texture.
	color_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_RT | msaa | clamp);
	const bgfx::TextureFormat::Enum depthFormats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	for (bgfx::TextureFormat::Enum f : depthFormats) {
		if (bgfx::isValid(depth_)) break;
		if (!bgfx::isTextureValid(0, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY | msaa)) continue;
		depth_ = bgfx::createTexture2D(w, h, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY | msaa);
	}
	halfW_ = std::max(1, width / 2);
	halfH_ = std::max(1, height / 2);
	half_ = bgfx::createTexture2D(uint16_t(halfW_), uint16_t(halfH_), false, 1,
			bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | clamp);
	if (!bgfx::isValid(color_) || !bgfx::isValid(depth_) || !bgfx::isValid(half_)) {
		LogWarn("scene targets: none at %dx%d, post-processing off", width, height);
		ReleaseTargets();
		return false;
	}
	const bgfx::TextureHandle scene[] = {color_, depth_};
	fb_ = bgfx::createFrameBuffer(2, scene, true);
	halfFb_ = bgfx::createFrameBuffer(1, &half_, true);
	if (!bgfx::isValid(fb_) || !bgfx::isValid(halfFb_)) {
		LogWarn("scene targets: no framebuffer at %dx%d, post-processing off", width, height);
		ReleaseTargets();
		return false;
	}
	width_ = width;
	height_ = height;
	LogInfo("scene targets: %dx%d msaa x%d, half %dx%d", width, height, msaa_, halfW_, halfH_);
	return true;
}

void SceneTargets::SetMsaa(int samples) {
	samples = samples < 2 ? 0 : samples;
	if (samples == msaa_) return;
	msaa_ = samples;
	ReleaseTargets(); // rebuilt at the next BeginFrame
}

void SceneTargets::BeginFrame(int width, int height, bool enabled, bgfx::ViewId skyView,
		bgfx::ViewId worldView) {
	active_ = false;
	if (enabled && ready() && width > 0 && height > 0) {
		if (width != width_ || height != height_) {
			if (!BuildTargets(width, height)) enabled = false;
		}
		active_ = enabled;
	}
	bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
	if (active_) target = fb_;
	bgfx::setViewFrameBuffer(skyView, target);
	bgfx::setViewFrameBuffer(worldView, target);
}

// At half size the bilinear fetch sits on the corner of four scene pixels,
// so this is an exact 2x2 average.
void SceneTargets::Downsample(bgfx::ViewId halfView) {
	if (!active_) return;
	bgfx::setViewFrameBuffer(halfView, halfFb_);
	bgfx::setTexture(0, sScene_, color_);
	FullScreenTriangle(halfView, layout_, halfW_, halfH_);
	bgfx::submit(halfView, copy_);
}

} // namespace painful
