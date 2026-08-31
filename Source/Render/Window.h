#pragma once
#include <string>
#include <vector>

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
    // True once per press of F1..F4, the in-game debug modes. Indexed 0..3
    // so the game loop can keep its own modes without the window knowing what
    // any of them mean.
    bool TakeDebugToggle(int index);

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
    bool hasFocus() const { return hasFocus_; }
    // Fullscreen modes the display supports, as "WIDTHxHEIGHT", widest first
    // and de-duplicated across refresh rates.
    std::vector<std::string> DisplayModes() const;

    // The cursor in window pixels, tracked whether or not the mouse is
    // captured - the menu needs it, the camera does not.
    float mouseX() const { return mouseX_; }
    float mouseY() const { return mouseY_; }
    // While the menu is up the cursor must stay visible, so capture is
    // suppressed rather than toggled on every click.
    void SetSystemCursorVisible(bool visible);
    void SetAllowCapture(bool on) {
        allowCapture_ = on;
        if (!on && mouseCaptured_) SetMouseCaptured(false);
    }
    // Hands Escape to the caller instead of quitting on it: the game loop
    // gives that key to the menu.
    void SetEscapeQuits(bool on) { escapeQuits_ = on; }
    bool TakeEscape() {
        const bool e = escapePressed_;
        escapePressed_ = false;
        return e;
    }
    // Left-button edge since the last call, for menu clicks.
    bool TakeLeftClick() {
        const bool c = leftClicked_;
        leftClicked_ = false;
        return c;
    }

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
    float mouseX_ = 0.f, mouseY_ = 0.f;
    bool leftClicked_ = false;
    bool allowCapture_ = true;
    bool escapePressed_ = false;
    bool escapeQuits_ = true;
    bool systemCursorVisible_ = true;
    bool hasFocus_ = true;
    float mouseDx_ = 0.f, mouseDy_ = 0.f;
    int levelStep_ = 0;
    bool noclipToggle_ = false;
    bool physicsDebugToggle_ = false;
    bool debugToggles_[4] = {false, false, false, false};   // F1..F4
    bool vkDown_[256] = {};
    int wheelSteps_ = 0;
};

} // namespace painful
