#pragma once
#include <string>

struct SDL_Window;

namespace painful {

// Keys the engine currently cares about. Kept as our own enum so SDL types stay
// out of headers and the rest of the engine never includes SDL.
enum class Key { Forward, Back, Left, Right, Up, Down, Fast, ScaleUp, ScaleDown };

// Thin SDL3 wrapper: owns the window, the event pump and input state, nothing
// more. The renderer stays independent of the windowing library.
class Window {
public:
    ~Window() { Close(); }

    bool Open(const std::string& title, int width, int height);
    void Close();

    // Processes pending events. Returns false once the user asks to quit.
    bool PumpEvents();

    bool IsDown(Key key) const;
    // Level cycling: returns -1, 0 or +1 and clears itself. Edge triggered, so
    // holding the key does not scroll through levels.
    int TakeLevelStep();

    // Mouse movement since the previous call, in pixels.
    void TakeMouseDelta(float& dx, float& dy);
    // While captured the cursor is hidden and movement is unbounded.
    void SetMouseCaptured(bool captured);
    bool mouseCaptured() const { return mouseCaptured_; }

    // Platform window handle, for handing to the graphics backend.
    void* NativeHandle() const;

    int  width() const { return width_; }
    int  height() const { return height_; }
    // True for one poll after the window changed size.
    bool TakeResized();

private:
    SDL_Window* window_ = nullptr;
    int width_ = 0, height_ = 0;
    bool resized_ = false;
    bool mouseCaptured_ = false;
    float mouseDx_ = 0.f, mouseDy_ = 0.f;
    int levelStep_ = 0;
};

} // namespace painful
