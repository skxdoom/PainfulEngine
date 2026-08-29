#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

// The action bitmask, verbatim from LScripts/Main/Definitions.lua. The
// scripts build one of these every tick, hand it to the entity through
// ENTITY.PO_SetAction and then ask the mover to run it - so these bits are
// the whole vocabulary of what a player can do.
namespace Act {
enum : uint32_t {
    None              = 1,
    Forward           = 2,
    Backward          = 4,
    Left              = 8,
    Right             = 16,
    Jump              = 32,
    Fire              = 64,
    AltFire           = 128,
    NextWeapon        = 256,
    PrevWeapon        = 512,
    Weapon1           = 1024,          // Weapon<n> = Weapon1 << (n-1), to 14
    FireBestWeapon1   = 0x01000000,
    FireBestWeapon2   = 0x02000000,
    RocketJump        = 0x04000000,
    ForwardRocketJump = 0x08000000,
    UseCards          = 0x10000000,
    ComboFire         = 0x20000000,
    SelectBestWeapon1 = 0x40000000,
    SelectBestWeapon2 = 0x80000000,

    // What PlayerAction actually consumes: it masks its stored action down
    // to these five bits (0x3e) and leaves the rest to the scripts.
    MoveMask          = Forward | Backward | Left | Right | Jump,
};
}

// UIActions, likewise from Definitions.lua - the out-of-game keys, which the
// scripts read through INP.UIAction rather than the player's action mask.
namespace UIAct {
enum : uint32_t {
    None = 1, Pause = 2, Screenshot = 4, Menu = 8, Scoreboard = 16,
    SayToAll = 32, SayToTeam = 64, QuickLoad = 128, QuickSave = 256,
    Flashlight = 512, Zoom = 1024,
};
}

// Keyboard, mouse and the bindings that turn them into actions.
//
// Keys are Windows VIRTUAL-KEY codes throughout, because that is what the
// scripts use: Definitions.lua's `Keys` table is the standard VK list
// (Space 32, A 65, LeftShift 160) plus three codes the engine synthesises -
// NumlockEnter 252, MouseWheelForward 253, MouseWheelBack 254. Nothing here
// knows about SDL; the window translates and feeds us.
class Input {
public:
    static constexpr int kKeyCount = 256;
    static constexpr int kMouseWheelForward = 253;
    static constexpr int kMouseWheelBack = 254;

    // Rolls this frame's key state into last frame's, so KeyState can tell a
    // press from a hold. Call once per frame BEFORE feeding new state.
    void BeginFrame();

    void SetKeyDown(int vk, bool down);
    // The wheel has no held state: it reads as pressed for exactly the frame
    // its event arrived.
    void PulseKey(int vk);

    // INP.Key(k): 0 not pressed, 1 pressed this frame, 2 held. The scripts
    // rely on both halves - `==1` for toggles, `==2` for modifiers.
    int KeyState(int vk) const;
    bool IsDown(int vk) const { return vk > 0 && vk < kKeyCount && down_[vk]; }

    void AddMouseDelta(float dx, float dy) { mouseDx_ += dx; mouseDy_ += dy; }
    void TakeMouseDelta(float& dx, float& dy) {
        dx = mouseDx_; dy = mouseDy_;
        mouseDx_ = mouseDy_ = 0.f;
    }

    // INP.LoadBindings(): the bindings live in the scripts' own Cfg table as
    // Cfg.KeyPrimary<Action> / Cfg.KeyAlternative<Action>, holding engine key
    // NAMES ("Left Mouse Button", "Right Ctrl", "Space", "None"). Cfg.lua
    // carries the defaults and loads config.ini over them, and the options
    // menu calls this again after a rebind. `lookup` reads one Cfg field.
    using CfgReader = std::string (*)(void* ctx, const char* field);
    void LoadBindings(CfgReader lookup, void* ctx);
    bool hasBindings() const { return !actionBinds_.empty(); }

    // The masks the scripts ask for. ActionMask is what INP.GetActionStatus
    // returns; Action/UIAction answer the single-bit queries.
    uint32_t ActionMask() const;
    uint32_t UIActionMask() const;
    bool Action(uint32_t mask) const { return (ActionMask() & mask) != 0; }
    bool UIAction(uint32_t mask) const { return (UIActionMask() & mask) != 0; }

    // INP.IsFireSwitched(): primary and alternate fire swapped, either held
    // (FireSwitch) or latched (FireSwitchToggle).
    bool fireSwitched() const;

    // INP.Reset() - forget every key, so a menu transition cannot leave a
    // key stuck down. (INP.GetTime is not ours: it is the engine clock, and
    // lives with the other time natives.)
    void Reset();

    // "Left Mouse Button" -> 1, "Space" -> 32, "None" -> 0. The names are
    // the engine's own, recovered from its key-name table in Engine.dll.
    static int VirtualKeyForEngName(const std::string& name);

private:
    struct Bind {
        uint32_t bit;
        int vk;
    };

    bool down_[kKeyCount] = {};
    bool wasDown_[kKeyCount] = {};
    bool pulse_[kKeyCount] = {};
    float mouseDx_ = 0.f, mouseDy_ = 0.f;

    std::vector<Bind> actionBinds_;
    std::vector<Bind> uiBinds_;
    int fireSwitchKey_ = 0;         // held swap
    int fireSwitchToggleKey_ = 0;   // latched swap
    mutable bool fireSwitchLatch_ = false;
};

} // namespace painful
