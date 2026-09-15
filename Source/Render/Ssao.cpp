#include "Ssao.h"
#include "Camera.h"
#include "FullScreenPass.h"
#include "SceneTargets.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <bx/math.h>

namespace painful {

namespace {

constexpr uint64_t kPointClamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr uint64_t kLinearClamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

bgfx::ProgramHandle Program(const std::string& dir, const char* vs, const char* fs) {
	bgfx::ShaderHandle v = LoadShader(dir, vs);
	bgfx::ShaderHandle f = LoadShader(dir, fs);
	if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
		if (bgfx::isValid(v)) bgfx::destroy(v);
		if (bgfx::isValid(f)) bgfx::destroy(f);
		return BGFX_INVALID_HANDLE;
	}
	return bgfx::createProgram(v, f, true);
}

} // namespace

bool Ssao::Init(const std::string& shaderDir) {
	ao_ = Program(shaderDir, "vs_post", "fs_ssao");
	aoMs_ = Program(shaderDir, "vs_post", "fs_ssao_ms");
	blur_ = Program(shaderDir, "vs_post", "fs_ssao_blur");
	apply_ = Program(shaderDir, "vs_post", "fs_ssao_apply");
	sDepth_ = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
	sAo_ = bgfx::createUniform("s_ao", bgfx::UniformType::Sampler);
	uInvProj_ = bgfx::createUniform("u_ssaoInvProj", bgfx::UniformType::Mat4);
	uScreen_ = bgfx::createUniform("u_ssaoScreen", bgfx::UniformType::Vec4);
	uParams_ = bgfx::createUniform("u_ssaoParams", bgfx::UniformType::Vec4);
	uBlur_ = bgfx::createUniform("u_ssaoBlur", bgfx::UniformType::Vec4);
	layout_ = PostVertexLayout();
	const bool ok = bgfx::isValid(ao_) && bgfx::isValid(blur_) && bgfx::isValid(apply_);
	if (!ok) {
		LogWarn("ssao: shaders missing, SSAO off");
		Shutdown();
	}
	return ok;
}

void Ssao::Shutdown() {
	ReleaseTargets();
	for (bgfx::ProgramHandle* p : {&ao_, &aoMs_, &blur_, &apply_}) {
		if (bgfx::isValid(*p)) bgfx::destroy(*p);
		*p = BGFX_INVALID_HANDLE;
	}
	for (bgfx::UniformHandle* u : {&sDepth_, &sAo_, &uInvProj_, &uScreen_, &uParams_, &uBlur_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	active_ = false;
}

void Ssao::ReleaseTargets() {
	for (int i = 0; i < 2; ++i) {
		if (bgfx::isValid(fb_[i])) bgfx::destroy(fb_[i]);
		fb_[i] = BGFX_INVALID_HANDLE;
		tex_[i] = BGFX_INVALID_HANDLE;
	}
	bufW_ = bufH_ = 0;
}

bool Ssao::BuildTargets(int width, int height) {
	ReleaseTargets();
	for (int i = 0; i < 2; ++i) {
		tex_[i] = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::RGBA16F,
				BGFX_TEXTURE_RT | kLinearClamp);
		if (bgfx::isValid(tex_[i])) fb_[i] = bgfx::createFrameBuffer(1, &tex_[i], true);
		if (!bgfx::isValid(fb_[i])) {
			if (bgfx::isValid(tex_[i])) bgfx::destroy(tex_[i]);
			tex_[i] = BGFX_INVALID_HANDLE;
			LogWarn("ssao: no target at %dx%d, SSAO off", width, height);
			ReleaseTargets();
			return false;
		}
	}
	bufW_ = width;
	bufH_ = height;
	LogInfo("ssao: %dx%d buffers", width, height);
	return true;
}

void Ssao::SetParams(float radius, float strength) {
	radius_ = radius > 0.001f ? radius : 0.001f;
	strength_ = strength;
}

void Ssao::Draw(const SceneTargets& scene, const Camera& camera, bgfx::ViewId aoView, bgfx::ViewId blurHView,
		bgfx::ViewId blurVView, bgfx::ViewId applyView) {
	active_ = false;
	if (!ready() || !scene.active() || !scene.depthReadable() || strength_ <= 0.f) return;
	const bool multisampled = scene.msaa() >= 2;
	if (multisampled && !bgfx::isValid(aoMs_)) return;
	const int w = scene.width(), h = scene.height(), hw = scene.halfWidth(), hh = scene.halfHeight();
	if ((hw != bufW_ || hh != bufH_) && !BuildTargets(hw, hh)) return;

	float view[16], proj[16], inverse[16];
	camera.ViewProj(w, h, camera.farPlane, view, proj);
	bx::mtxInverse(inverse, proj);
	const bgfx::Caps* caps = bgfx::getCaps();
	const float screen[4] = {float(w), float(h), caps->originBottomLeft ? 1.f : 0.f,
			caps->homogeneousDepth ? 1.f : 0.f};
	const float params[4] = {radius_, strength_, proj[5], 0.f};

	// The occlusion, at half size from the full-size depth.
	bgfx::setViewFrameBuffer(aoView, fb_[0]);
	FullScreenTriangle(aoView, layout_, hw, hh);
	bgfx::setUniform(uInvProj_, inverse);
	bgfx::setUniform(uScreen_, screen);
	bgfx::setUniform(uParams_, params);
	bgfx::setTexture(0, sDepth_, scene.depth(), kPointClamp);
	bgfx::submit(aoView, multisampled ? aoMs_ : ao_);

	// Blurred across, then down, weighted by how near each tap's depth is.
	const float across[4] = {1.f / float(hw), 0.f, 0.f, 0.f};
	bgfx::setViewFrameBuffer(blurHView, fb_[1]);
	FullScreenTriangle(blurHView, layout_, hw, hh);
	bgfx::setUniform(uBlur_, across);
	bgfx::setTexture(0, sAo_, tex_[0], kPointClamp);
	bgfx::submit(blurHView, blur_);
	const float down[4] = {0.f, 1.f / float(hh), 0.f, 0.f};
	bgfx::setViewFrameBuffer(blurVView, fb_[0]);
	FullScreenTriangle(blurVView, layout_, hw, hh);
	bgfx::setUniform(uBlur_, down);
	bgfx::setTexture(0, sAo_, tex_[1], kPointClamp);
	bgfx::submit(blurVView, blur_);

	// Multiplied over the scene's colour.
	bgfx::setViewFrameBuffer(applyView, scene.colorFramebuffer());
	FullScreenTriangle(applyView, layout_, w, h);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR));
	bgfx::setUniform(uParams_, params);
	bgfx::setTexture(0, sAo_, tex_[0], kLinearClamp);
	bgfx::submit(applyView, apply_);
	active_ = true;
}

} // namespace painful
