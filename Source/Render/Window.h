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
    // True once per press of the noclip key, which lets the camera leave the
    // level again after physics started holding it inside.
    bool TakeNoclipToggle();
    // True once per press of the collision-wireframe key.
    bool TakePhysicsDebugToggle();

    // Windows VIRTUAL-KEY state, which is the space the scripts speak:
    // Definitions.lua's `Keys` table is the standard VK list. 256 entries,
    // indexed by code, mouse buttons included at 1/2/4. Edge detection is
    // not here - Input owns that, because INP.Key's tri-state is a script
    // contract rather than a windowing one.
    const bool* VirtualKeys() const { return vkDown_; }
    // Wheel notches since the last call: positive forward, negative back.
    // The wheel has no held state, so it reaches the scripts as the
    // synthetic keys 253/254 pulsed for a single frame.
    int TakeWheelSteps();

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
    bool noclipToggle_ = false;
    bool physicsDebugToggle_ = false;
    bool vkDown_[256] = {};
    int wheelSteps_ = 0;
};

} // namespace painful
