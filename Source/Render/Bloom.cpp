#include "Bloom.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cmath>

namespace painful {

namespace {

struct PostVertex {
	float x, y, z;
	float u, v;
};

// The original's kernel: seven Gaussian samples, sigma 4 in tap units, one
// tap every 1.25 texels of the half-size buffer (FUN_100a8f60, FUN_100a8860).
constexpr float kSigma = 4.f;
constexpr float kTapTexels = 1.25f;
constexpr int kOriginalPairs = 7;
// Three sigma, for the kernel that does not stop at one and a half.
constexpr int kFullPairs = 13;

float Gauss(int i) {
	return std::exp(-float(i * i) / (2.f * kSigma * kSigma)) /
			std::sqrt(kSigma * kSigma * 2.f * 3.14159265f);
}

} // namespace

bool Bloom::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle fsBright = LoadShader(shaderDir, "fs_bloom_bright");
	bgfx::ShaderHandle fsBlur = LoadShader(shaderDir, "fs_bloom_blur");
	bgfx::ShaderHandle fsComposite = LoadShader(shaderDir, "fs_bloom_composite");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fsBright) || !bgfx::isValid(fsBlur) ||
			!bgfx::isValid(fsComposite)) {
		LogWarn("bloom: shaders missing, off");
		return false;
	}
	// One vertex shader, three programs: bgfx frees a shader with the last
	// program that holds it, so only the last createProgram gets the flag.
	bright_ = bgfx::createProgram(vs, fsBright, false);
	blur_ = bgfx::createProgram(vs, fsBlur, false);
	composite_ = bgfx::createProgram(vs, fsComposite, true);
	bgfx::destroy(fsBright);
	bgfx::destroy(fsBlur);

	sScene_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
	sBloom_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
	uParams_ = bgfx::createUniform("u_bloomParams", bgfx::UniformType::Vec4);
	uDir_ = bgfx::createUniform("u_bloomDir", bgfx::UniformType::Vec4);
	uKernel_ = bgfx::createUniform("u_bloomKernel", bgfx::UniformType::Vec4, kMaxPairs);
	uOverlay_ = bgfx::createUniform("u_bloomOverlay", bgfx::UniformType::Vec4);

	layout_.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	return true;
}

void Bloom::Shutdown() {
	ReleaseTargets();
	for (bgfx::ProgramHandle* p : {&bright_, &blur_, &composite_}) {
		if (bgfx::isValid(*p)) bgfx::destroy(*p);
		*p = BGFX_INVALID_HANDLE;
	}
	for (bgfx::UniformHandle* u : {&sScene_, &sBloom_, &uParams_, &uDir_, &uKernel_, &uOverlay_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	active_ = false;
}

void Bloom::ReleaseTargets() {
	// The framebuffers own their textures (createFrameBuffer's destroy flag).
	if (bgfx::isValid(sceneFb_)) bgfx::destroy(sceneFb_);
	sceneFb_ = BGFX_INVALID_HANDLE;
	sceneColor_ = sceneDepth_ = BGFX_INVALID_HANDLE;
	for (int i = 0; i < 2; ++i) {
		if (bgfx::isValid(fb_[i])) bgfx::destroy(fb_[i]);
		fb_[i] = BGFX_INVALID_HANDLE;
		color_[i] = BGFX_INVALID_HANDLE;
	}
	width_ = height_ = bufW_ = bufH_ = 0;
}

bool Bloom::BuildTargets(int width, int height) {
	ReleaseTargets();
	const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	sceneColor_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
			bgfx::TextureFormat::RGBA8, colorFlags);
	const bgfx::TextureFormat::Enum depthFormats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	for (bgfx::TextureFormat::Enum f : depthFormats) {
		if (bgfx::isValid(sceneDepth_)) break;
		if (!bgfx::isTextureValid(0, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY)) continue;
		sceneDepth_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, f,
				BGFX_TEXTURE_RT_WRITE_ONLY);
	}
	if (!bgfx::isValid(sceneColor_) || !bgfx::isValid(sceneDepth_)) {
		LogWarn("bloom: no scene target at %dx%d, off", width, height);
		ReleaseTargets();
		return false;
	}
	const bgfx::TextureHandle scene[] = {sceneColor_, sceneDepth_};
	sceneFb_ = bgfx::createFrameBuffer(2, scene, true);

	const int scale = std::max(1, scale_);
	bufW_ = std::max(1, width / scale);
	bufH_ = std::max(1, height / scale);
	for (int i = 0; i < 2; ++i) {
		color_[i] = bgfx::createTexture2D(uint16_t(bufW_), uint16_t(bufH_), false, 1,
				bgfx::TextureFormat::RGBA8, colorFlags);
		if (bgfx::isValid(color_[i])) fb_[i] = bgfx::createFrameBuffer(1, &color_[i], true);
	}
	if (!bgfx::isValid(sceneFb_) || !bgfx::isValid(fb_[0]) || !bgfx::isValid(fb_[1])) {
		LogWarn("bloom: no blur targets at %dx%d, off", bufW_, bufH_);
		ReleaseTargets();
		return false;
	}
	width_ = width;
	height_ = height;
	if (kernelDirty_) BuildKernel();
	LogInfo("bloom: scene %dx%d, blur buffers %dx%d (1/%d), %d taps", width, height, bufW_, bufH_,
			scale, taps());
	return true;
}

void Bloom::SetParams(float threshold, float multiplier, uint32_t overlayArgb) {
	threshold_ = threshold;
	if (multiplier != multiplier_) kernelDirty_ = true;
	multiplier_ = multiplier;
	overlay_[0] = float((overlayArgb >> 16) & 0xff) / 255.f;
	overlay_[1] = float((overlayArgb >> 8) & 0xff) / 255.f;
	overlay_[2] = float(overlayArgb & 0xff) / 255.f;
}

void Bloom::SetQuality(int scale, int kernel) {
	scale = std::max(1, std::min(scale, 8));
	kernel = kernel == 1 ? 1 : 0;
	if (scale != scale_) ReleaseTargets(); // rebuilt at the next BeginFrame
	if (kernel != kernelMode_) kernelDirty_ = true;
	scale_ = scale;
	kernelMode_ = kernel;
}

// Weights the way FUN_100a8f60 makes them, Multiplier included - it is applied
// in BOTH passes, so a level's 1.5 is 2.25 on the result. The extended kernel
// is scaled to the original's total so the intensity does not move with it.
void Bloom::BuildKernel() {
	kernelDirty_ = false;
	pairs_ = kernelMode_ == 1 ? kOriginalPairs : kFullPairs;
	float originalSum = Gauss(0), sum = Gauss(0);
	for (int i = 1; i < kOriginalPairs; ++i) originalSum += 2.f * Gauss(i);
	for (int i = 1; i < pairs_; ++i) sum += 2.f * Gauss(i);
	const float norm = multiplier_ * originalSum / sum;
	for (int i = 0; i < kMaxPairs; ++i) {
		kernel_[i][0] = kTapTexels * float(i);
		kernel_[i][1] = i < pairs_ ? Gauss(i) * norm : 0.f;
		kernel_[i][2] = kernel_[i][3] = 0.f;
	}
}

void Bloom::BeginFrame(int width, int height, bool enabled, bgfx::ViewId skyView,
		bgfx::ViewId worldView) {
	active_ = false;
	if (enabled && ready() && width > 0 && height > 0) {
		if (width != width_ || height != height_) {
			if (!BuildTargets(width, height)) enabled = false;
		}
		active_ = enabled;
	}
	bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
	if (active_) target = sceneFb_;
	bgfx::setViewFrameBuffer(skyView, target);
	bgfx::setViewFrameBuffer(worldView, target);
}

void Bloom::FullScreenTriangle(bgfx::ViewId view, int width, int height) {
	bgfx::setViewRect(view, 0, 0, uint16_t(width), uint16_t(height));
	bgfx::setViewClear(view, BGFX_CLEAR_NONE);
	bgfx::setViewTransform(view, nullptr, nullptr);
	if (bgfx::getAvailTransientVertexBuffer(3, layout_) < 3) return;
	bgfx::TransientVertexBuffer tvb;
	bgfx::allocTransientVertexBuffer(&tvb, 3, layout_);
	// One triangle past the corners; v runs the way the backend stores a
	// render target, top row first on D3D, bottom row first on GL.
	const bool flip = bgfx::getCaps()->originBottomLeft;
	PostVertex* v = reinterpret_cast<PostVertex*>(tvb.data);
	v[0] = {-1.f, -1.f, 0.f, 0.f, flip ? 0.f : 1.f};
	v[1] = {3.f, -1.f, 0.f, 2.f, flip ? 0.f : 1.f};
	v[2] = {-1.f, 3.f, 0.f, 0.f, flip ? 2.f : -1.f};
	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

void Bloom::Draw(bgfx::ViewId brightView, bgfx::ViewId blurHView, bgfx::ViewId blurVView,
		bgfx::ViewId compositeView) {
	if (!active_) return;
	if (kernelDirty_) BuildKernel();

	// Bright pass: the scene, 1/scale sized. At 2 the bilinear fetch sits on
	// the corner of four scene pixels, so this is an exact 2x2 average.
	const float params[4] = {threshold_, 0.f, 0.f, 0.f};
	bgfx::setViewFrameBuffer(brightView, fb_[0]);
	bgfx::setTexture(0, sScene_, sceneColor_);
	bgfx::setUniform(uParams_, params);
	FullScreenTriangle(brightView, bufW_, bufH_);
	bgfx::submit(brightView, bright_);

	// Horizontal into the second buffer, vertical back into the first.
	const float dirH[4] = {1.f / float(bufW_), 0.f, float(pairs_), 0.f};
	bgfx::setViewFrameBuffer(blurHView, fb_[1]);
	bgfx::setTexture(0, sScene_, color_[0]);
	bgfx::setUniform(uDir_, dirH);
	bgfx::setUniform(uKernel_, kernel_, uint16_t(kMaxPairs));
	FullScreenTriangle(blurHView, bufW_, bufH_);
	bgfx::submit(blurHView, blur_);

	const float dirV[4] = {0.f, 1.f / float(bufH_), float(pairs_), 0.f};
	bgfx::setViewFrameBuffer(blurVView, fb_[0]);
	bgfx::setTexture(0, sScene_, color_[1]);
	bgfx::setUniform(uDir_, dirV);
	bgfx::setUniform(uKernel_, kernel_, uint16_t(kMaxPairs));
	FullScreenTriangle(blurVView, bufW_, bufH_);
	bgfx::submit(blurVView, blur_);

	// Composite onto the backbuffer, where the HUD then draws.
	bgfx::setViewFrameBuffer(compositeView, BGFX_INVALID_HANDLE);
	bgfx::setTexture(0, sScene_, sceneColor_);
	bgfx::setTexture(1, sBloom_, color_[0]);
	bgfx::setUniform(uOverlay_, overlay_);
	FullScreenTriangle(compositeView, width_, height_);
	bgfx::submit(compositeView, composite_);
}

} // namespace painful
