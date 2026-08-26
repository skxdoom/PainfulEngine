#include "Renderer.h"
#include "Window.h"
#include "../Core/Log.h"

#include <bgfx/bgfx.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace painful {

namespace {

// bgfx::requestScreenShot only hands the pixels to the callback interface; with
// no interface installed the default stub drops them, which is why --shot wrote
// nothing. This writes an uncompressed 32-bit TGA, which Tools/shot.ps1 already
// knows how to convert.
struct ScreenShotCallback : public bgfx::CallbackI {
    virtual ~ScreenShotCallback() {}
    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* str) override {
        LogWarn("bgfx fatal: %s", str);
    }
    void traceVargs(const char*, uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}

    void screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum format, const void* data, uint32_t,
                    bool yflip) override {
        FILE* f = fopen(filePath, "wb");
        if (!f) { LogWarn("screenshot: cannot open %s", filePath); return; }
        uint8_t header[18] = {};
        header[2] = 2;                                   // uncompressed true-colour
        header[12] = uint8_t(width & 0xff);
        header[13] = uint8_t((width >> 8) & 0xff);
        header[14] = uint8_t(height & 0xff);
        header[15] = uint8_t((height >> 8) & 0xff);
        header[16] = 32;                                 // bits per pixel
        header[17] = 0x20;                               // rows top-to-bottom
        fwrite(header, 1, sizeof(header), f);
        // TGA stores BGRA. D3D11 hands back BGRA8 already; the GL backends give
        // RGBA8, so those need the two swapped.
        const bool swapRB = format == bgfx::TextureFormat::RGBA8;
        const uint8_t* src = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> row(size_t(width) * 4);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t sy = yflip ? (height - 1 - y) : y;
            std::memcpy(row.data(), src + size_t(sy) * pitch, row.size());
            if (swapRB)
                for (size_t x = 0; x < row.size(); x += 4) std::swap(row[x], row[x + 2]);
            fwrite(row.data(), 1, row.size(), f);
        }
        fclose(f);
        LogInfo("screenshot written: %s (%ux%u)", filePath, width, height);
    }
};

ScreenShotCallback g_screenShotCallback;

}  // namespace

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
    init.callback = &g_screenShotCallback;
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
