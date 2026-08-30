#include "MenuSystem.h"

#include "../Core/Log.h"
#include "../Render/HudRenderer.h"
#include "../Render/TextureCache.h"

#include <algorithm>
#include <cmath>

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
    if (backgroundMaterial_ > 0 && hud_) hud_->ReleaseMaterial(backgroundMaterial_);
    backgroundMaterial_ = 0;
    background_.clear();
    // The cursor is deliberately NOT released here: it belongs to the menu
    // rather than to a screen, and survives every screen change.
}

void MenuSystem::SetBackground(const std::string& material, int type) {
    backgroundType_ = type;
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
    if (focused_.empty()) MoveFocus(1);
}

void MenuSystem::NavUp() { MoveFocus(-1); }
void MenuSystem::NavDown() { MoveFocus(1); }

void MenuSystem::NavAdjust(int direction) {
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
    if (Item* item = Find(focused_)) Choose(*item);
}

void MenuSystem::Update(float mouseX, float mouseY, bool clicked) {
    if (!active_) return;
    mouseX_ = mouseX;
    mouseY_ = mouseY;

    // The mouse wins over the keyboard whenever it is over a row: hover moves
    // focus, so the description text and the highlight follow the pointer.
    if (showMouse_) {
        for (Item* item : Ordered()) {
            if (!Focusable(item->kind) || !item->visible || item->disabled) continue;
            if (item->hitW <= 0.f || item->hitH <= 0.f) continue;
            if (mouseX < item->hitX || mouseX > item->hitX + item->hitW) continue;
            if (mouseY < item->hitY || mouseY > item->hitY + item->hitH) continue;

            if (focused_ != item->name) {
                focused_ = item->name;
                if (playSound_ && !item->sndLightOn.empty()) playSound_(item->sndLightOn);
            }
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

    // The background covers the whole screen regardless of its aspect: it is
    // artwork, not a layout element.
    if (backgroundMaterial_ > 0)
        hud_->Quad(backgroundMaterial_, 0.f, 0.f, float(screenW), float(screenH), 0xffffffffu);

    // Borders first, whatever order they were declared in: they are the panels
    // everything else sits ON, so a border added after its contents would
    // otherwise paint over them.
    for (Item* item : Ordered())
        if (item->kind == Kind::Border && item->visible) DrawBorder(*item);

    const Item* focusedItem = nullptr;

    for (Item* item : Ordered()) {
        if (item->kind == Kind::Border) continue;
        if (!item->visible) {
            item->hitW = item->hitH = 0.f;
            continue;
        }
        const bool isFocused = (item->name == focused_) && !item->disabled &&
                               Focusable(item->kind);
        if (isFocused) focusedItem = item;

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
        if (isFocused && !item->itemBG.empty() && item->hitW > 0.f)
            DrawItemBG(*item, item->hitX, y, item->hitW, float(size) * 1.4f);

        hud_->Text(item->fontBig, size, x, y, item->text, ArgbToAbgr(colour),
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
        if (HasValue(item->kind)) {
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

    // The caps scale with the row height so the bevel keeps its proportions.
    int lw = 0, lh = 0, rw = 0, rh = 0;
    hud_->MaterialSize(item.itemBGMat[0], lw, lh);
    hud_->MaterialSize(item.itemBGMat[2], rw, rh);
    const float scale = lh > 0 ? h / float(lh) : 1.f;
    const float capL = float(lw) * scale, capR = float(rw) * scale;
    const float middle = w - capL - capR;

    hud_->Quad(item.itemBGMat[0], x, y, capL, h, 0xffffffffu);
    if (middle > 0.f && item.itemBGMat[1] > 0)
        hud_->Quad(item.itemBGMat[1], x + capL, y, middle, h, 0xffffffffu);
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
        // TXT.On / TXT.Off are Texts[4] and [5], but the menu never asks the
        // engine to localise a checkbox - it just draws the state - so the
        // words come from the language table the same way anything else does.
        hud_->Text(item.fontBig, size, x, y, item.value != 0.0 ? onText_ : offText_, abgr);
        break;

    case Kind::Slider: {
        // A filled bar with the value beside it. The original draws a textured
        // track and a grip (HUD/blachy_menu); this is the same geometry
        // without the art, which lands in stage 3 with MenuItemBorder.
        const float barW = 200.f * sx();
        const float barH = 6.f * sy();
        const float by = y + float(size) * 0.45f;
        const double span = item.maxValue - item.minValue;
        const double t = span > 0.0 ? (item.value - item.minValue) / span : 0.0;

        hud_->Quad(0, x, by, barW, barH, ArgbToAbgr(item.disabledColor));
        hud_->Quad(0, x, by, barW * float(t), barH, abgr);

        char buf[32];
        if (item.isFloat) snprintf(buf, sizeof buf, "%.2f", item.value);
        else              snprintf(buf, sizeof buf, "%d", int(item.value));
        hud_->Text(item.fontBig, size, x + barW + 16.f * sx(), y, buf, abgr);
        break;
    }

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
