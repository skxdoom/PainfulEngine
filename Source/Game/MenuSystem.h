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

    // Definitions.lua MenuItemTypes, for the kinds we draw. The numbering
    // there is the script's; this is our own, because most of the 24 types are
    // not implemented and a sparse enum would invite the zero-based mistake
    // MenuAlign already cost once.
    enum class Kind {
        StaticText,    // a caption; never focusable
        TextButton,    // the ordinary menu row
        Checkbox,      // On/Off
        Slider,        // a numeric range, dragged or arrowed
        NumRange,      // integer steps, arrowed only
        TextButtonEx,  // a row whose VALUE is one of a list the script owns
        TextEdit,      // free text, and NumEdit which is the same with digits
        Border,        // the carved frame a panel sits inside
    };

    // The frame is drawn from ten tiled pieces; see DrawBorder.
    static constexpr int kBorderPieces = 10;

    // Whether a kind carries a value the arrows change. TextButton does not:
    // it is chosen, not adjusted.
    // Everything but a caption can take focus.
    static bool Focusable(Kind k) { return k != Kind::StaticText && k != Kind::Border; }

    static bool HasValue(Kind k) {
        return k == Kind::Checkbox || k == Kind::Slider || k == Kind::NumRange ||
               k == Kind::TextButtonEx;
    }

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
        // PMENU.SetItemFontsTex: the texture the glyphs are filled WITH, bound
        // as a second stage the way HUD::Print does it. 46 shipped screens ask
        // for "HUD/font_texturka_alpha", and without it their authored
        // RGBA(100,100,100) draws as literal grey.
        std::string fontBigTex, fontSmallTex;
        int fontBigTexMat = -1, fontSmallTexMat = -1;
        std::string sndLightOn;      // played when focus arrives
        bool visible = true;
        bool disabled = false;

        // --- the value a widget carries ------------------------------------
        // One number covers checkbox (0/1), slider and num-range, because the
        // scripts read them all back as numbers. `valueText` is what a
        // TextButtonEx shows, which the SCRIPT owns - it holds the list and
        // pushes the new label through ChangeTextButtonExValue - and is also
        // the buffer a TextEdit accumulates.
        double value = 0.0;
        double minValue = 0.0, maxValue = 100.0;
        bool isFloat = false;
        std::string valueText;
        size_t maxLength = 0;        // TextEdit / NumEdit character cap
        float sliderWidth = 340.f;   // in 1024-wide authoring units
        // --- Border ---------------------------------------------------------
        float height = 0.f;          // SetBorderSize, with width above
        float headerHeight = 0.f;    // SetBorderHeader: a dark band at the top
        bool dark = false;           // AddBorder's second argument
        std::vector<float> columns;  // SetBorderColCount / SetBorderColumn
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
    // Freezing the world is part of the TRANSITION, not of the key that
    // triggered it: a script forcing the menu up on a dropped connection has
    // to pause too, and hanging this off the Escape handler misses that.
    void SetPauseHandler(std::function<void(bool)> pause) {
        setPaused_ = std::move(pause);
    }
    // Reads one TXT.* entry out of the script layer, for the words a widget
    // draws itself - a checkbox's On and Off.
    void SetTextReader(std::function<std::string(const std::string&)> read) {
        readText_ = std::move(read);
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
    // TXT.On and TXT.Off, handed down from the script layer so a checkbox
    // reads in the player's language rather than in hardcoded English.
    void SetOnOffText(const std::string& on, const std::string& off) {
        onText_ = on;
        offText_ = off;
    }

    // Draws the carved frame around a bare rectangle, in authoring units.
    // HUD.DrawBorder is this: the original builds a MenuItemBorder for it, so
    // the HUD and the menu share one frame rather than each having their own.
    // Needs the screen size, so it only draws inside a frame the menu or the
    // HUD has already begun.
    void DrawFrame(float x, float y, float w, float h);

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
    // Left and right adjust the focused widget. A TextButtonEx has no value of
    // its own - the SCRIPT owns the list - so adjusting one just runs its
    // action, which pushes the next label back through
    // ChangeTextButtonExValue. That is how the original cycles one.
    void NavAdjust(int direction);
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
    int FontTexture(const Item& item, bool big);
    void DrawValue(const Item& item, float x, float y, int size, uint32_t colour);
    void DrawBorder(const Item& item);
    void DrawCursor();
    void MoveFocus(int delta);
    void Choose(const Item& item);

    HudRenderer* hud_ = nullptr;
    TextureCache* textures_ = nullptr;
    std::function<void(const std::string&)> runAction_;
    std::function<void(const std::string&)> playSound_;
    std::function<void(bool)> setPaused_;
    std::function<std::string(const std::string&)> readText_;

    std::map<std::string, Item> items_;
    int nextOrder_ = 0;
    std::string focused_;
    std::string background_;
    int backgroundType_ = 0;
    int backgroundMaterial_ = 0;     // HudRenderer material handle
    // HUD/kursor - the engine draws the pointer itself, from a name held in
    // Engine.dll rather than in any script. 32x32, and Polish for "cursor".
    int cursorMaterial_ = -1;
    float mouseX_ = 0.f, mouseY_ = 0.f;
    float menuWidth_ = 0.f, topPosition_ = 0.f;
    bool active_ = false;
    bool showMouse_ = true;
    std::string onText_ = "On", offText_ = "Off";
    std::vector<int> borderArt_;   // the ten frame pieces, resolved on first use
    int screenW_ = 1024, screenH_ = 768;
    // An action can activate a different screen, which clears the items out
    // from under the loop that is walking them. So actions are deferred to the
    // end of the frame rather than run where they are found.
    std::vector<std::string> pending_;
};

} // namespace painful
