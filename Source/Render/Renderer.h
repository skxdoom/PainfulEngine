#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace painful {

class Window;

// bgfx front end. The renderer owns backend setup and the frame loop; nothing
// above it needs to know which graphics API is in use.
class Renderer {
public:
    // Sky is drawn first and owns the clear; the world paints over it.
    static constexpr bgfx::ViewId kSkyView = 0;
    static constexpr bgfx::ViewId kWorldView = 1;

    ~Renderer() { Shutdown(); }

    bool Init(Window& window);
    void Shutdown();

    // Called when the window size changes.
    void Resize(int width, int height);

    void BeginFrame();
    void EndFrame();

    // Debug overlay text, one line per call, starting at the given row.
    void DebugText(uint16_t row, const char* fmt, ...);
    // Asks bgfx to save the next frame to disk (written as a TGA).
    void RequestScreenshot(const std::string& path);


    // Human-readable name of the backend bgfx actually selected.
    std::string BackendName() const;

private:
    bool  initialised_ = false;
    int   width_ = 0, height_ = 0;
};

} // namespace painful
