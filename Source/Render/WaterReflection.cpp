#include "WaterReflection.h"
#include "../Core/Log.h"
#include <algorithm>

namespace painful {

void WaterReflection::Shutdown() {
	if (bgfx::isValid(fb_)) bgfx::destroy(fb_);
	fb_ = BGFX_INVALID_HANDLE;
	color_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	width_ = height_ = 0;
}

bool WaterReflection::Build(int width, int height) {
	Shutdown();
	const uint16_t w = uint16_t(std::max(1, width / 2)), h = uint16_t(std::max(1, height / 2));
	const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	color_ = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_RT | clamp);
	const bgfx::TextureFormat::Enum depthFormats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	for (bgfx::TextureFormat::Enum f : depthFormats) {
		if (bgfx::isValid(depth_)) break;
		if (!bgfx::isTextureValid(0, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY)) continue;
		depth_ = bgfx::createTexture2D(w, h, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY);
	}
	if (!bgfx::isValid(color_) || !bgfx::isValid(depth_)) {
		LogWarn("water reflection: no target at %dx%d", w, h);
		Shutdown();
		return false;
	}
	const bgfx::TextureHandle both[] = {color_, depth_};
	fb_ = bgfx::createFrameBuffer(2, both, true);
	if (!bgfx::isValid(fb_)) {
		LogWarn("water reflection: no framebuffer at %dx%d", w, h);
		Shutdown();
		return false;
	}
	width_ = width;
	height_ = height;
	LogInfo("water reflection: %dx%d target", w, h);
	return true;
}

bool WaterReflection::Begin(int width, int height, bgfx::ViewId skyView, bgfx::ViewId worldView) {
	if ((width != width_ || height != height_ || !ready()) && !Build(width, height)) return false;
	const uint16_t w = uint16_t(std::max(1, width / 2)), h = uint16_t(std::max(1, height / 2));
	bgfx::setViewFrameBuffer(skyView, fb_);
	bgfx::setViewFrameBuffer(worldView, fb_);
	bgfx::setViewRect(skyView, 0, 0, w, h);
	bgfx::setViewRect(worldView, 0, 0, w, h);
	bgfx::setViewClear(skyView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
	if (worldView != skyView) bgfx::setViewClear(worldView, BGFX_CLEAR_NONE);
	bgfx::setViewMode(skyView, bgfx::ViewMode::Sequential);
	bgfx::touch(skyView);
	return true;
}

} // namespace painful
