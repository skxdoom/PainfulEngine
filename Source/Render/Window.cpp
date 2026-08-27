#include "Window.h"
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
} // namespace

bool Window::Open(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LogWarn("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        LogWarn("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void Window::Close() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

bool Window::PumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            return false;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_LEFTBRACKET) levelStep_ = -1;
            if (e.key.key == SDLK_RIGHTBRACKET) levelStep_ = 1;
            if (e.key.key == SDLK_N && !e.key.repeat) noclipToggle_ = true;
            if (e.key.key == SDLK_P && !e.key.repeat) physicsDebugToggle_ = true;
            if (e.key.key == SDLK_ESCAPE) {
                // First Escape releases the mouse, a second one quits.
                if (mouseCaptured_) SetMouseCaptured(false);
                else return false;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!mouseCaptured_) SetMouseCaptured(true);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (mouseCaptured_) {
                mouseDx_ += e.motion.xrel;
                mouseDy_ += e.motion.yrel;
            }
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

void Window::TakeMouseDelta(float& dx, float& dy) {
    dx = mouseDx_;
    dy = mouseDy_;
    mouseDx_ = mouseDy_ = 0.f;
}

void Window::SetMouseCaptured(bool captured) {
    if (!window_) return;
    mouseCaptured_ = captured;
    SDL_SetWindowRelativeMouseMode(window_, captured);
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
