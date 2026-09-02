#include "MenuSystem.h"
#include "Input.h"

#include "../Core/Log.h"
#include "../Render/HudRenderer.h"
#include "../Render/TextureCache.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace painful {

namespace {

// The scripts build colours with R3D.RGB/RGBA, which pack D3D ARGB. The HUD
// batcher wants bgfx's little-endian ABGR. Same conversion the HUD natives do.
uint32_t ArgbToAbgr(uint32_t argb) {
    const uint32_t a = (argb >> 24) & 0xFF, r = (argb >> 16) & 0xFF;
    const uint32_t g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

} // namespace

void MenuSystem::Open() {
    if (active_ || !runAction_) return;
    active_ = true;
    focused_.clear();
    // PainMenu.mainScreen is MainMenu, and the in-game rows on it (Resume
    // Game, Return to Map) reveal themselves through their own inGameOnly
    // handling inside SetupScreen - so one entry point serves both cases.
    if (setPaused_) setPaused_(true);
    runAction_("PainMenu:OpenMenu(); PainMenu:ActivateScreen(PainMenu.mainScreen)");
    // TXT.On / TXT.Off, so a checkbox reads in the player's language. Taken
    // once the scripts are up rather than at construction, because the
    // language table is built during boot.
    if (readText_) SetOnOffText(readText_("On"), readText_("Off"));
}

void MenuSystem::Close() {
    if (!active_) return;
    if (runAction_) runAction_("PainMenu:CloseMenu()");
    ClearScreen();
    active_ = false;
    if (setPaused_) setPaused_(false);
}

void MenuSystem::Activate(bool on) {
    active_ = on;
    if (!on) focused_.clear();
}

void MenuSystem::Clear() {
    ClearScreen();
    active_ = false;
}

void MenuSystem::ClearScreen() {
    items_.clear();
    nextOrder_ = 0;
    focused_.clear();
    mapMode_ = false;
    if (backgroundMaterial_ > 0 && hud_) hud_->ReleaseMaterial(backgroundMaterial_);
    backgroundMaterial_ = 0;
    background_.clear();
    // The cursor is deliberately NOT released here: it belongs to the menu
    // rather than to a screen, and survives every screen change.
}

void MenuSystem::SetBackground(const std::string& material, int type) {
    backgroundType_ = type;
    // Cfg.BlackEdition swaps HUD/Menu for HUD/Menu_black in ActivateScreen; the
    // map follows suit with Map_black, which is the engine's own `Menu_black`
    // test.
    if (!material.empty()) blackEdition_ = material.find("black") != std::string::npos;
    if (material == background_) return;
    if (backgroundMaterial_ > 0 && hud_) hud_->ReleaseMaterial(backgroundMaterial_);
    background_ = material;
    backgroundMaterial_ = 0;
    if (hud_ && textures_ && !material.empty())
        backgroundMaterial_ = hud_->CreateMaterial(material, *textures_, "");
}

MenuSystem::Item& MenuSystem::Add(const std::string& name, Kind kind) {
    // FindItem-then-create, which is what the original does: a second Add with
    // a name already present configures the existing item rather than
    // replacing it, and the scripts rely on that when they rebuild a screen.
    auto it = items_.find(name);
    if (it != items_.end()) return it->second;

    Item& item = items_[name];
    item.name = name;
    item.kind = kind;
    item.order = nextOrder_++;
    return item;
}

MenuSystem::Item* MenuSystem::Find(const std::string& name) {
    auto it = items_.find(name);
    return it == items_.end() ? nullptr : &it->second;
}

std::vector<MenuSystem::Item*> MenuSystem::Ordered() {
    std::vector<Item*> out;
    out.reserve(items_.size());
    for (auto& kv : items_) out.push_back(&kv.second);
    std::sort(out.begin(), out.end(),
              [](const Item* a, const Item* b) { return a->order < b->order; });
    return out;
}

void MenuSystem::Choose(const Item& item) {
    // A key row opens a capture rather than running anything.
    if (item.kind == Kind::KeyControl && !item.disabled && item.keyIndex > 0) {
        capture_ = item.name;
        captureColumn_ = item.keySingle ? 1 : keyColumn_;
        captureFresh_ = true;
        return;
    }
    if (item.disabled || item.action.empty()) return;
    // Deferred: an action commonly calls PainMenu:ActivateScreen, which clears
    // every item - including the one we are standing in.
    pending_.push_back(item.action);
}

void MenuSystem::MoveFocus(int delta) {
    std::vector<Item*> reachable;
    for (Item* i : Ordered())
        if (Focusable(i->kind) && i->visible && !i->disabled)
            reachable.push_back(i);
    if (reachable.empty()) return;

    // Up and down walk the screen the way it LOOKS, top to bottom and then
    // left to right, not the way the items were declared. Declaration order is
    // the order `next()` happened to walk the screen's Lua table, which is
    // arbitrary - opening the main menu seated the highlight on Options rather
    // than on the first row.
    std::sort(reachable.begin(), reachable.end(), [](const Item* a, const Item* b) {
        if (a->y != b->y) return a->y < b->y;
        return a->order < b->order;
    });

    int at = -1;
    for (size_t i = 0; i < reachable.size(); ++i)
        if (reachable[i]->name == focused_) at = int(i);

    // Wraps, which is what the shipped menu does at both ends.
    const int n = int(reachable.size());
    at = (at < 0) ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;

    if (reachable[at]->name != focused_) {
        focused_ = reachable[at]->name;
        if (playSound_ && !reachable[at]->sndLightOn.empty())
            playSound_(reachable[at]->sndLightOn);
    }
}

// Called once a screen has been built, so the menu is usable from the keyboard
// the moment it opens and the highlight has somewhere to be. The mouse takes
// over the instant it moves over a row.
void MenuSystem::FocusFirst() {
    if (mapMode_) return;
    if (focused_.empty()) MoveFocus(1);
}

void MenuSystem::NavUp() {
    if (mapMode_) { MapMoveChapter(-1); return; }
    MoveFocus(-1);
    EnsureKeyRowVisible();
}
void MenuSystem::NavDown() {
    if (mapMode_) { MapMoveChapter(1); return; }
    MoveFocus(1);
    EnsureKeyRowVisible();
}

void MenuSystem::NavAdjust(int direction) {
    if (mapMode_) { MapMoveCursor(direction); return; }
    // Left and right on a key row pick the column the next capture edits.
    if (const Item* f = Find(focused_))
        if (f->kind == Kind::KeyControl && !f->keySingle) {
            keyColumn_ = direction < 0 ? 1 : 2;
            return;
        }
    Item* item = Find(focused_);
    if (!item || item->disabled || !HasValue(item->kind)) return;

    switch (item->kind) {
    case Kind::Checkbox:
        // Either arrow toggles. The original lets left and right both flip a
        // checkbox rather than making one of them a no-op.
        item->value = (item->value != 0.0) ? 0.0 : 1.0;
        break;
    case Kind::Slider: {
        // A hundredth of the range per press for a float, one unit for an
        // integer - which is what makes a 0..100 volume move in whole
        // percent and a 0..1 gamma move smoothly.
        const double span = item->maxValue - item->minValue;
        const double step = item->isFloat ? span / 100.0 : 1.0;
        item->value += step * direction;
        if (item->value < item->minValue) item->value = item->minValue;
        if (item->value > item->maxValue) item->value = item->maxValue;
        if (!item->isFloat) item->value = std::floor(item->value + 0.5);
        break;
    }
    case Kind::NumRange:
        item->value += direction;
        // A maximum of -1 means unbounded, which is how the script spells
        // "no upper limit" for things like a frag limit.
        if (item->value < item->minValue) item->value = item->minValue;
        if (item->maxValue != -1.0 && item->value > item->maxValue)
            item->value = item->maxValue;
        break;
    case Kind::TextButtonEx:
        // No value of ours to change: the script holds the list and pushes the
        // next label back through ChangeTextButtonExValue when its action runs.
        break;
    default:
        return;
    }
    Choose(*item);
}

void MenuSystem::NavActivate() {
    if (mapMode_) { MapChoose(); return; }
    if (Item* item = Find(focused_)) Choose(*item);
}

void MenuSystem::Update(float mouseX, float mouseY, bool clicked) {
    if (!active_) return;
    mouseX_ = mouseX;
    mouseY_ = mouseY;
    if (mapMode_) { UpdateMap(mouseX, mouseY, clicked); return; }
    // The click that opened a capture must not also be the key it binds.
    captureFresh_ = false;

    // The mouse wins over the keyboard whenever it is over a row: hover moves
    // focus, so the description text and the highlight follow the pointer.
    if (showMouse_ && capture_.empty()) {
        for (Item* item : Ordered()) {
            if (!Focusable(item->kind) || !item->visible || item->disabled) continue;
            if (item->hitW <= 0.f || item->hitH <= 0.f) continue;
            if (mouseX < item->hitX || mouseX > item->hitX + item->hitW) continue;
            if (mouseY < item->hitY || mouseY > item->hitY + item->hitH) continue;

            if (focused_ != item->name) {
                focused_ = item->name;
                if (playSound_ && !item->sndLightOn.empty()) playSound_(item->sndLightOn);
            }
            // Which key cell of a row the pointer is over.
            if (item->kind == Kind::KeyControl)
                keyColumn_ = (keyColumn2X_ > 0.f && mouseX >= keyColumn2X_) ? 2 : 1;
            if (clicked) Choose(*item);
            break;
        }
    }

    // Run whatever was chosen, now that nothing is walking the item list.
    if (!pending_.empty()) {
        std::vector<std::string> run;
        run.swap(pending_);
        for (const std::string& chunk : run)
            if (runAction_) runAction_(chunk);
    }
}

void MenuSystem::Draw(int screenW, int screenH) {
    if (!active_ || !hud_) return;
    screenW_ = screenW;
    screenH_ = screenH;
    if (mapMode_) {
        DrawMap();
        if (showMouse_) DrawCursor();
        return;
    }

    // The background covers the whole screen regardless of its aspect: it is
    // artwork, not a layout element.
    if (backgroundMaterial_ > 0)
        hud_->Quad(backgroundMaterial_, 0.f, 0.f, float(screenW), float(screenH), 0xffffffffu);

    // Borders first, whatever order they were declared in: they are the panels
    // everything else sits ON, so a border added after its contents would
    // otherwise paint over them.
    for (Item* item : Ordered())
        if (item->kind == Kind::Border && item->visible) DrawBorder(*item);
    // Tab groups: every group's tab box shows, only the visible group's panel.
    {
        int tabIndex = 0;
        for (Item* item : Ordered())
            if (item->kind == Kind::TabGroup) DrawTabGroup(*item, tabIndex++);
    }

    const Item* focusedItem = nullptr;

    for (Item* item : Ordered()) {
        if (item->kind == Kind::Border || item->kind == Kind::Scroller ||
            item->kind == Kind::TabGroup)
            continue;
        if (!item->visible) {
            item->hitW = item->hitH = 0.f;
            continue;
        }
        const bool isFocused = (item->name == focused_) && !item->disabled &&
                               Focusable(item->kind);
        if (isFocused) focusedItem = item;
        if (item->kind == Kind::KeyControl) {
            DrawKeyRow(*item, isFocused);
            continue;
        }

        const int size = int(std::lround(double(item->fontBigSize) * double(sy())));
        const float w = hud_->TextWidth(item->fontBig, size, item->text);
        const float h = hud_->TextHeight(item->fontBig, size);

        // x < 0 is "centre me", the same convention HUD.PrintXY carries. A real
        // x is in 1024-wide authoring units and scales to the window.
        //
        // MenuAlign is ONE-BASED (Definitions.lua: None 1, Left 2, Right 3,
        // Center 4), so alignment only decides which edge of the string sits
        // at x. Read as zero-based it sends every left-aligned item off the
        // left of the screen and every right-aligned one off the right, which
        // is exactly what the bottom bar did before this was checked.
        // A negative x does NOT simply mean "centre on screen" - it means "let
        // the alignment place me inside the menu box", and the box is
        // menuWidth wide (PainMenu defaults 720 authoring units) centred on the
        // screen. VideoOptions is what proves it: TextureQualityWeapons and
        // TextureQualityCharacters are BOTH declared x = -1, y = 330, and
        // differ only in align - Left and Right. They are one two-column row,
        // and centring both drew them on top of each other.
        //
        // An explicit x still wins, and there alignment only chooses which
        // edge of the string sits on it.
        const float menuW = menuWidth_ > 0.f ? menuWidth_ * sx() : float(screenW);
        const float menuLeft = (float(screenW) - menuW) * 0.5f;
        float x;
        if (item->x >= 0.f)
            x = (item->align == kAlignRight) ? item->x * sx() - w : item->x * sx();
        else if (item->align == kAlignLeft)
            x = menuLeft;
        else if (item->align == kAlignRight)
            // The right-hand COLUMN, not right-aligned text: the label leads
            // and its value follows a sliderWidth along, exactly as in the left
            // half. Right-aligning the label against the menu's edge instead
            // leaves its value nowhere to go - it lands past the screen, which
            // is why Characters and Skies drew with no setting beside them.
            x = menuLeft + menuW * 0.5f;
        else
            x = (float(screenW) - w) * 0.5f;
        const float y = item->y * sy();

        uint32_t colour = item->textColor;
        if (item->disabled)     colour = item->disabledColor;
        else if (isFocused)     colour = item->underMouseColor;

        // The plate goes behind the row, and only under the FOCUSED one: the
        // art is opaque bronze, so drawing it under every row would tile the
        // whole column over the background and lose the menu's artwork
        // entirely. As a highlight it is what makes the selected row read as
        // pressed.
        // The plate goes under EVERY row that asked for one, not only the
        // focused one - the shipped Options screen shows five plates - at the
        // art's own proportions: 114 high with 110-wide caps, standing 67
        // authoring units tall on an 80-unit row pitch, centred on the text,
        // and spanning the menu box less an 84-unit margin each side (the
        // original's plate is 553 units wide in a 720-unit box).
        if (!item->itemBG.empty()) {
            const float plateH = 67.f * sy();
            const float plateX = menuLeft + 84.f * sx();
            DrawItemBG(*item, plateX, y + h * 0.5f - plateH * 0.5f, menuW - 168.f * sx(), plateH);
        }

        // A checkbox is its box, then its label - not a label with "On"
        // beside it (HUD/ChkChecked, ChkUnchecked).
        float labelX = x;
        if (item->kind == Kind::Checkbox) {
            DrawCheckbox(*item, x, y, size);
            labelX = x + 50.f * sx();
        }
        hud_->Text(item->fontBig, size, labelX, y, item->text, ArgbToAbgr(colour),
                   FontTexture(*item, true));

        // The hit target is the ROW, not the word. PMENU.SetMenuWidth is what
        // says how wide a row is - PainMenu defaults it to 720 authoring units
        // - and hit-testing the glyphs alone leaves most of the row dead, so
        // the pointer only highlights an item while it is literally over the
        // letters. Rows are also spaced further apart than a line is tall
        // (80 units against about 60), so the box grows to close that gap too.
        item->hitX = x;
        item->hitY = y;
        item->hitW = w;
        item->hitH = h;
        if (item->x < 0.f && menuWidth_ > 0.f) {
            // A left- or right-aligned row shares its line with the other
            // half, so it takes half the box. Centred rows take all of it.
            if (item->align == kAlignLeft) {
                item->hitX = menuLeft;
                item->hitW = menuW * 0.5f;
            } else if (item->align == kAlignRight) {
                item->hitX = menuLeft + menuW * 0.5f;
                item->hitW = menuW * 0.5f;
            } else {
                item->hitX = menuLeft;
                item->hitW = menuW;
            }
        }
        if (HasValue(item->kind))
            item->hitW = std::max(item->hitW, (item->sliderWidth + 180.f) * sx());

        // A widget's VALUE is drawn to the right of its label, in the column
        // the screen's sliderWidth reserves. The label sits at the item's x, so
        // the value column starts a fixed distance along rather than after the
        // text - otherwise the values in a list of options would not line up.
        if (item->kind == Kind::Slider) {
            DrawSlider(*item, x, y, size, colour, menuLeft, item->x >= 0.f);
        } else if (HasValue(item->kind)) {
            // A full-width row puts its value a fixed sliderWidth along, so
            // the values in a column line up. A HALF row - one side of a
            // two-column line - has no room for that: sliderWidth is 340
            // authoring units against a half of 360, so the value would run
            // into the next column's label. There the value right-aligns to
            // the half's own right edge instead.
            const bool halfRow = item->x < 0.f && menuWidth_ > 0.f &&
                                 (item->align == kAlignLeft || item->align == kAlignRight);
            float vx = x + item->sliderWidth * sx();
            if (halfRow) {
                const float halfLeft =
                    menuLeft + (item->align == kAlignRight ? menuW * 0.5f : 0.f);
                // A gutter, or the left half's value ends exactly where the right
                // half's label begins and the two read as one word.
                vx = halfLeft + menuW * 0.5f - ValueWidth(*item, size) - 28.f * sx();
            }
            DrawValue(*item, vx, y, size, colour);
        }
    }

    // Rows are spaced further apart than a line is tall - 80 authoring units
    // against about 60 - which would leave a dead band between them where the
    // pointer highlights nothing. Each row in a column therefore grows down to
    // meet the next, so a column hit-tests as one continuous strip.
    {
        std::vector<Item*> rows;
        for (Item* item : Ordered())
            if (item->hitW > 0.f && item->hitH > 0.f) rows.push_back(item);
        std::sort(rows.begin(), rows.end(),
                  [](const Item* a, const Item* b) { return a->hitY < b->hitY; });
        for (size_t i = 0; i + 1 < rows.size(); ++i) {
            Item* a = rows[i];
            const Item* b = rows[i + 1];
            // Only within one column, and only across a gap small enough to be
            // row spacing rather than a different block of the screen.
            if (a->hitX != b->hitX || a->hitW != b->hitW) continue;
            const float gap = b->hitY - (a->hitY + a->hitH);
            if (gap > 0.f && gap < a->hitH) a->hitH = b->hitY - a->hitY;
        }
    }

    DrawKeyScroller();

    // "Version: 1.64", top right: the engine calls PainMenu_PrintGameVersion
    // (the name is in Engine.dll) every menu frame, and it draws with
    // HUD.PrintXY into the batch that is open right now.
    if (runAction_) runAction_("if PainMenu_PrintGameVersion then PainMenu_PrintGameVersion() end");

    // The focused row's blurb, centred near the bottom - where the shipped
    // menu puts it.
    if (focusedItem && !focusedItem->desc.empty()) {
        const int size = int(std::lround(double(focusedItem->fontSmallSize) * double(sy())));
        hud_->Text(focusedItem->fontSmall, size, -1.f, float(screenH) - 80.f * sy(),
                   focusedItem->desc, ArgbToAbgr(focusedItem->descColor),
                   FontTexture(*focusedItem, false));
    }

    DrawCursor();
}

// Resolved on first use and cached on the item, because a screen redraws
// every frame and CreateMaterial would otherwise hand out a slot per frame.
int MenuSystem::FontTexture(const Item& item, bool big) {
    if (!hud_ || !textures_) return 0;
    const std::string& name = big ? item.fontBigTex : item.fontSmallTex;
    int& cached = const_cast<Item&>(item).*(big ? &Item::fontBigTexMat
                                               : &Item::fontSmallTexMat);
    if (name.empty()) return 0;
    if (cached < 0) cached = hud_->CreateMaterial(name, *textures_, "");
    return cached;
}

// How wide the value half of a row draws, so a half-column can right-align it.
// The bevelled plate a row sits on, from the three-slice under
// HUD/blachy_menu. The caps keep their own width and the middle tiles between
// them, which is why the art is three pieces and not one stretched image.
void MenuSystem::DrawItemBG(Item& item, float x, float y, float w, float h) {
    if (!hud_ || !textures_ || item.itemBG.empty()) return;
    if (item.itemBGMat[0] < 0) {
        static const char* kSuffix[3] = {"_lewa", "_centrum", "_prawa"};
        for (int i = 0; i < 3; ++i)
            item.itemBGMat[i] = hud_->CreateMaterial(
                "HUD/blachy_menu/" + item.itemBG + kSuffix[i], *textures_, "");
    }
    if (item.itemBGMat[0] <= 0 || item.itemBGMat[2] <= 0) return;

    // The caps keep the ART's proportions: blaszka_lewa and _prawa are 110 x
    // 114, so a cap is as wide as 110/114 of the plate's height whatever the
    // plate's width. MaterialSize can report a padded size, which is what
    // stretched them before; the file's own numbers are used instead.
    const float capL = h * (110.f / 114.f), capR = capL;
    const float middle = w - capL - capR;

    hud_->Quad(item.itemBGMat[0], x, y, capL, h, 0xffffffffu);
    // The middle REPEATS - blaszka_centrum is 103 x 114 of bevelled metal,
    // and stretched across a plate it reads as one smeared highlight. Tiled
    // at the plate's own scale, with the last tile cut to fit.
    if (middle > 0.f && item.itemBGMat[1] > 0) {
        const float tileW = h * (103.f / 114.f);
        for (float at = 0.f; at < middle; at += tileW) {
            const float span = std::min(tileW, middle - at);
            hud_->Quad(item.itemBGMat[1], x + capL + at, y, span, h, 0xffffffffu, 0.f, 0.f,
                       span / tileW, 1.f);
        }
    }
    hud_->Quad(item.itemBGMat[2], x + w - capR, y, capR, h, 0xffffffffu);
}

float MenuSystem::ValueWidth(const Item& item, int size) {
    if (!hud_) return 0.f;
    switch (item.kind) {
    case Kind::Checkbox:
        return hud_->TextWidth(item.fontBig, size,
                               item.value != 0.0 ? onText_ : offText_);
    case Kind::TextButtonEx:
        return hud_->TextWidth(item.fontBig, size, item.valueText);
    case Kind::NumRange: {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", int(item.value));
        return hud_->TextWidth(item.fontBig, size, buf);
    }
    case Kind::Slider:
        // The bar plus its number; sliders are full-width rows in every
        // shipped screen, so this only matters if one ever is not.
        return 200.f * sx() + 16.f * sx() + hud_->TextWidth(item.fontBig, size, "000.00");
    default:
        return 0.f;
    }
}

void MenuSystem::DrawValue(const Item& item, float x, float y, int size, uint32_t colour) {
    const uint32_t abgr = ArgbToAbgr(colour);

    switch (item.kind) {
    case Kind::Checkbox:
        // The box is drawn before the label by the row itself; nothing here.
        (void)x; (void)y; (void)size; (void)abgr;
        break;

    case Kind::Slider:
        break;                       // DrawSlider, from the row

    case Kind::NumRange: {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", int(item.value));
        hud_->Text(item.fontBig, size, x, y, buf, abgr);
        break;
    }

    case Kind::TextButtonEx:
        hud_->Text(item.fontBig, size, x, y, item.valueText, abgr);
        break;

    default:
        break;
    }
}

// The carved frame every menu panel sits inside - MenuItemBorder::Render at
// Engine.dll 0x100643b0, which is also what HUD.DrawBorder builds.
//
// It is a nine-slice with a striped fill, drawn from ten pieces whose names
// are held in the constructor at 0x10064a90 (and are Polish: naroznik is
// corner, ramka is frame, tlo_paski is striped background):
//
//   naroznik_lewy_gora / prawy_gora / lewy_dol / prawy_dol   the four corners
//   ramka_gorna_srodek / dolna_srodek / lewa / prawa         the four edges
//   tlo_paski / tlo_paski_ciemne                             the fill, light and dark
//
// Every piece is TILED at its native size rather than stretched, which is why
// the art is small - the fill is 32x32 and the edges are about 30 across.
//
// The overhangs below (-3, -5, -7, -11, -22...) are the original's, in raw
// pixels and deliberately not scaled: the frame sits slightly OUTSIDE the
// rectangle it is given, so the panel's content area is the rectangle itself.
void MenuSystem::DrawFrame(float x, float y, float w, float h) {
    Item panel;
    panel.kind = Kind::Border;
    panel.x = x;
    panel.y = y;
    panel.width = w;
    panel.height = h;
    DrawBorder(panel);
}

void MenuSystem::DrawBorder(const Item& item) {
    if (!hud_ || !textures_) return;
    if (borderArt_.empty()) {
        static const char* kNames[kBorderPieces] = {
            "HUD/border/naroznik_lewy_gora",  "HUD/border/naroznik_prawy_gora",
            "HUD/border/naroznik_lewy_dol",   "HUD/border/naroznik_prawy_dol",
            "HUD/border/ramka_gorna_srodek",  "HUD/border/ramka_dolna_srodek",
            "HUD/border/ramka_lewa",          "HUD/border/ramka_prawa",
            "HUD/border/tlo_paski",           "HUD/border/tlo_paski_ciemne",
        };
        borderArt_.resize(kBorderPieces);
        for (int i = 0; i < kBorderPieces; ++i)
            borderArt_[i] = hud_->CreateMaterial(kNames[i], *textures_, "");
    }

    const int cornerTL = borderArt_[0], cornerTR = borderArt_[1];
    const int cornerBL = borderArt_[2], cornerBR = borderArt_[3];
    const int edgeTop  = borderArt_[4], edgeBottom = borderArt_[5];
    const int edgeLeft = borderArt_[6], edgeRight = borderArt_[7];
    const int fill     = borderArt_[8], fillDark = borderArt_[9];

    const float x = item.x * sx();
    const float y = item.y * sy();
    const float w = item.width * sx();
    const float h = item.height * sy();
    const float headerH = item.headerHeight * sy();
    const uint32_t white = 0xffffffffu;

    // The header band, in the dark stripe, then the body beneath it.
    if (headerH > 0.f) hud_->Tiles(fillDark, x, y, w, headerH, white);

    if (item.columns.empty()) {
        hud_->Tiles(item.dark ? fillDark : fill, x, y + headerH, w, h - headerH, white);
    } else {
        // Columns alternate light and dark, which is what gives a list its
        // banding. The LAST column takes whatever width is left rather than
        // its declared one, so rounding never leaves a gap at the right edge.
        float at = 0.f;
        for (size_t c = 0; c < item.columns.size(); ++c) {
            float cw = item.columns[c] * sx();
            if (c + 1 == item.columns.size()) cw = w - at;
            hud_->Tiles((c & 1) ? fillDark : fill, x + at, y + headerH, cw, h - headerH, white);
            at += cw;
        }
        // A separator down each column boundary but the last.
        float sep = 0.f;
        for (size_t c = 0; c + 1 < item.columns.size(); ++c) {
            sep += item.columns[c] * sx();
            hud_->Tiles(edgeLeft, x - 7.f + sep, y, 0.f, h, white);
        }
    }

    // The rule under the header, then the four edges. Widths and heights of
    // zero mean "one texture across", so each of these tiles along one axis.
    if (headerH > 0.f) hud_->Tiles(edgeTop, x, y - 7.f + headerH, w, 0.f, white);

    int cw = 0, ch = 0;
    hud_->MaterialSize(cornerTL, cw, ch);
    hud_->Tiles(edgeLeft, x - 7.f, y - 3.f + float(ch), 0.f, h - float(ch) - 3.f, white);
    hud_->MaterialSize(cornerTR, cw, ch);
    hud_->Tiles(edgeRight, x - 11.f + w, y - 5.f + float(ch), 0.f, h - float(ch) - 5.f, white);
    hud_->MaterialSize(cornerBL, cw, ch);
    hud_->Tiles(edgeBottom, x + float(cw) - 3.f, y - 11.f + h, w - float(cw) - 3.f, 0.f, white);
    hud_->Tiles(edgeTop, x, y - 7.f, w, 0.f, white);

    // The corners last, at native size, each overhanging its own way.
    const struct { int mat; float dx, dy; bool fromRight, fromBottom; } corners[4] = {
        {cornerBL, -3.f,  -22.f, false, true},
        {cornerTL, -3.f,  -3.f,  false, false},
        {cornerTR, -23.f, -5.f,  true,  false},
        {cornerBR, -24.f, -25.f, true,  true},
    };
    for (const auto& c : corners) {
        int mw = 0, mh = 0;
        if (!hud_->MaterialSize(c.mat, mw, mh)) continue;
        hud_->Quad(c.mat, x + c.dx + (c.fromRight ? w : 0.f),
                   y + c.dy + (c.fromBottom ? h : 0.f), float(mw), float(mh), white);
    }
}

// ---------------------------------------------------------------- widgets
//
// The art under HUD/border: strzalka (arrow) and dzwigienka (lever, the
// knob) in small and large, kreska (line) in small and large - the slider is
// the small set, the list scroller the large - plus HUD/ChkChecked and
// ChkUnchecked for a checkbox. Sizes are the files' own, scaled with the
// screen.

// A slider: an arrow at each end, the red line between, the knob at the
// value, the number right-aligned to the value column. Read off the shipped
// Video Options: with a 720-unit menu box the number ENDS at
// menuLeft + sliderCtrlWidth (700) and the bar of sliderWidth (370) ends
// thirty units short of it. A row with an explicit x lays the bar after its
// own label instead.
void MenuSystem::DrawSlider(const Item& item, float labelX, float y, int size, uint32_t colour,
                            float menuLeft, bool explicitX) {
    const float SX = sx(), SY = sy();
    const uint32_t abgr = ArgbToAbgr(colour);
    char buf[32];
    if (item.isFloat) snprintf(buf, sizeof buf, "%.2f", item.value / 100.0);
    else              snprintf(buf, sizeof buf, "%d", int(item.value));
    const float valueW = hud_->TextWidth(item.fontBig, size, buf);
    const float valueSlot = hud_->TextWidth(item.fontBig, size, "00.00");

    // The arrows sit OUTSIDE the line, so the label and the value clear
    // them, not just the line.
    const float aw = 62.f * SX, ah = 40.f * SY;
    float barLeft, barRight, valueX;
    if (explicitX) {
        barLeft = labelX + hud_->TextWidth(item.fontBig, size, item.text) + 24.f * SX + aw;
        barRight = barLeft + item.sliderWidth * SX;
        valueX = barRight + aw + 12.f * SX;
    } else {
        const float valueRight = menuLeft + item.sliderCtrlWidth * SX;
        valueX = valueRight - valueW;
        barRight = valueRight - valueSlot - 12.f * SX - aw;
        barLeft = barRight - item.sliderWidth * SX;
    }
    const float cy = y + float(size) * 0.5f;
    const double span = item.maxValue - item.minValue;
    const double t = span > 0.0 ? (item.value - item.minValue) / span : 0.0;

    // The LARGE set is the slider's: strzalka_duza points right, kreska_duza
    // is a stretch of horizontal line, dzwigienka_duza the upright knob. (The
    // small set is vertical - the list scroller's.)
    const int arrow = MapMat("HUD/border/strzalka_duza");
    const int line = MapMat("HUD/border/kreska_duza");
    const int knob = MapMat("HUD/border/dzwigienka_duza");
    if (line > 0) hud_->Tiles(line, barLeft, cy - 17.f * SY, barRight - barLeft, 0.f);
    // The spearheads point INTO the line: the file's right-pointing arrow
    // stands at the left end, its mirror at the right.
    if (arrow > 0) {
        hud_->Quad(arrow, barLeft - aw, cy - ah * 0.5f, aw, ah, 0xffffffffu);
        hud_->Quad(arrow, barRight, cy - ah * 0.5f, aw, ah, 0xffffffffu, 1.f, 0.f, 0.f, 1.f);
    }
    if (knob > 0) {
        const float kw = 45.f * SX, kh = 59.f * SY;
        hud_->Quad(knob, barLeft + float(t) * (barRight - barLeft) - kw * 0.5f, cy - kh * 0.5f, kw,
                   kh, 0xffffffffu);
    }
    hud_->Text(item.fontBig, size, valueX, y, buf, abgr, FontTexture(item, true));
}

// HUD/ChkChecked.tga is the red diamond tick ALONE on transparency (40 x 37;
// the .bmp beside it is a 16-pixel Windows icon the resolver must not pick),
// and ChkUnchecked is empty. The bevelled box under it is drawn here: a dark
// fill with a bronze rim, in the item's own colour, which is what the
// original's box looks like beside "Invert Mouse".
void MenuSystem::DrawCheckbox(const Item& item, float x, float y, int size) {
    const float bw = 40.f * sx(), bh = 37.f * sy();
    const float by = y + float(size) * 0.5f - bh * 0.5f;
    const float rim = std::max(1.f, 2.f * sy());
    const uint32_t bronze = ArgbToAbgr(item.disabled ? item.disabledColor : item.textColor);
    hud_->Quad(0, x, by, bw, bh, bronze);
    hud_->Quad(0, x + rim, by + rim, bw - 2.f * rim, bh - 2.f * rim, 0xff141414u);
    if (item.value != 0.0) {
        const int tick = MapMat("HUD/ChkChecked");
        if (tick > 0) hud_->Quad(tick, x, by, bw, bh, 0xffffffffu);
    }
}

// A tab group is a tab box - 180 x 52, the first ten units in from the
// group's x and eight down, the next 172 along - over a panel that starts
// fifty units below the group's y (the shipped VideoOptions group at 122,70
// 776x560 draws its panel from y 120 to 630; ControlsConfig declares the
// same panel as an explicit EmptyBorder at y 110). The script places the tab
// LABELS itself, as ordinary rows, and shifts the inactive one eight units
// down - which is why "Advanced" sits lower than "General" in the original.
void MenuSystem::DrawTabGroup(const Item& item, int index) {
    Item tab = item;
    tab.kind = Kind::Border;
    tab.x = item.x + 10.f + float(index) * 172.f;
    tab.y = item.y + 8.f;
    tab.width = 180.f;
    tab.height = 52.f;
    tab.columns.clear();
    tab.headerHeight = 0.f;
    tab.dark = !item.visible;
    DrawBorder(tab);
    if (!item.visible) return;
    Item panel = item;
    panel.kind = Kind::Border;
    panel.y = item.y + 50.f;
    panel.height = item.height - 50.f;
    panel.columns.clear();
    panel.headerHeight = 0.f;
    DrawBorder(panel);
}

// ---------------------------------------------------------------- key rows
//
// PMENU.AddKeyControl(name, label, primaryOption, alternativeOption,
// primaryText, alternativeText, primaryKey, alternativeKey) declares one
// action's row; PMENU.SetKeyItemIndex places it, 0 being the disabled header
// row ("Action | Primary | Alternative"). The rows carry no position of their
// own: they are a TABLE inside the border the script names KeyBorder - at
// (50,110), 924 x 410, a 50-high header band and three columns of 328/308/308
// authoring units - twelve rows visible (maxVisible) of fourteen, the rest
// reached by scrolling.
//
// PainMenu:ApplyControlConfig reads the result back with GetPrimaryKey /
// GetAlternateKey, writes Cfg, and ApplyControlSettings runs
// INP.LoadBindings and Cfg:Save - so a rebind reaches config.ini through the
// scripts' own path, exactly as in the original.
namespace {
// Thirteen rows in the 360-unit body of the shipped KeyBorder.
constexpr float kKeyRowH = 27.f;
constexpr float kKeyCellPad = 20.f;
}  // namespace

// The list scroller beside the key table: a large arrow at each end of a
// large line, the large lever as the thumb.
void MenuSystem::DrawKeyScroller() {
    const Item* border = Find("KeyBorder");
    if (!border) return;
    int rows = 0;
    for (const auto& kv : items_)
        if (kv.second.kind == Kind::KeyControl && !kv.second.keySingle && kv.second.visible)
            rows = std::max(rows, kv.second.keyIndex);
    const float header = border->headerHeight > 0.f ? border->headerHeight : 50.f;
    const int visible = std::max(1, int((border->height - header) / kKeyRowH));
    if (rows <= visible) return;

    // The SMALL set: strzalka_mala points down (flipped for the top),
    // kreska_mala is a stretch of vertical line, dzwigienka_mala the thumb.
    const float SX = sx(), SY = sy();
    const int arrow = MapMat("HUD/border/strzalka_mala");
    const int line = MapMat("HUD/border/kreska_mala");
    const int thumb = MapMat("HUD/border/dzwigienka_mala");
    const float aw = 35.f * SX, ah = 42.f * SY;
    const float x = (border->x + border->width - 30.f) * SX - aw * 0.5f;
    const float top = (border->y + 6.f) * SY;
    const float bottom = (border->y + border->height - 6.f) * SY;
    if (line > 0) hud_->Tiles(line, x, top + ah, 0.f, bottom - top - 2.f * ah);
    if (arrow > 0) {
        hud_->Quad(arrow, x, top, aw, ah, 0xffffffffu, 0.f, 1.f, 1.f, 0.f);
        hud_->Quad(arrow, x, bottom - ah, aw, ah, 0xffffffffu);
    }
    if (thumb > 0) {
        const float tw = 40.f * SX, th = 29.f * SY;
        const float travel = (bottom - ah) - (top + ah) - th;
        const float t = float(keyScroll_) / float(std::max(1, rows - visible));
        hud_->Quad(thumb, x + aw * 0.5f - tw * 0.5f, top + ah + travel * t, tw, th, 0xffffffffu);
    }
}

void MenuSystem::DrawKeyRow(Item& item, bool focused) {
    const float SX = sx(), SY = sy();
    const int size = int(std::lround(double(item.fontBigSize) * double(SY)));
    uint32_t colour = item.textColor;
    if (item.disabled)  colour = item.disabledColor;
    else if (focused)   colour = item.underMouseColor;
    const bool thisCapture = capture_ == item.name;

    // AddSimpleKeyConf's one-key row sits where the script put it.
    if (item.keySingle) {
        const float x = item.x >= 0.f ? item.x * SX : (float(screenW_) - 300.f * SX) * 0.5f;
        const float y = item.y * SY;
        hud_->Text(item.fontBig, size, x, y, item.text, ArgbToAbgr(colour),
                   FontTexture(item, true));
        const std::string shown = thisCapture ? "..." : item.keyPrimaryText;
        hud_->Text(item.fontBig, size, x + 160.f * SX, y, shown, ArgbToAbgr(colour),
                   FontTexture(item, true));
        item.hitX = x;
        item.hitY = y;
        item.hitW = 300.f * SX;
        item.hitH = kKeyRowH * SY;
        return;
    }

    // The table's frame, from the border the script declared, or its shipped
    // numbers when a screen has none.
    float bx = 50.f, by = 110.f, bw = 924.f, bh = 410.f, header = 50.f;
    float cols[3] = {328.f, 308.f, 308.f};
    if (const Item* border = Find("KeyBorder")) {
        bx = border->x; by = border->y;
        if (border->width > 0.f)  bw = border->width;
        if (border->height > 0.f) bh = border->height;
        if (border->headerHeight > 0.f) header = border->headerHeight;
        for (size_t c = 0; c < 3 && c < border->columns.size(); ++c)
            if (border->columns[c] > 0.f) cols[c] = border->columns[c];
    }
    const int visible = std::max(1, int((bh - header) / kKeyRowH));
    float y;
    if (item.keyIndex <= 0) {
        y = (by + (header - kKeyRowH) * 0.5f) * SY;
    } else {
        const int r = item.keyIndex - 1 - keyScroll_;
        if (r < 0 || r >= visible) { item.hitW = item.hitH = 0.f; return; }
        y = (by + header + float(r) * kKeyRowH) * SY;
    }
    // The label sits left in its column; the two keys are CENTRED in theirs,
    // and the header row centres all three.
    const float col2W = bw - cols[0] - cols[1];
    const float c0 = (bx + cols[0] * 0.5f) * SX;
    const float c1 = (bx + cols[0] + cols[1] * 0.5f) * SX;
    const float c2 = (bx + cols[0] + cols[1] + col2W * 0.5f) * SX;
    keyColumn2X_ = (bx + cols[0] + cols[1]) * SX;
    const auto centred = [&](float cx, const std::string& s) {
        return cx - hud_->TextWidth(item.fontBig, size, s) * 0.5f;
    };

    const float x0 = item.keyIndex <= 0 ? centred(c0, item.text) : (bx + kKeyCellPad) * SX;
    hud_->Text(item.fontBig, size, x0, y, item.text, ArgbToAbgr(colour), FontTexture(item, true));
    // The cell being edited shows "..." in place of its key; the cell the
    // next capture would edit is the one drawn in the focus colour.
    const uint32_t plain = ArgbToAbgr(item.disabled ? item.disabledColor : item.textColor);
    const uint32_t hot = ArgbToAbgr(item.underMouseColor);
    const bool editPrimary = thisCapture && captureColumn_ == 1;
    const bool editAlt = thisCapture && captureColumn_ == 2;
    const std::string primary = editPrimary ? "..." : item.keyPrimaryText;
    const std::string alt = editAlt ? "..." : item.keyAltText;
    hud_->Text(item.fontBig, size, centred(c1, primary), y, primary,
               (focused && keyColumn_ == 1) || editPrimary ? hot : plain, FontTexture(item, true));
    hud_->Text(item.fontBig, size, centred(c2, alt), y, alt,
               (focused && keyColumn_ == 2) || editAlt ? hot : plain, FontTexture(item, true));

    item.hitX = bx * SX;
    item.hitY = y;
    item.hitW = bw * SX;
    item.hitH = kKeyRowH * SY;
}

void MenuSystem::EnsureKeyRowVisible() {
    const Item* f = Find(focused_);
    if (!f || f->kind != Kind::KeyControl || f->keySingle || f->keyIndex <= 0) return;
    float bh = 410.f, header = 50.f;
    if (const Item* border = Find("KeyBorder")) {
        if (border->height > 0.f) bh = border->height;
        if (border->headerHeight > 0.f) header = border->headerHeight;
    }
    const int visible = std::max(1, int((bh - header) / kKeyRowH));
    const int row = f->keyIndex - 1;
    if (row < keyScroll_) keyScroll_ = row;
    else if (row >= keyScroll_ + visible) keyScroll_ = row - visible + 1;
}

void MenuSystem::KeyPressed(int vk) {
    if (capture_.empty() || captureFresh_) return;
    Item* item = Find(capture_);
    if (!item) { capture_.clear(); return; }
    if (vk == 27) { capture_.clear(); return; }           // Escape: keep the old key
    std::string eng;
    if (vk == 8 || vk == 46) eng = "None";                // Backspace, Delete: unbind
    else eng = Input::EngNameForVirtualKey(vk);
    if (eng.empty()) return;                              // a key the engine has no name for

    // MenuScreen::IsKeyInUse: a key already bound to another action moves
    // here, the old binding going to None, so two actions never share one.
    if (eng != "None")
        for (auto& kv : items_) {
            Item& o = kv.second;
            if (o.kind != Kind::KeyControl) continue;
            if (o.keyPrimary == eng) { o.keyPrimary = "None"; o.keyPrimaryText = "None"; }
            if (o.keyAlt == eng)     { o.keyAlt = "None";     o.keyAltText = "None"; }
        }
    if (captureColumn_ == 2) { item->keyAlt = eng;     item->keyAltText = eng; }
    else                     { item->keyPrimary = eng; item->keyPrimaryText = eng; }
    const std::string name = item->name;
    capture_.clear();
    if (playSound_) playSound_("menu/menu/option-accept");
    // The engine's own hook after a control changed; ApplySettings on the
    // way out of the screen is what writes Cfg.
    if (runAction_) runAction_("PainMenu:AfterControlChange('" + name + "')");
}

// ---------------------------------------------------------------- the map
//
// PMENU.SwitchToMap is EngineGame::SwitchMapSelect (0x10074930): the engine
// clears the screen, calls Levels_FillMap() back into Lua - which declares
// every level through PMENU.AddLevelToMap - and takes the screen over with
// its own map. Choosing a level runs Game:LoadLevel('<dir>'), the string
// Painkiller.exe carries.
//
// STAND-IN LAYOUT. The art is the original's - HUD/Map/Map, the cyferka*
// chapter digits (clean/normal/glow/pressed), the karta* cards, the level
// sketches - but where MapSelect's renderer places each piece has not been
// read out of the binary. Chapters run down the left as digit buttons; the
// chapter on show lays its levels out as a row of cards across the middle,
// name beneath. Docs/Reference/Menu.md, "The map".
namespace {
// The layout, measured off a capture of the original map screen in 1024x768
// authoring units. The dial at the left is the chapter selector: a ring with
// a pentagram, Roman numerals I..V in its five triangles (text, the selected
// chapter red), the chapter's digit on the klawisz tab at the top, and the
// crystal in the centre as the button that starts the level. The ring's own
// art carries an arrow either side of the tab; those are the previous and
// next level. The black panel at the right holds the sketch; the okienko
// plate at the bottom left reads "Chapter N / Level N / name"; a pentagram
// marker sits bottom right.
constexpr float kDialCX = 270.f, kDialCY = 278.f, kDialR = 177.f, kNumeralR = 100.f;
constexpr float kOkienkoW = 287.f, kOkienkoH = 121.f;        // the arched plate
constexpr float kDigitW = 50.f, kDigitH = 51.f;              // cyferka
// klawisz1..5, the wedges, each cut to its own size.
constexpr int kWedgeW[5] = {178, 77, 125, 132, 89};
constexpr int kWedgeH[5] = {106, 136, 92, 109, 140};
constexpr float kCrystalW = 131.f, kCrystalH = 124.f;
constexpr float kArrowLX = 120.f, kArrowRX = 360.f, kArrowY = 124.f;   // hit zones
constexpr float kArrowW = 60.f, kArrowH = 44.f;
constexpr float kPanelX = 477.f, kPanelY = 192.f, kPanelW = 320.f, kPanelH = 240.f;
constexpr float kPlateX = 92.f, kPlateY = 526.f, kPlateW = 306.f, kPlateH = 108.f;
constexpr float kPentX = 875.f, kPentY = 630.f, kPentW = 139.f, kPentH = 139.f;
constexpr float kPi = 3.14159265f;
constexpr uint32_t kMapText = 0xff60c0e8u;       // ABGR: warm gold
constexpr uint32_t kMapTextFocus = 0xffffffffu;
constexpr uint32_t kMapTextRed = 0xff2020e0u;
constexpr uint32_t kMapTextLocked = 0xff808080u;
const char* const kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
}  // namespace

int MenuSystem::MapMat(const std::string& name) {
    if (!hud_ || !textures_ || name.empty()) return 0;
    auto it = mapMat_.find(name);
    if (it != mapMat_.end()) return it->second;
    const int m = hud_->CreateMaterial(name, *textures_, "");
    mapMat_[name] = m;
    return m;
}

std::vector<int> MenuSystem::MapChapterLevels(int chapter) const {
    std::vector<int> out;
    for (size_t i = 0; i < mapLevels_.size(); ++i)
        if (mapLevels_[i].chapter == chapter) out.push_back(int(i));
    return out;
}

int MenuSystem::MapChapterCount() const {
    int n = 0;
    for (const MapLevel& l : mapLevels_) n = std::max(n, l.chapter);
    return n;
}

void MenuSystem::EnterMap() {
    ClearScreen();
    mapLevels_.clear();
    if (runAction_) runAction_("Levels_FillMap()");
    mapMode_ = true;
    active_ = true;
    showMouse_ = true;
    if (setPaused_) setPaused_(true);
    // Open on the chapter holding the current level - the one Levels_FillMap
    // marked status 1 - with the focus on it.
    mapChapter_ = mapCurrChapter_ > 0 ? mapCurrChapter_ : 1;
    mapCursor_ = 0;
    for (const MapLevel& l : mapLevels_)
        if (l.status == 1) { mapChapter_ = l.chapter; break; }
    const std::vector<int> levels = MapChapterLevels(mapChapter_);
    for (size_t k = 0; k < levels.size(); ++k)
        if (mapLevels_[size_t(levels[k])].status == 1) mapCursor_ = int(k);
    LogInfo("map: %zu levels in %d chapters, chapter %d on show", mapLevels_.size(),
            MapChapterCount(), mapChapter_);
    // PAINFUL_MAP_PICK=<dir>: a diagnostic that chooses a level the moment the
    // map opens, so the menu-to-level path can be driven without a hand on
    // the mouse. Any level, locked or not.
    if (const char* pick = getenv("PAINFUL_MAP_PICK")) {
        for (size_t i = 0; i < mapLevels_.size(); ++i) {
            if (mapLevels_[i].dir != pick) continue;
            mapChapter_ = mapLevels_[i].chapter;
            const std::vector<int> chapterLevels = MapChapterLevels(mapChapter_);
            for (size_t k = 0; k < chapterLevels.size(); ++k)
                if (chapterLevels[k] == int(i)) mapCursor_ = int(k);
            mapLevels_[i].status = 1;
            MapChoose();
            break;
        }
    }
}

void MenuSystem::AddMapLevel(const MapLevel& level) { mapLevels_.push_back(level); }

void MenuSystem::MapReset() {
    mapLevels_.clear();
    mapCurrChapter_ = mapCurrLevel_ = 0;
    hasPendingLevel_ = false;
}

void MenuSystem::MapSetCurrent(int level, int chapter) {
    mapCurrLevel_ = std::max(0, level);
    mapCurrChapter_ = std::max(0, chapter);
}

void MenuSystem::MapNextLevel() {
    if (mapCurrChapter_ <= 0) { mapCurrChapter_ = 1; mapCurrLevel_ = 1; return; }
    const std::vector<int> levels = MapChapterLevels(mapCurrChapter_);
    if (mapCurrLevel_ < int(levels.size())) ++mapCurrLevel_;
    else { ++mapCurrChapter_; mapCurrLevel_ = 1; }
}

const MenuSystem::MapLevel* MenuSystem::mapCurrent() const {
    if (mapCurrChapter_ <= 0 || mapCurrLevel_ <= 0) return nullptr;
    const std::vector<int> levels = MapChapterLevels(mapCurrChapter_);
    if (size_t(mapCurrLevel_ - 1) >= levels.size()) return nullptr;
    return &mapLevels_[size_t(levels[size_t(mapCurrLevel_ - 1)])];
}

void MenuSystem::MapMoveChapter(int delta) {
    const int n = MapChapterCount();
    if (n <= 0) return;
    mapChapter_ = std::max(1, std::min(n, mapChapter_ + delta));
    mapCursor_ = 0;
}

void MenuSystem::MapMoveCursor(int delta) {
    const int n = int(MapChapterLevels(mapChapter_).size());
    if (n <= 0) return;
    mapCursor_ = std::max(0, std::min(n - 1, mapCursor_ + delta));
}

// A card that is current or finished loads; a locked one does nothing, as in
// the original, where the question-mark sketch is not a button.
void MenuSystem::MapChoose() {
    const std::vector<int> levels = MapChapterLevels(mapChapter_);
    if (mapCursor_ < 0 || size_t(mapCursor_) >= levels.size()) return;
    const MapLevel& l = mapLevels_[size_t(levels[size_t(mapCursor_)])];
    if (l.status != 1 && l.status != 2) return;
    pendingLevel_ = l;
    hasPendingLevel_ = true;
    mapCurrChapter_ = l.chapter;
    mapCurrLevel_ = mapCursor_ + 1;
    mapMode_ = false;
    LogInfo("map: chose %s (%s)", l.dir.c_str(), l.name.c_str());
}

bool MenuSystem::Back() {
    if (!mapMode_) return false;
    mapMode_ = false;
    if (runAction_) runAction_("PainMenu:ActivateScreen(MainMenu)");
    return true;
}

bool MenuSystem::TakePendingLevel(std::string& dir, std::string& name, std::string& sketch) {
    if (!hasPendingLevel_) return false;
    hasPendingLevel_ = false;
    dir = pendingLevel_.dir;
    name = pendingLevel_.name;
    sketch = pendingLevel_.sketch;
    return true;
}

void MenuSystem::UpdateMap(float mouseX, float mouseY, bool clicked) {
    mapHoverChapter_ = 0;
    for (size_t c = 0; c < mapDigitRects_.size(); ++c) {
        if (!mapDigitRects_[c].Contains(mouseX, mouseY)) continue;
        mapHoverChapter_ = int(c) + 1;
        if (clicked && mapChapter_ != int(c) + 1) {
            mapChapter_ = int(c) + 1;
            mapCursor_ = 0;
            if (playSound_) playSound_("menu/menu/option-light-on");
        }
    }
    mapCrystalHover_ = mapCrystalRect_.Contains(mouseX, mouseY);
    if (clicked) {
        if (mapArrowRects_[0].Contains(mouseX, mouseY)) MapMoveCursor(-1);
        else if (mapArrowRects_[1].Contains(mouseX, mouseY)) MapMoveCursor(1);
        else if (mapCrystalHover_) MapChoose();
    }
}

void MenuSystem::DrawMap() {
    const float SX = sx(), SY = sy();
    const int bg = MapMat(blackEdition_ ? "HUD/Map/Map_black" : "HUD/Map/Map");
    if (bg > 0) hud_->Quad(bg, 0.f, 0.f, float(screenW_), float(screenH_), 0xffffffffu);

    mapDigitRects_.clear();
    const std::vector<int> levels = MapChapterLevels(mapChapter_);
    const MapLevel* shown =
        (mapCursor_ >= 0 && size_t(mapCursor_) < levels.size())
            ? &mapLevels_[size_t(levels[size_t(mapCursor_)])]
            : nullptr;
    const bool playable = shown && (shown->status == 1 || shown->status == 2);

    // The arched okienko plate at the top of the ring, the chapter's digit in
    // its cutout.
    {
        const int plate = MapMat("HUD/Map/okienko");
        const float px = (kDialCX - kOkienkoW * 0.5f) * SX, py = (kDialCY - kDialR - 30.f) * SY;
        if (plate > 0) hud_->Quad(plate, px, py, kOkienkoW * SX, kOkienkoH * SY, 0xffffffffu);
        const int digit = MapMat("HUD/Map/cyferka" + std::to_string(mapChapter_) + "_wcisnieta");
        if (digit > 0)
            hud_->Quad(digit, (kDialCX - kDigitW * 0.5f) * SX, py + 28.f * SY, kDigitW * SX,
                       kDigitH * SY, 0xffffffffu);
    }

    // The chapters are the pentagram's five wedges, one klawisz file each
    // with its numeral baked in - clean, glowing under the pointer, pressed
    // and red for the chapter on show - clockwise from the top.
    const int chapters = MapChapterCount();
    for (int c = 1; c <= chapters && c <= 5; ++c) {
        const char* state = (c == mapChapter_)        ? "_wcisniety_czerwony"
                            : (c == mapHoverChapter_) ? "_swiec"
                                                      : "_czysty";
        const int m = MapMat("HUD/Map/klawisz" + std::to_string(c) + state);
        const float angle = -kPi * 0.5f + float(c - 1) * (2.f * kPi / 5.f);
        int w = kWedgeW[c - 1], h = kWedgeH[c - 1];
        Rect r;
        r.w = float(w) * SX;
        r.h = float(h) * SY;
        r.x = (kDialCX + kNumeralR * std::cos(angle)) * SX - r.w * 0.5f;
        r.y = (kDialCY + kNumeralR * std::sin(angle)) * SY - r.h * 0.5f;
        if (m > 0) hud_->Quad(m, r.x, r.y, r.w, r.h, 0xffffffffu);
        mapDigitRects_.push_back(r);
    }

    // The crystal in the centre is the button: lit when the level can be
    // played, brighter under the pointer, dark when it is locked.
    {
        const char* state = !playable ? "krysztal_zgaszony"
                            : mapCrystalHover_ ? "krysztal_swiecacy"
                                               : "krysztal_swiec";
        const int cm = MapMat(std::string("HUD/Map/") + state);
        mapCrystalRect_.x = (kDialCX - kCrystalW * 0.5f) * SX;
        mapCrystalRect_.y = (kDialCY - kCrystalH * 0.5f) * SY;
        mapCrystalRect_.w = kCrystalW * SX;
        mapCrystalRect_.h = kCrystalH * SY;
        if (cm > 0)
            hud_->Quad(cm, mapCrystalRect_.x, mapCrystalRect_.y, mapCrystalRect_.w,
                       mapCrystalRect_.h, 0xffffffffu);
    }
    // The ring's arrows: previous and next level. Their art is in the map.
    mapArrowRects_[0] = {kArrowLX * SX, kArrowY * SY, kArrowW * SX, kArrowH * SY};
    mapArrowRects_[1] = {kArrowRX * SX, kArrowY * SY, kArrowW * SX, kArrowH * SY};

    // The sketch in the panel, and the plate: "Chapter N / Level N / name".
    if (shown) {
        // The sketch is a parchment scrap centred on a transparent 512-square,
        // drawn at that size over the panel's centre: the scrap then fills the
        // black window the way the original shows it.
        const int sm = MapMat(shown->sketch);
        const float side = 450.f;       // the scrap then spans the window, as in the original
        if (sm > 0)
            hud_->Quad(sm, (kPanelX + kPanelW * 0.5f - side * 0.5f) * SX,
                       (kPanelY + kPanelH * 0.5f - side * 0.5f) * SY, side * SX, side * SY,
                       playable ? 0xffffffffu : 0xffa0a0a0u);
        // The info panel is a menu border - dark striped fill, gold frame -
        // not a map texture.
        Item panel;
        panel.kind = Kind::Border;
        panel.x = kPlateX;
        panel.y = kPlateY;
        panel.width = kPlateW;
        panel.height = kPlateH;
        panel.dark = true;
        DrawBorder(panel);
        const float px = kPlateX * SX, py = kPlateY * SY;
        const int size = int(std::lround(26.0 * SY));
        const std::string chapterWord = readText_ ? readText_("Menu.Chapter") : "";
        const std::string levelWord = readText_ ? readText_("Menu.Level") : "";
        const std::string line1 = (chapterWord.empty() ? "Chapter" : chapterWord) + " " +
                                  std::to_string(shown->chapter);
        const std::string line2 = (levelWord.empty() ? "Level" : levelWord) + " " +
                                  std::to_string(mapCursor_ + 1);
        const float lx = px + 22.f * SX;
        hud_->Text("timesbd", size, lx, py + 12.f * SY, line1, kMapText);
        hud_->Text("timesbd", size, lx, py + 40.f * SY, line2, kMapText);
        hud_->Text("timesbd", size, lx, py + 68.f * SY, shown->name,
                   playable ? kMapTextRed : kMapTextLocked);
    }
    const int pent = MapMat("HUD/Map/pentagra_czysty");
    if (pent > 0)
        hud_->Quad(pent, kPentX * SX, kPentY * SY, kPentW * SX, kPentH * SY, 0xffffffffu);
}

void MenuSystem::DrawCursor() {
    if (!showMouse_ || !hud_ || !textures_) return;

    // "HUD/kursor" - Polish for cursor, and held in Engine.dll rather than in
    // any script, which is why no amount of grepping the Lua finds it. The
    // pointer is the engine's, like the rest of the menu.
    if (cursorMaterial_ < 0)
        cursorMaterial_ = hud_->CreateMaterial("HUD/kursor", *textures_, "");
    if (cursorMaterial_ <= 0) return;

    // Drawn at its authored size scaled the way the rest of the interface is,
    // with its hotspot at the top-left corner - which is where a plain arrow
    // cursor points.
    int w = 32, h = 32;
    hud_->MaterialSize(cursorMaterial_, w, h);
    hud_->Quad(cursorMaterial_, mouseX_, mouseY_, float(w) * sy(), float(h) * sy(),
               0xffffffffu);
}

} // namespace painful
