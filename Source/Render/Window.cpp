#include "Window.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "../Core/Log.h"
#include <SDL3/SDL.h>

namespace painful {

namespace {
SDL_Scancode ScancodeFor(Key key) {
    switch (key) {
    case Key::Forward: return SDL_SCANCODE_W;
    case Key::Back:    return SDL_SCANCODE_S;
    case Key::Left:    return SDL_SCANCODE_A;
    case Key::Right:   return SDL_SCANCODE_D;
    case Key::Up:      return SDL_SCANCODE_SPACE;
    case Key::Down:    return SDL_SCANCODE_LCTRL;
    case Key::Fast:    return SDL_SCANCODE_LSHIFT;
    }
    return SDL_SCANCODE_UNKNOWN;
}

// SDL scancode -> Windows virtual-key code, the space Definitions.lua's
// `Keys` table uses. Translating from the SCANCODE rather than the keycode
// keeps WASD on the same three physical keys under any layout, which is what
// a player binding movement expects. The three codes with no Windows
// original - Numpad Enter 252, wheel 253/254 - are the engine's own.
int VirtualKeyFor(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return 112 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
        return 97 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_RETURN:       return 13;
    case SDL_SCANCODE_ESCAPE:       return 27;
    case SDL_SCANCODE_BACKSPACE:    return 8;
    case SDL_SCANCODE_TAB:          return 9;
    case SDL_SCANCODE_SPACE:        return 32;
    case SDL_SCANCODE_MINUS:        return 189;
    case SDL_SCANCODE_EQUALS:       return 187;
    case SDL_SCANCODE_LEFTBRACKET:  return 219;
    case SDL_SCANCODE_RIGHTBRACKET: return 221;
    case SDL_SCANCODE_BACKSLASH:    return 220;
    case SDL_SCANCODE_SEMICOLON:    return 186;
    case SDL_SCANCODE_APOSTROPHE:   return 222;
    case SDL_SCANCODE_GRAVE:        return 192;
    case SDL_SCANCODE_COMMA:        return 188;
    case SDL_SCANCODE_PERIOD:       return 190;
    case SDL_SCANCODE_SLASH:        return 191;
    case SDL_SCANCODE_CAPSLOCK:     return 20;
    case SDL_SCANCODE_PRINTSCREEN:  return 44;
    case SDL_SCANCODE_SCROLLLOCK:   return 145;
    case SDL_SCANCODE_PAUSE:        return 19;
    case SDL_SCANCODE_INSERT:       return 45;
    case SDL_SCANCODE_HOME:         return 36;
    case SDL_SCANCODE_PAGEUP:       return 33;
    case SDL_SCANCODE_DELETE:       return 46;
    case SDL_SCANCODE_END:          return 35;
    case SDL_SCANCODE_PAGEDOWN:     return 34;
    case SDL_SCANCODE_RIGHT:        return 39;
    case SDL_SCANCODE_LEFT:         return 37;
    case SDL_SCANCODE_DOWN:         return 40;
    case SDL_SCANCODE_UP:           return 38;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 144;
    case SDL_SCANCODE_KP_DIVIDE:    return 111;
    case SDL_SCANCODE_KP_MULTIPLY:  return 106;
    case SDL_SCANCODE_KP_MINUS:     return 109;
    case SDL_SCANCODE_KP_PLUS:      return 107;
    case SDL_SCANCODE_KP_ENTER:     return 252;
    case SDL_SCANCODE_KP_0:         return 96;
    case SDL_SCANCODE_KP_PERIOD:    return 110;
    case SDL_SCANCODE_LCTRL:        return 162;
    case SDL_SCANCODE_LSHIFT:       return 160;
    case SDL_SCANCODE_LALT:         return 164;
    case SDL_SCANCODE_LGUI:         return 91;
    case SDL_SCANCODE_RCTRL:        return 163;
    case SDL_SCANCODE_RSHIFT:       return 161;
    case SDL_SCANCODE_RALT:         return 165;
    case SDL_SCANCODE_RGUI:         return 92;
    default:                        return 0;
    }
}
} // namespace

bool Window::Open(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LogWarn("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // PAINFUL_HIDDEN keeps the window off the screen. Automated runs still
    // need a real window - bgfx wants a native handle and the frame has to be
    // rendered for a screenshot to mean anything - but a scripted test popping
    // a window onto the desktop, stealing focus and vanishing is noise when a
    // batch of them runs back to back.
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (const char* hidden = std::getenv("PAINFUL_HIDDEN"))
        if (*hidden && *hidden != '0') flags |= SDL_WINDOW_HIDDEN;

    window_ = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!window_) {
        LogWarn("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void Window::SetMode(int width, int height, bool fullscreen) {
    if (!window_ || width <= 0 || height <= 0) return;
    if (fullscreen) {
        SDL_DisplayMode mode;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        if (SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.f, false, &mode)) {
            SDL_SetWindowFullscreenMode(window_, &mode);
        } else {
            LogWarn("window: no fullscreen mode near %dx%d, using the desktop", width, height);
            SDL_SetWindowFullscreenMode(window_, nullptr);
        }
        SDL_SetWindowFullscreen(window_, true);
    } else {
        SDL_SetWindowFullscreen(window_, false);
        SDL_SetWindowSize(window_, width, height);
    }
    LogInfo("window: %dx%d %s", width, height, fullscreen ? "fullscreen" : "windowed");
}

void Window::Close() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

std::vector<std::string> Window::DisplayModes() const {
    std::vector<std::string> out;
    int count = 0;
    SDL_DisplayID display = window_ ? SDL_GetDisplayForWindow(window_)
                                    : SDL_GetPrimaryDisplay();
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes) return out;
    for (int i = 0; i < count; ++i) {
        char buf[32];
        snprintf(buf, sizeof buf, "%dx%d", modes[i]->w, modes[i]->h);
        // One entry per SIZE: the display reports a mode per refresh rate, and
        // the menu offers sizes.
        if (std::find(out.begin(), out.end(), buf) == out.end()) out.push_back(buf);
    }
    SDL_free(modes);
    return out;
}

bool Window::PumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            return false;
        case SDL_EVENT_KEY_UP:
            if (const int vk = VirtualKeyFor(e.key.scancode)) vkDown_[vk] = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (const int vk = VirtualKeyFor(e.key.scancode)) vkDown_[vk] = true;
            if (e.key.key == SDLK_LEFTBRACKET) levelStep_ = -1;
            if (e.key.key == SDLK_RIGHTBRACKET) levelStep_ = 1;
            if (e.key.key == SDLK_N && !e.key.repeat) noclipToggle_ = true;
            if (e.key.key == SDLK_P && !e.key.repeat) physicsDebugToggle_ = true;
            if (e.key.key == SDLK_F1 && !e.key.repeat) debugToggles_[0] = true;
            if (e.key.key == SDLK_F2 && !e.key.repeat) debugToggles_[1] = true;
            if (e.key.key == SDLK_F3 && !e.key.repeat) debugToggles_[2] = true;
            if (e.key.key == SDLK_F4 && !e.key.repeat) debugToggles_[3] = true;
            if (e.key.key == SDLK_F5 && !e.key.repeat) debugToggles_[4] = true;
            if (e.key.key == SDLK_F6 && !e.key.repeat) debugToggles_[5] = true;
            if (e.key.key == SDLK_ESCAPE && !e.key.repeat) {
                escapePressed_ = true;
                // In the script-driven game Escape belongs to the MENU, so the
                // window must not consume it: the caller clears escapeQuits_
                // and reads TakeEscape() instead. The diagnostic viewers keep
                // the old behaviour - release the mouse, then quit - because
                // they have no menu to open.
                if (escapeQuits_) {
                    if (mouseCaptured_) SetMouseCaptured(false);
                    else return false;
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            // VK_LBUTTON 1, VK_RBUTTON 2, VK_MBUTTON 4 - the codes
            // Definitions.lua names MouseButtonLeft/Right/Middle.
            const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (e.button.button == SDL_BUTTON_LEFT)   vkDown_[1] = down;
            if (e.button.button == SDL_BUTTON_RIGHT)  vkDown_[2] = down;
            if (e.button.button == SDL_BUTTON_MIDDLE) vkDown_[4] = down;
            if (down && e.button.button == SDL_BUTTON_LEFT) leftClicked_ = true;
            // Clicking into the window captures the mouse rather than reaching
            // the game, so a click never both aims and fires. The menu wants
            // the opposite - it needs a visible cursor to point at rows - so
            // it suppresses capture while it is up.
            if (down && !mouseCaptured_ && allowCapture_) SetMouseCaptured(true);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            wheelSteps_ += int(e.wheel.y);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // Both are tracked, because they answer different questions: the
            // delta steers the view while the mouse is captured, and the
            // absolute position is what the menu hit-tests a row against.
            if (mouseCaptured_) {
                mouseDx_ += e.motion.xrel;
                mouseDy_ += e.motion.yrel;
            }
            mouseX_ = e.motion.x;
            mouseY_ = e.motion.y;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // Alt-tabbing away never delivers the key-up, so without this
            // the player keeps walking into a wall while the window is not
            // even focused.
            for (bool& k : vkDown_) k = false;
            // And let go of the mouse, or the pointer stays trapped in a
            // window nobody is looking at.
            hasFocus_ = false;
            SetMouseCaptured(false);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            hasFocus_ = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            width_ = e.window.data1;
            height_ = e.window.data2;
            resized_ = true;
            break;
        default:
            break;
        }
    }
    return true;
}

bool Window::IsDown(Key key) const {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return false;
    // The scale keys accept both the main row and the keypad.
    if (key == Key::ScaleUp)   return keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS];
    if (key == Key::ScaleDown) return keys[SDL_SCANCODE_MINUS]  || keys[SDL_SCANCODE_KP_MINUS];
    SDL_Scancode code = ScancodeFor(key);
    return code != SDL_SCANCODE_UNKNOWN && keys[code];
}

int Window::TakeWheelSteps() {
    const int steps = wheelSteps_;
    wheelSteps_ = 0;
    return steps;
}

int Window::TakeLevelStep() {
    int step = levelStep_;
    levelStep_ = 0;
    return step;
}

bool Window::TakeNoclipToggle() {
    const bool pressed = noclipToggle_;
    noclipToggle_ = false;
    return pressed;
}

bool Window::TakePhysicsDebugToggle() {
    const bool pressed = physicsDebugToggle_;
    physicsDebugToggle_ = false;
    return pressed;
}

bool Window::TakeDebugToggle(int index) {
    if (index < 0 || index >= 6) return false;
    const bool pressed = debugToggles_[index];
    debugToggles_[index] = false;
    return pressed;
}

void Window::TakeMouseDelta(float& dx, float& dy) {
    dx = mouseDx_;
    dy = mouseDy_;
    mouseDx_ = mouseDy_ = 0.f;
}

void Window::SetMouseCaptured(bool captured) {
    if (!window_ || captured == mouseCaptured_) return;
    mouseCaptured_ = captured;
    SDL_SetWindowRelativeMouseMode(window_, captured);
}

void Window::SetSystemCursorVisible(bool visible) {
    // The menu draws its OWN pointer (HUD/kursor), so the system one has to go
    // while it is up or there are two cursors on screen. Relative mode hides
    // the system cursor by itself, which is why this only matters once capture
    // is released for the menu.
    //
    // Only on a change: this is called every frame, and SDL_ShowCursor is a
    // platform call, not a flag test.
    if (visible == systemCursorVisible_) return;
    systemCursorVisible_ = visible;
    if (visible) SDL_ShowCursor();
    else SDL_HideCursor();
}

bool Window::TakeResized() {
    bool r = resized_;
    resized_ = false;
    return r;
}

void* Window::NativeHandle() const {
    if (!window_) return nullptr;
    SDL_PropertiesID props = SDL_GetWindowProperties(window_);
#if defined(_WIN32)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    return reinterpret_cast<void*>(
        SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
#endif
}

} // namespace painful
