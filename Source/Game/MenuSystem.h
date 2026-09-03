#pragma once
#include <chrono>
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
// first. See Docs/Reference/Menu.md.
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
        KeyControl,    // one action's row in the key table: label, primary, alternative
        Scroller,      // declared by the scripts for a long list; drawn by nothing yet
        TabGroup,      // a tab box over a panel; the group's rows show with it
        LoadSave,      // the save-game table: level, playtime, saved-at, difficulty
    };

    // The frame is drawn from ten tiled pieces; see DrawBorder.
    static constexpr int kBorderPieces = 10;

    // Whether a kind carries a value the arrows change. TextButton does not:
    // it is chosen, not adjusted.
    // Everything but a caption can take focus.
    static bool Focusable(Kind k) {
        return k != Kind::StaticText && k != Kind::Border && k != Kind::Scroller &&
               k != Kind::TabGroup;
    }

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
        // PMENU.EnableItemBG(name, "blaszka"): the bevelled plate a row sits
        // on. The art is a three-slice under HUD/blachy_menu - _lewa, _centrum,
        // _prawa, left cap, tiled middle, right cap - and the script passes
        // only the base name.
        std::string itemBG;
        int itemBGMat[3] = {-1, -1, -1};
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
        // AddSlider's ninth argument: where the value column ENDS, measured
        // from the menu box's left edge (PainMenu defaults 700). The bar sits
        // to its left, the value right-aligned against it.
        float sliderCtrlWidth = 700.f;
        // --- Border ---------------------------------------------------------
        float height = 0.f;          // SetBorderSize, with width above
        float headerHeight = 0.f;    // SetBorderHeader: a dark band at the top
        bool dark = false;           // AddBorder's second argument
        std::vector<float> columns;  // SetBorderColCount / SetBorderColumn
        // KeyControl: the engine key names ("Left Mouse Button", "None") the
        // scripts read back with GetPrimaryKey / GetAlternateKey and write to
        // Cfg, and the words shown for them. keyIndex is the row in the key
        // table (SetKeyItemIndex); 0 is the header. keySingle is
        // AddSimpleKeyConf's one-key variant.
        std::string keyPrimary, keyAlt;
        std::string keyPrimaryText, keyAltText;
        int keyIndex = -1;
        bool keySingle = false;
        // StaticText: SetStaticTextRect(name, x1, y1, x2, y2) - the text is
        // wrapped to the rectangle and its lines centred in it. A yes/no
        // prompt is the case: one long question over (240,240)-(780,380).
        bool hasTextRect = false;
        float textRect[4] = {0, 0, 0, 0};
        // Slider: where the bar was drawn, in pixels, for the mouse.
        float barX = 0, barY = 0, barW = 0, barH = 0;
        // LoadSave: the rows PMENU.AddSaveGameToList added. Row 0 is the
        // header when its slot is "header"; "empty" is the new-save row.
        // MenuItemList (Engine.dll 0x1006b310); layout in DrawLoadSave.
        struct SaveRow {
            std::string slot, level, time, date, diff;
        };
        std::vector<SaveRow> rows;
        int selected = -1;           // index into rows, -1 none
        bool allowSave = true;       // PMENU.SetAllowSave
        float listMaxHeight = 0.f;   // PMENU.SetListMaxHeight, authoring units
        int listScroll = 0;          // first data row on show
        std::vector<float> rowTop;   // screen y of each row this frame (0 = not drawn)
        float rowH = 0.f;
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
    // Any string down a dotted global path ("__pkCardTex.c6"), for the few
    // things the map needs out of script tables that are not TXT.
    void SetPathReader(std::function<std::string(const std::string&)> read) {
        readPath_ = std::move(read);
    }

    // --- screen lifecycle -------------------------------------------------
    // What the Escape key does. The engine drives this, not the scripts:
    // PainMenu:OpenMenu and CloseMenu are HOOKS with no ActivateScreen in
    // them - they only save the camera FOV, read the server list and run the
    // CD check - and nothing in the shipped Lua ever calls either. So the
    // engine owns the transition and the scripts observe it.
    void Open();
    void Close();
    // PMENU.Activate: the scripts' own way out - PainMenu:LoadLevel and
    // SaveGame:Load end with Activate(false) - so off means the screen goes
    // and the world unpauses, without the CloseMenu hook Escape runs.
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
    // The screen the authoring units scale to. Draw sets it itself; a caller
    // drawing a frame while the menu is down (the console) sets it first.
    void SetScreenSize(int w, int h) {
        screenW_ = w;
        screenH_ = h;
    }

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
    void FocusFirst();
    void Draw(int screenW, int screenH);

    const std::string& focusedName() const { return focused_; }

    // --- the campaign map (PMENU.SwitchToMap) -----------------------------
    // One level as Levels_FillMap declares it through PMENU.AddLevelToMap.
    struct MapLevel {
        int chapter = 1;
        std::string dir;            // the level directory, what Game:LoadLevel takes
        std::string name;           // the localised name
        std::string sketch;         // HUD/Map/sketch_*, or sketch_question when locked
        std::string cardCondition;  // the tarot card's unlock text
        int cardIndex = 0;
        int status = 0;             // 0 unavailable, 1 current, 2 finished, 3 locked
    };
    // The engine's own SwitchMapSelect: clear the screen, ask the scripts for
    // the levels, take the screen over with the map.
    void EnterMap();
    void AddMapLevel(const MapLevel& level);
    void MapReset();
    void MapSetCurrent(int level, int chapter);     // 1-based, MapSetCurrLevel
    void MapNextLevel();
    int mapCurrLevel() const { return mapCurrLevel_; }
    int mapCurrChapter() const { return mapCurrChapter_; }
    const MapLevel* mapCurrent() const;
    bool mapMode() const { return mapMode_; }
    // Escape on the map: back to the main menu; on the board: back to the
    // map. False when on neither.
    bool Back();

    // --- the tarot board (PMENU.SwitchToBoard, the MBOARD natives) ---------
    // EngineGame::SwitchMagicBoard: the engine calls MagicBoard:Setup() back
    // into Lua - which declares the slot rows and every card - shows the
    // board, and runs MagicBoard_UpdateCardsStatus() on the way out so the
    // scripts read the selection back through IsCardInSlot.
    struct BoardSlots {
        int count = 0;
        float y = 0, w = 0, h = 0, space = 0;
        std::vector<float> x;
    };
    struct BoardCard {
        int type = 1;              // MagicCardsTypes: 1 Time, 2 Perm
        std::string name, texture, desc, bigImage;
        int cost = 0;
        bool available = false;
        bool selected = false;
    };
    void EnterBoard();
    void LeaveBoard();
    bool boardMode() const { return boardMode_; }
    void BoardSetupSlots(int type, int count, float y, float w, float h, float space);
    void BoardSetSlotX(int type, int slot, float x);
    void BoardAddCard(const BoardCard& card);
    // MBOARD.IsCardInSlot(type, i): for an All row, whether card i of that
    // row's kind still sits there (i.e. is NOT selected); for a Sel row,
    // whether slot i holds a card.
    bool BoardCardInSlot(int type, int index) const;

    // --- key binding ------------------------------------------------------
    // Choosing a key row starts a capture; the next key or mouse button
    // pressed lands in the column the pointer (or left/right) picked. Escape
    // cancels, Backspace and Delete bind "None". The app feeds every key edge
    // while a capture is open.
    bool capturing() const { return !capture_.empty(); }
    void KeyPressed(int vk);
    // Whether the left button is held right now, for dragging a slider.
    void SetMouseDown(bool down) { mouseDown_ = down; }
    // The level the player chose on the map, once. The app loads it at the
    // top of a frame, since the load tears down the world the menu is drawn
    // over.
    bool TakePendingLevel(std::string& dir, std::string& name, std::string& sketch);

private:
    // The interface was authored at 1024x768; everything the scripts position
    // is in those units and is scaled to the real window here, which is what
    // keeps a menu laid out for 1024x768 centred and proportionate at any
    // resolution.
    float sx() const { return float(screenW_) / 1024.f; }
    float sy() const { return float(screenH_) / 768.f; }

    std::vector<Item*> Ordered();
    int FontTexture(const Item& item, bool big);
    float ValueWidth(const Item& item, int size);
    std::string ValueString(const Item& item) const;
    void DrawItemBG(Item& item, float x, float y, float w, float h);
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

    // The map.
    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool Contains(float px, float py) const {
            return px >= x && py >= y && px < x + w && py < y + h;
        }
    };
    int MapMat(const std::string& name);
    std::vector<int> MapChapterLevels(int chapter) const;
    int MapChapterCount() const;
    void DrawMap();
    void UpdateMap(float mouseX, float mouseY, bool clicked);
    void MapChoose();
    void MapMoveChapter(int delta);
    void MapMoveCursor(int delta);
    std::vector<MapLevel> mapLevels_;
    bool mapMode_ = false;
    bool blackEdition_ = false;           // the last background named *_black
    int mapChapter_ = 1;                  // the chapter on show, 1-based
    int mapCursor_ = 0;                   // the card under focus within it
    int mapHoverChapter_ = 0;
    int mapCurrChapter_ = 0, mapCurrLevel_ = 0;   // the marker, 1-based, 0 = none
    std::map<std::string, int> mapMat_;          // art by name, any screen
    std::vector<Rect> mapDigitRects_;            // the chapter wedges
    std::vector<Rect> mapLevelRects_;            // the level digits on the ring
    Rect mapCrystalRect_, mapArrowRects_[2];     // go, previous level, next level
    Rect mapPlateRect_, mapCardRect_, mapPentRect_;
    bool mapCrystalHover_ = false;
    bool mapPlateHover_ = false, mapCardHover_ = false, mapPentHover_ = false;
    int mapLevelHover_ = -1;                     // the ring spot under the pointer
    float plateAngle_ = 0.f;                     // where the selector plate is, radians
    std::chrono::steady_clock::time_point plateClock_ = std::chrono::steady_clock::now();
    std::function<std::string(const std::string&)> readPath_;

    // The board.
    void DrawBoard();
    void UpdateBoard(float mouseX, float mouseY, bool clicked);
    std::vector<const BoardCard*> BoardCardsOfType(int type) const;
    bool boardMode_ = false;
    BoardSlots boardSlots_[4];                   // by BoardSlotsTypes
    std::vector<BoardCard> boardCards_;
    int boardHover_ = -1;                        // index into boardCards_
    std::vector<std::pair<Rect, int>> boardCardRects_;
    Rect boardCrystalRect_;
    bool boardCrystalHover_ = false;
    MapLevel pendingLevel_;
    bool hasPendingLevel_ = false;

    // Widgets drawn from the shipped art (HUD/border, HUD/blachy_menu, HUD/Chk*).
    void DrawTabGroup(const Item& item, int index);
    void DrawSlider(const Item& item, float labelX, float y, int size, uint32_t colour,
                    float menuLeft, bool explicitX);
    void DrawCheckbox(const Item& item, float x, float y, int size);
    void DrawKeyScroller();

    // The key table.
    void DrawKeyRow(Item& item, bool focused);
    void EnsureKeyRowVisible();
    // The save table: draws it, sets the Delete/Save/Load buttons' enabled
    // state from the selection, and answers clicks and arrows inside it.
    void DrawLoadSave(Item& item, bool focused);
    void ListClick(Item& item, float mouseY);
    bool ListNav(Item& item, int delta);
    void ListActivate(Item& item);
    void ListButtons(const Item& item);
    int keyColumn_ = 1;              // 1 primary, 2 alternative - where the pointer is
    int keyScroll_ = 0;              // first visible row of the table, 0-based
    float keyColumn2X_ = 0.f;        // screen x where the alternative column starts
    std::string capture_;            // the row being rebound, or empty
    bool mouseDown_ = false;
    std::string dragging_;           // the slider the held button is on
    int captureColumn_ = 1;
    bool captureFresh_ = false;      // set on the frame the capture opened
    // An action can activate a different screen, which clears the items out
    // from under the loop that is walking them. So actions are deferred to the
    // end of the frame rather than run where they are found.
    std::vector<std::string> pending_;
};

} // namespace painful
