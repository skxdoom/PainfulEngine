#include "FrameCompose.h"
#include "FullScreenPass.h"
#include "SceneTargets.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

namespace painful {

bool FrameCompose::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_compose");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		if (bgfx::isValid(vs)) bgfx::destroy(vs);
		if (bgfx::isValid(fs)) bgfx::destroy(fs);
		LogWarn("frame compose: shaders missing, off");
		return false;
	}
	program_ = bgfx::createProgram(vs, fs, true);
	const char* names[kTextures] = {"s_tex0", "s_tex1", "s_tex2"};
	for (int i = 0; i < kTextures; ++i)
		sTex_[i] = bgfx::createUniform(names[i], bgfx::UniformType::Sampler);
	uWeights_ = bgfx::createUniform("u_composeWeights", bgfx::UniformType::Vec4);
	uShift_ = bgfx::createUniform("u_composeShift", bgfx::UniformType::Vec4, 2);
	return ready();
}

void FrameCompose::Shutdown() {
	ReleaseTargets();
	for (bgfx::UniformHandle& u : sTex_) {
		if (bgfx::isValid(u)) bgfx::destroy(u);
		u = BGFX_INVALID_HANDLE;
	}
	for (bgfx::UniformHandle* u : {&uWeights_, &uShift_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
}

void FrameCompose::ReleaseTargets() {
	for (int i = 0; i < kTextures; ++i) {
		if (bgfx::isValid(fb_[i])) bgfx::destroy(fb_[i]);
		fb_[i] = BGFX_INVALID_HANDLE;
		tex_[i] = BGFX_INVALID_HANDLE;
		written_[i] = false;
	}
	width_ = height_ = 0;
}

bool FrameCompose::BuildTargets(const SceneTargets& scene) {
	ReleaseTargets();
	const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	for (int i = 0; i < kTextures; ++i) {
		tex_[i] = bgfx::createTexture2D(uint16_t(scene.halfWidth()), uint16_t(scene.halfHeight()),
				false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | clamp);
		if (bgfx::isValid(tex_[i])) fb_[i] = bgfx::createFrameBuffer(1, &tex_[i], true);
		if (!bgfx::isValid(fb_[i])) {
			LogWarn("frame compose: no targets at %dx%d", scene.halfWidth(), scene.halfHeight());
			ReleaseTargets();
			return false;
		}
	}
	width_ = scene.halfWidth();
	height_ = scene.halfHeight();
	return true;
}

bool FrameCompose::Draw(const SceneTargets& scene, int target, const int* textures,
		const float* weights, int count, const float (*shift)[2], bgfx::ViewId copyView,
		bgfx::ViewId composeView) {
	if (!ready() || !scene.active() || target < 1 || target > kTextures) return false;

	// Shifted: every stage is this frame, clamped at the edge it is moved off.
	if (shift) {
		float w[4] = {0.f, 0.f, 0.f, 0.f};
		float s[8] = {};
		const uint32_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
		for (int i = 0; i < kTextures; ++i) {
			if (i < count && textures[i] >= 1 && textures[i] <= kTextures) {
				w[i] = weights[i];
				s[i * 2 + 0] = shift[textures[i] - 1][0];
				s[i * 2 + 1] = shift[textures[i] - 1][1];
			}
			bgfx::setTexture(uint8_t(i), sTex_[i], scene.half(), clamp);
		}
		bgfx::setUniform(uWeights_, w);
		bgfx::setUniform(uShift_, s, 2);
		bgfx::setViewFrameBuffer(composeView, BGFX_INVALID_HANDLE);
		FullScreenTriangle(composeView, scene.layout(), scene.width(), scene.height());
		bgfx::submit(composeView, program_);
		return true;
	}

	if ((scene.halfWidth() != width_ || scene.halfHeight() != height_) && !BuildTargets(scene))
		return false;

	// RenderWorldToTexture: this frame into the target.
	const int into = target - 1;
	bgfx::setViewFrameBuffer(copyView, fb_[into]);
	bgfx::setTexture(0, scene.sceneSampler(), scene.half());
	FullScreenTriangle(copyView, scene.layout(), width_, height_);
	bgfx::submit(copyView, scene.copyProgram());
	written_[into] = true;

	// ComposeTextures: the weights past `count` are 0, and a texture with
	// nothing in it yet stands in as this frame.
	float w[4] = {0.f, 0.f, 0.f, 0.f};
	for (int i = 0; i < kTextures; ++i) {
		int k = into;
		if (i < count && textures[i] >= 1 && textures[i] <= kTextures) {
			w[i] = weights[i];
			if (written_[textures[i] - 1]) k = textures[i] - 1;
		}
		bgfx::setTexture(uint8_t(i), sTex_[i], tex_[k]);
	}
	bgfx::setUniform(uWeights_, w);
	const float noShift[8] = {};
	bgfx::setUniform(uShift_, noShift, 2);
	bgfx::setViewFrameBuffer(composeView, BGFX_INVALID_HANDLE);
	FullScreenTriangle(composeView, scene.layout(), scene.width(), scene.height());
	bgfx::submit(composeView, program_);
	return true;
}

} // namespace painful
