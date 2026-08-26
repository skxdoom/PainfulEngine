#include "Renderer.h"
#include "Window.h"
#include "../Core/Log.h"

#include <bgfx/bgfx.h>
#include <cstdarg>

namespace painful {

bool Renderer::Init(Window& window) {
    void* nwh = window.NativeHandle();
    if (!nwh) {
        LogWarn("no native window handle");
        return false;
    }

    // Calling renderFrame() before init() keeps bgfx single-threaded, which
    // makes debugging far simpler and costs nothing at this scale.
    bgfx::renderFrame();

    bgfx::Init init;
    init.type = bgfx::RendererType::Count;   // let bgfx pick (D3D11 on Windows)
    init.platformData.nwh = nwh;
    init.resolution.width = static_cast<uint32_t>(window.width());
    init.resolution.height = static_cast<uint32_t>(window.height());
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        LogWarn("bgfx::init failed");
        return false;
    }

    width_ = window.width();
    height_ = window.height();
    initialised_ = true;

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    // View 0 draws the sky and owns the clear; view 1 draws the world on top.
    bgfx::setViewClear(kSkyView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a20ff, 1.0f, 0);
    // (SetClearColor overrides this per level.)| BGFX_CLEAR_DEPTH, 0x1a1a20ff, 1.0f, 0);
    bgfx::setViewClear(kWorldView, BGFX_CLEAR_NONE);
    // Sky layers must composite in order, so stop bgfx sorting that view.
    bgfx::setViewMode(kSkyView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(0, 0, 0, uint16_t(width_), uint16_t(height_));
    return true;
}

void Renderer::Shutdown() {
    if (!initialised_) return;
    bgfx::shutdown();
    initialised_ = false;
}

void Renderer::Resize(int width, int height) {
    if (!initialised_ || width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    bgfx::reset(uint32_t(width), uint32_t(height), BGFX_RESET_VSYNC);
    bgfx::setViewRect(kSkyView, 0, 0, uint16_t(width), uint16_t(height));
    bgfx::setViewRect(kWorldView, 0, 0, uint16_t(width), uint16_t(height));
}

void Renderer::BeginFrame() {
    if (!initialised_) return;
    bgfx::setViewRect(kSkyView, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::setViewRect(kWorldView, 0, 0, uint16_t(width_), uint16_t(height_));
    // touch() guarantees the view is processed even with nothing submitted.
    bgfx::touch(kSkyView);
    bgfx::dbgTextClear();
}

void Renderer::EndFrame() {
    if (initialised_) bgfx::frame();
}

void Renderer::DebugText(uint16_t row, const char* fmt, ...) {
    if (!initialised_) return;
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    bgfx::dbgTextPrintf(1, row, 0x0f, "%s", buffer);
}

void Renderer::RequestScreenshot(const std::string& path) {
    if (initialised_) bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
}

std::string Renderer::BackendName() const {
    if (!initialised_) return "none";
    return bgfx::getRendererName(bgfx::getRendererType());
}

void Renderer::SetClearColor(float r, float g, float b) {
    const uint32_t rgba = (uint32_t(r * 255.f) << 24) | (uint32_t(g * 255.f) << 16) |
                          (uint32_t(b * 255.f) << 8) | 0xff;
    bgfx::setViewClear(kSkyView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0f, 0);
}

} // namespace painful
