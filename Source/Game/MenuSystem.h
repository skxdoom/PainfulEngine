#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace painful {

class HudRenderer;
class TextureCache;

// The menu: a retained widget tree the scripts DECLARE and the engine draws.
//
// This is the opposite of the HUD, and the difference is the whole design. The
// HUD is immediate - the scripts call HUD.DrawQuad every frame and we only
// rasterise. The menu is retained: PainMenu:ActivateScreen walks a Lua table
// once, calling PMENU.AddTextButton and PMENU.SetItem*, and then never draws
// anything. Layout, hit-testing, keyboard navigation and drawing are ours.
//
// Items are keyed by NAME, not by handle. AddTextButton at Engine.dll
// 0x10075a40 opens with MenuScreen::FindItem and only creates the item when
// the name is absent, which is why all ~40 SetItem* natives take a name string
// first. See Docs/Menu.md.
class MenuSystem {
public:
    static constexpr int kAlignNone = 1, kAlignLeft = 2, kAlignRight = 3, kAlignCenter = 4;

    enum class Kind {
        StaticText,   // a caption; never focusable
        TextButton,   // the ordinary menu row
    };

    struct Item {
        Kind kind = Kind::TextButton;
        std::string name;
        std::string text;
        std::string desc;        // the blurb shown for the focused row
        std::string action;      // LUA SOURCE, run when the item is chosen
        // The scripts pass -1 for x to mean "centre me", the same convention
        // HUD.PrintXY uses. Coordinates are in the 1024x768 the interface was
        // authored at, and are scaled to the window at draw time.
        float x = -1.f, y = 0.f;
        float width = 0.f;
        // Definitions.lua MenuAlign, which is ONE-based: None 1, Left 2,
        // Right 3, Center 4. Zero means the script never set one.
        int align = 0;
        // Packed ARGB, as R3D.RGB/RGBA build them.
        uint32_t textColor = 0xFF646464u;
        uint32_t disabledColor = 0xFF9B9B9Bu;
        uint32_t underMouseColor = 0xFFFFFFFFu;
        uint32_t descColor = 0xFFFFFFFFu;
        std::string fontBig = "timesbd", fontSmall = "timesbd";
        int fontBigSize = 26, fontSmallSize = 22;
        std::string sndLightOn;      // played when focus arrives
        bool visible = true;
        bool disabled = false;
        // Declaration order, so keyboard navigation walks the screen the way
        // the script wrote it rather than the way a map happens to sort.
        int order = 0;
        // Filled at draw time so the mouse can be tested against what was
        // actually drawn, in real pixels.
        float hitX = 0, hitY = 0, hitW = 0, hitH = 0;
    };

    void Attach(HudRenderer* hud, TextureCache* textures) {
        hud_ = hud;
        textures_ = textures;
    }
    // How the menu runs an item's `action`, which is a string of Lua.
    void SetActionRunner(std::function<void(const std::string&)> run) {
        runAction_ = std::move(run);
    }
    // How the menu plays an item's focus sound.
    void SetSoundPlayer(std::function<void(const std::string&)> play) {
        playSound_ = std::move(play);
    }

    // --- screen lifecycle -------------------------------------------------
    // What the Escape key does. The engine drives this, not the scripts:
    // PainMenu:OpenMenu and CloseMenu are HOOKS with no ActivateScreen in
    // them - they only save the camera FOV, read the server list and run the
    // CD check - and nothing in the shipped Lua ever calls either. So the
    // engine owns the transition and the scripts observe it.
    void Open();
    void Close();
    void Activate(bool on);
    bool active() const { return active_; }
    void Clear();          // PMENU.Clear: leave menu mode entirely
    void ClearScreen();    // PMENU.ClearScreen: drop the items, stay in the menu
    void SetBackground(const std::string& material, int type);
    void SetMenuWidth(float w) { menuWidth_ = w; }
    void SetTopPosition(float y) { topPosition_ = y; }
    void ShowMouse(bool on) { showMouse_ = on; }
    bool mouseShown() const { return showMouse_; }

    // --- items ------------------------------------------------------------
    Item& Add(const std::string& name, Kind kind);
    Item* Find(const std::string& name);
    size_t itemCount() const { return items_.size(); }

    // --- per frame --------------------------------------------------------
    // Mouse position in real pixels, and whether the button went down THIS
    // frame. Keyboard navigation arrives through the Nav* calls.
    void Update(float mouseX, float mouseY, bool clicked);
    void NavUp();
    void NavDown();
    void NavActivate();
    void Draw(int screenW, int screenH);

    const std::string& focusedName() const { return focused_; }

private:
    // The interface was authored at 1024x768; everything the scripts position
    // is in those units and is scaled to the real window here, which is what
    // keeps a menu laid out for 1024x768 centred and proportionate at any
    // resolution.
    float sx() const { return float(screenW_) / 1024.f; }
    float sy() const { return float(screenH_) / 768.f; }

    std::vector<Item*> Ordered();
    void MoveFocus(int delta);
    void Choose(const Item& item);

    HudRenderer* hud_ = nullptr;
    TextureCache* textures_ = nullptr;
    std::function<void(const std::string&)> runAction_;
    std::function<void(const std::string&)> playSound_;

    std::map<std::string, Item> items_;
    int nextOrder_ = 0;
    std::string focused_;
    std::string background_;
    int backgroundType_ = 0;
    int backgroundMaterial_ = 0;     // HudRenderer material handle
    float menuWidth_ = 0.f, topPosition_ = 0.f;
    bool active_ = false;
    bool showMouse_ = true;
    int screenW_ = 1024, screenH_ = 768;
    // An action can activate a different screen, which clears the items out
    // from under the loop that is walking them. So actions are deferred to the
    // end of the frame rather than run where they are found.
    std::vector<std::string> pending_;
};

} // namespace painful
