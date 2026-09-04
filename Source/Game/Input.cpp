#include "Input.h"

#include "../Core/Log.h"

#include <cstring>

namespace painful {
namespace {

struct NamedKey {
    const char* name;
    int vk;
};

// The engine's own key names, recovered from Engine.dll's key-name table
// (the array that feeds INP.GetKeyNameByEngName, sitting beside the short
// names "LMB"/"RCtrl"/"WheelFwd"). Codes come from Definitions.lua's `Keys`,
// which is the standard Windows virtual-key list.
const NamedKey kNamedKeys[] = {
    {"None", 0},
    {"Left Mouse Button", 1},   {"Right Mouse Button", 2},
    {"Middle Mouse Button", 4},
    {"Mouse Wheel Forward", Input::kMouseWheelForward},
    {"Mouse Wheel Back", Input::kMouseWheelBack},

    {"Backspace", 8},  {"Tab", 9},        {"Return", 13},
    {"Shift", 16},     {"Control", 17},   {"Alt", 18},
    {"Pause", 19},     {"Caps Lock", 20}, {"Escape", 27},   {"Space", 32},
    {"Page Up", 33},   {"Page Down", 34}, {"End", 35},      {"Home", 36},
    {"Cursor Left", 37}, {"Cursor Up", 38},
    {"Cursor Right", 39}, {"Cursor Down", 40},
    {"Print Screen", 44}, {"Insert", 45}, {"Delete", 46},

    // "Menu" is the Windows key here, not Alt - Definitions.lua names the
    // same codes LeftWindows / RightWindows / FarRightWindows.
    {"Left Menu", 91}, {"Right Menu", 92}, {"Far Right Menu", 93},

    {"Numpad 0", 96},  {"Numpad 1", 97},  {"Numpad 2", 98},  {"Numpad 3", 99},
    {"Numpad 4", 100}, {"Numpad 5", 101}, {"Numpad 6", 102}, {"Numpad 7", 103},
    {"Numpad 8", 104}, {"Numpad 9", 105},
    {"Numpad *", 106}, {"Numpad +", 107}, {"Numpad -", 109},
    {"Numpad .", 110}, {"Numpad /", 111}, {"Numpad Enter", 252},

    {"F1", 112},  {"F2", 113},  {"F3", 114},  {"F4", 115},
    {"F5", 116},  {"F6", 117},  {"F7", 118},  {"F8", 119},
    {"F9", 120},  {"F10", 121}, {"F11", 122}, {"F12", 123},

    {"Num Lock", 144},   {"Scroll Lock", 145},
    {"Left Shift", 160}, {"Right Shift", 161},
    {"Left Ctrl", 162},  {"Right Ctrl", 163},
    {"Left Alt", 164},   {"Right Alt", 165},

    {";", 186}, {"=", 187}, {",", 188}, {"-", 189}, {".", 190}, {"/", 191},
    {"~", 192}, {"[", 219}, {"\\", 220}, {"]", 221}, {"'", 222},
};

// Cfg.KeyPrimary<suffix> / Cfg.KeyAlternative<suffix> -> the bit it sets.
// The suffixes are the game's own, straight out of config.ini.
struct NamedAction {
    const char* cfgSuffix;
    uint32_t bit;
};

const NamedAction kActionBinds[] = {
    {"MoveForward", Act::Forward},   {"MoveBackward", Act::Backward},
    {"StrafeLeft", Act::Left},       {"StrafeRight", Act::Right},
    {"Jump", Act::Jump},
    {"Fire", Act::Fire},             {"AlternativeFire", Act::AltFire},
    {"NextWeapon", Act::NextWeapon}, {"PreviousWeapon", Act::PrevWeapon},
    {"FireBestWeapon1", Act::FireBestWeapon1},
    {"FireBestWeapon2", Act::FireBestWeapon2},
    {"SelectBestWeapon1", Act::SelectBestWeapon1},
    {"SelectBestWeapon2", Act::SelectBestWeapon2},
    {"RocketJump", Act::RocketJump},
    {"ForwardRocketJump", Act::ForwardRocketJump},
    {"UseCards", Act::UseCards},
};

const NamedAction kUIBinds[] = {
    {"Pause", UIAct::Pause},           {"Screenshot", UIAct::Screenshot},
    {"Menu", UIAct::Menu},             {"Scoreboard", UIAct::Scoreboard},
    {"SayToAll", UIAct::SayToAll},     {"SayToTeam", UIAct::SayToTeam},
    {"QuickLoad", UIAct::QuickLoad},   {"QuickSave", UIAct::QuickSave},
    {"Flashlight", UIAct::Flashlight}, {"Zoom", UIAct::Zoom},
};

} // namespace

std::string Input::EngNameForVirtualKey(int vk) {
    if (vk <= 0) return "None";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    for (const NamedKey& k : kNamedKeys)
        if (k.vk == vk) return k.name;
    return {};
}

std::string Input::ShortNameForEngName(const std::string& name) {
    struct Short { const char* eng; const char* brief; };
    static const Short kShort[] = {
        {"Left Mouse Button", "LMB"},   {"Right Mouse Button", "RMB"},
        {"Middle Mouse Button", "MMB"}, {"Mouse Wheel Forward", "WheelFwd"},
        {"Mouse Wheel Back", "WheelBack"}, {"Backspace", "BkSp"},
        {"Return", "Enter"},   {"Control", "Ctrl"},   {"Escape", "Esc"},
        {"Page Up", "PgUp"},   {"Page Down", "PgDn"}, {"Cursor Left", "Left"},
        {"Cursor Up", "Up"},   {"Cursor Right", "Right"}, {"Cursor Down", "Down"},
        {"Print Screen", "PrtSc"}, {"Insert", "Ins"},  {"Delete", "Del"},
        {"Left Shift", "LShift"},  {"Right Shift", "RShift"},
        {"Left Ctrl", "LCtrl"},    {"Right Ctrl", "RCtrl"},
        {"Left Alt", "LAlt"},      {"Right Alt", "RAlt"},
        {"Numpad Enter", "NumEnter"}, {"Caps Lock", "Caps"},
        {"Num Lock", "NumLk"},     {"Scroll Lock", "ScrLk"},
    };
    for (const Short& s : kShort)
        if (name == s.eng) return s.brief;
    if (name.compare(0, 7, "Numpad ") == 0) return "Num" + name.substr(7);
    return name;
}

int Input::VirtualKeyForEngName(const std::string& name) {
    if (name.empty()) return 0;
    // Letters and digits are their own names, and are already their codes.
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') return c;
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        if (c >= '0' && c <= '9') return c;
    }
    for (const NamedKey& k : kNamedKeys)
        if (name == k.name) return k.vk;
    LogWarn("input: unknown key name \"%s\"", name.c_str());
    return 0;
}

void Input::BeginFrame() {
    // A consumed UI action comes back once its key is up again.
    for (const Bind& b : uiBinds_)
        if (!IsDown(b.vk)) uiConsumed_ &= ~b.bit;
    std::memcpy(wasDown_, down_, sizeof(down_));
    // A wheel notch lasts exactly one frame: it went down last frame, so it
    // comes up now.
    for (int i = 0; i < kKeyCount; ++i) {
        if (pulse_[i]) {
            down_[i] = false;
            pulse_[i] = false;
        }
    }
}

void Input::SetKeyDown(int vk, bool down) {
    if (vk > 0 && vk < kKeyCount) down_[vk] = down;
}

void Input::PulseKey(int vk) {
    if (vk > 0 && vk < kKeyCount) {
        down_[vk] = true;
        pulse_[vk] = true;
    }
}

int Input::KeyState(int vk) const {
    if (vk <= 0 || vk >= kKeyCount || !down_[vk]) return 0;
    return wasDown_[vk] ? 2 : 1;
}

void Input::Reset() {
    std::memset(down_, 0, sizeof(down_));
    std::memset(wasDown_, 0, sizeof(wasDown_));
    std::memset(pulse_, 0, sizeof(pulse_));
    mouseDx_ = mouseDy_ = 0.f;
}

void Input::LoadBindings(CfgReader lookup, void* ctx) {
    actionBinds_.clear();
    uiBinds_.clear();

    // Every action can carry a primary and an alternate key, and either may
    // read "None".
    auto bind = [&](std::vector<Bind>& out, const NamedAction& a) {
        for (const char* prefix : {"KeyPrimary", "KeyAlternative"}) {
            const std::string field = std::string(prefix) + a.cfgSuffix;
            const int vk = VirtualKeyForEngName(lookup(ctx, field.c_str()));
            if (vk) out.push_back({a.bit, vk});
        }
    };
    for (const NamedAction& a : kActionBinds) bind(actionBinds_, a);
    for (const NamedAction& a : kUIBinds) bind(uiBinds_, a);

    // Weapon1..Weapon14 are regular, so they are generated rather than
    // listed: Actions.Weapon<n> = Weapon1 << (n-1).
    for (int n = 1; n <= 14; ++n) {
        const NamedAction a = {nullptr, Act::Weapon1 << (n - 1)};
        for (const char* prefix : {"KeyPrimary", "KeyAlternative"}) {
            const std::string field =
                std::string(prefix) + "Weapon" + std::to_string(n);
            const int vk = VirtualKeyForEngName(lookup(ctx, field.c_str()));
            if (vk) actionBinds_.push_back({a.bit, vk});
        }
    }

    fireSwitchKey_ = VirtualKeyForEngName(lookup(ctx, "KeyPrimaryFireSwitch"));
    fireSwitchToggleKey_ =
        VirtualKeyForEngName(lookup(ctx, "KeyPrimaryFireSwitchToggle"));
    fireSwitchLatch_ = false;

    LogInfo("input: %zu action bindings, %zu UI bindings", actionBinds_.size(),
            uiBinds_.size());
}

uint32_t Input::ActionMask() const {
    uint32_t mask = 0;
    for (const Bind& b : actionBinds_)
        if (IsDown(b.vk)) mask |= b.bit;
    return mask;
}

// Held while the key is down; everything else is one press at a time. Which is
// which is a STAND-IN read off the shipped scripts and play, not the engine's
// own table - see Docs/Reference/PlayerMovement.md, "UI actions".
constexpr uint32_t kUIHeld = UIAct::Scoreboard | UIAct::Zoom;

uint32_t Input::UIActionMask() const {
    uint32_t mask = 0;
    for (const Bind& b : uiBinds_) {
        const bool on = (b.bit & kUIHeld) ? IsDown(b.vk) : (KeyState(b.vk) == 1);
        if (on) mask |= b.bit;
    }
    return mask & ~uiConsumed_;
}

bool Input::fireSwitched() const {
    if (fireSwitchToggleKey_ && KeyState(fireSwitchToggleKey_) == 1)
        fireSwitchLatch_ = !fireSwitchLatch_;
    const bool held = fireSwitchKey_ && IsDown(fireSwitchKey_);
    return fireSwitchLatch_ != held;   // either alone swaps; both cancel
}

} // namespace painful
