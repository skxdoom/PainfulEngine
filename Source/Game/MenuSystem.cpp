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
    boardMode_ = false;
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
    if (mapMode_ || boardMode_) return;
    if (focused_.empty()) MoveFocus(1);
}

void MenuSystem::NavUp() {
    if (boardMode_) return;
    if (mapMode_) { MapMoveChapter(-1); return; }
    MoveFocus(-1);
    EnsureKeyRowVisible();
}
void MenuSystem::NavDown() {
    if (boardMode_) return;
    if (mapMode_) { MapMoveChapter(1); return; }
    MoveFocus(1);
    EnsureKeyRowVisible();
}

void MenuSystem::NavAdjust(int direction) {
    if (boardMode_) return;
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
    if (boardMode_) { LeaveBoard(); return; }
    if (mapMode_) { MapChoose(); return; }
    if (Item* item = Find(focused_)) Choose(*item);
}

void MenuSystem::Update(float mouseX, float mouseY, bool clicked) {
    if (!active_) return;
    mouseX_ = mouseX;
    mouseY_ = mouseY;
    if (boardMode_) { UpdateBoard(mouseX, mouseY, clicked); return; }
    if (mapMode_) { UpdateMap(mouseX, mouseY, clicked); return; }
    // The click that opened a capture must not also be the key it binds.
    captureFresh_ = false;

    // A slider follows a held button across its bar: press anywhere on the
    // bar and the knob goes there, drag and it follows. Dragging keeps hold
    // of the slider it started on even when the pointer strays off the bar.
    if (showMouse_ && capture_.empty()) {
        if (!mouseDown_) dragging_.clear();
        Item* drag = dragging_.empty() ? nullptr : Find(dragging_);
        if (mouseDown_ && !drag) {
            for (Item* item : Ordered()) {
                if (item->kind != Kind::Slider || !item->visible || item->disabled) continue;
                if (item->barW <= 0.f) continue;
                if (mouseX < item->barX - 20.f || mouseX > item->barX + item->barW + 20.f) continue;
                if (mouseY < item->barY || mouseY > item->barY + item->barH) continue;
                dragging_ = item->name;
                focused_ = item->name;
                drag = item;
                break;
            }
        }
        if (drag && drag->barW > 0.f) {
            const double t = std::max(0.f, std::min(1.f, (mouseX - drag->barX) / drag->barW));
            double v = drag->minValue + t * (drag->maxValue - drag->minValue);
            if (!drag->isFloat) v = std::round(v);
            drag->value = std::max(drag->minValue, std::min(drag->maxValue, v));
        }
    }

    // The mouse wins over the keyboard whenever it is over a row: hover moves
    // focus, so the description text and the highlight follow the pointer.
    if (showMouse_ && capture_.empty() && dragging_.empty()) {
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
    if (boardMode_) {
        DrawBoard();
        if (showMouse_) DrawCursor();
        return;
    }
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
        // A static text with a rectangle wraps into it, its lines centred:
        // the yes/no prompt's question, three lines across the box.
        if (item->kind == Kind::StaticText && item->hasTextRect) {
            const int size = int(std::lround(double(item->fontBigSize) * double(sy())));
            const float x1 = item->textRect[0] * sx(), x2 = item->textRect[2] * sx();
            const float width = x2 - x1;
            const float lineH = hud_->TextHeight(item->fontBig, size);
            std::vector<std::string> lines;
            std::string line, word;
            const auto flush = [&]() { lines.push_back(line); line.clear(); };
            for (size_t i = 0; i <= item->text.size(); ++i) {
                const char ch = i < item->text.size() ? item->text[i] : ' ';
                if (ch == ' ' || ch == '\n' || i == item->text.size()) {
                    if (!word.empty()) {
                        const std::string trial = line.empty() ? word : line + " " + word;
                        if (!line.empty() && hud_->TextWidth(item->fontBig, size, trial) > width)
                            flush();
                        line = line.empty() ? word : line + " " + word;
                        word.clear();
                    }
                    if (ch == '\n') flush();
                } else {
                    word += ch;
                }
            }
            if (!line.empty()) flush();
            float y = item->textRect[1] * sy();
            const uint32_t colour = ArgbToAbgr(item->textColor);
            for (const std::string& l : lines) {
                const float w = hud_->TextWidth(item->fontBig, size, l);
                hud_->Text(item->fontBig, size, x1 + (width - w) * 0.5f, y, l, colour,
                           FontTexture(*item, true));
                y += lineH;
            }
            item->hitW = item->hitH = 0.f;
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

        // A CENTRED row with a value is one string, "label: value", centred
        // as a whole - "Speakers setup: Two Speakers" on the Sound screen -
        // rather than a label here and a value in a column that does not
        // exist for it. A centred checkbox centres box and label as a pair.
        const bool centredRow =
            item->x < 0.f && (item->align == kAlignCenter || item->align == kAlignNone);
        std::string label = item->text;
        bool inlineValue = false;
        if (centredRow && (item->kind == Kind::TextButtonEx || item->kind == Kind::NumRange)) {
            label += ": " + ValueString(*item);
            inlineValue = true;
        }
        float labelX = x;
        if (inlineValue)
            labelX = (float(screenW) - hud_->TextWidth(item->fontBig, size, label)) * 0.5f;
        // A checkbox is its box, then its label - not a label with "On"
        // beside it.
        if (item->kind == Kind::Checkbox) {
            const float boxW = 36.f * sx(), gap = 12.f * sx();
            float bx = x;
            if (centredRow) bx = (float(screenW) - (boxW + gap + w)) * 0.5f;
            DrawCheckbox(*item, bx, y, size);
            labelX = bx + boxW + gap;
        }
        hud_->Text(item->fontBig, size, labelX, y, label, ArgbToAbgr(colour),
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
        } else if (HasValue(item->kind) && !inlineValue) {
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

// The words a value shows as, for the rows that write it into their label.
std::string MenuSystem::ValueString(const Item& item) const {
    switch (item.kind) {
    case Kind::Checkbox: return item.value != 0.0 ? onText_ : offText_;
    case Kind::TextButtonEx: return item.valueText;
    case Kind::NumRange: return std::to_string(int(item.value));
    default: return {};
    }
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
    // The bar runs through the middle of the text's line, not of its point
    // size: the line is taller than the size and the glyphs sit low in it.
    const float cy = y + hud_->TextHeight(item.fontBig, size) * 0.5f;
    const double span = item.maxValue - item.minValue;
    const double t = span > 0.0 ? (item.value - item.minValue) / span : 0.0;
    // For the mouse: the bar and its arrows, a line's height tall.
    const_cast<Item&>(item).barX = barLeft;
    const_cast<Item&>(item).barW = barRight - barLeft;
    const_cast<Item&>(item).barY = cy - 20.f * SY;
    const_cast<Item&>(item).barH = 40.f * SY;

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

// The menu's checkbox is HUD/ikonki/checkbox_pusty (empty) and
// checkbox_zaznaczony (ticked): the bevelled bronze box with its red tick,
// as one piece. (HUD/ChkChecked is the HUD's own tick, not the menu's.)
void MenuSystem::DrawCheckbox(const Item& item, float x, float y, int size) {
    const int box = MapMat(item.value != 0.0 ? "HUD/ikonki/checkbox_zaznaczony"
                                             : "HUD/ikonki/checkbox_pusty");
    if (box <= 0) return;
    // The file is 55 x 51; the original draws it about 36 x 33 authoring
    // units, a line's height, measured off the Sound screen.
    const float bw = 36.f * sx(), bh = 33.f * sy();
    const float th = hud_->TextHeight(item.fontBig, size);
    hud_->Quad(box, x, y + th * 0.5f - bh * 0.5f, bw, bh,
               item.disabled ? 0xffa0a0a0u : 0xffffffffu);
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
constexpr float kRingR = 163.f;                              // the band with the padlocks
constexpr float kPlateR = 150.f;                             // the selector plate's centre
constexpr float kCardX = 750.f, kCardY = 597.f, kCardW = 82.f, kCardH = 125.f;
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
constexpr float kPentX = 824.f, kPentY = 588.f, kPentW = 139.f, kPentH = 139.f;
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
    if (runAction_) {
        runAction_("Levels_FillMap()");
        // The cards' pictures by index, for the map's card button: the
        // level's cardIndex names an entry of MagicCards.
        runAction_("__pkCardTex = {} if MagicCards then "
                   "for i,o in MagicCards.timeCards do __pkCardTex['c'..o.index] = o.texture end "
                   "for i,o in MagicCards.permCards do __pkCardTex['c'..o.index] = o.texture end end");
    }
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
    // PAINFUL_MAP_CURSOR=<k>: a diagnostic that puts the focus on the k-th
    // level of the chapter on show, for captures of the ring.
    if (const char* cur = getenv("PAINFUL_MAP_CURSOR")) MapMoveCursor(std::atoi(cur));
    // The plate starts ON the chosen level; it slides only for later moves.
    plateAngle_ = float(mapCursor_) * (kPi / 3.f);
    plateClock_ = std::chrono::steady_clock::now();
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
    // On the map the "current" level is the one under focus: that is what
    // Hud_RenderLevelStats asks MapGetCurrLevelName for when the plate is
    // hovered. Off the map it is the marker.
    int chapter = mapCurrChapter_, level = mapCurrLevel_;
    if (mapMode_) { chapter = mapChapter_; level = mapCursor_ + 1; }
    if (chapter <= 0 || level <= 0) return nullptr;
    const std::vector<int> levels = MapChapterLevels(chapter);
    if (size_t(level - 1) >= levels.size()) return nullptr;
    return &mapLevels_[size_t(levels[size_t(level - 1)])];
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
    // Round the ring: past the last level comes the first.
    mapCursor_ = ((mapCursor_ + delta) % n + n) % n;
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
    if (boardMode_) { LeaveBoard(); return true; }
    if (mapMode_) {
        mapMode_ = false;
        if (runAction_) runAction_("PainMenu:ActivateScreen(MainMenu)");
        return true;
    }
    // On an ordinary screen Escape is the Back button: PainMenu adds it as
    // the item "BackButton" carrying the screen's backAction, which on the
    // option screens applies the settings on the way out. A screen without
    // one - the main menu, a yes/no prompt - answers false and the app
    // decides (close onto the game, or nothing with no level up).
    if (const Item* back = Find("BackButton"))
        if (!back->action.empty()) {
            pending_.push_back(back->action);
            return true;
        }
    return false;
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
    mapLevelHover_ = -1;
    for (size_t k = 0; k < mapLevelRects_.size(); ++k)
        if (mapLevelRects_[k].Contains(mouseX, mouseY)) mapLevelHover_ = int(k);
    mapPlateHover_ = mapPlateRect_.Contains(mouseX, mouseY);
    mapCardHover_ = mapCardRect_.Contains(mouseX, mouseY);
    mapPentHover_ = mapPentRect_.Contains(mouseX, mouseY);
    if (clicked) {
        if (mapArrowRects_[0].Contains(mouseX, mouseY)) MapMoveCursor(-1);
        else if (mapArrowRects_[1].Contains(mouseX, mouseY)) MapMoveCursor(1);
        else if (mapCrystalHover_) MapChoose();
        else if (mapCardHover_) EnterBoard();
        else if (mapPentHover_) Back();
        else
            for (size_t k = 0; k < mapLevelRects_.size(); ++k)
                if (mapLevelRects_[k].Contains(mouseX, mouseY) && mapCursor_ != int(k)) {
                    mapCursor_ = int(k);
                    if (playSound_) playSound_("menu/menu/option-light-on");
                }
    }
}

void MenuSystem::DrawMap() {
    const float SX = sx(), SY = sy();
    mapDigitRects_.clear();
    const std::vector<int> levels = MapChapterLevels(mapChapter_);
    const MapLevel* shown =
        (mapCursor_ >= 0 && size_t(mapCursor_) < levels.size())
            ? &mapLevels_[size_t(levels[size_t(mapCursor_)])]
            : nullptr;
    const bool playable = shown && (shown->status == 1 || shown->status == 2);

    // The sketch goes UNDER the map. Map.dds is DXT3 and its black window is
    // transparent (alpha 0), so what is drawn before it shows through the
    // window and stops under the opaque straps that cross it - which is how
    // the original's scrap sits behind the leather. The scrap's picture is
    // the upper left of its texture; 474 units wide from (383,145) it fills
    // the window as in a full-screen 3440x1440 pair.
    if (shown) {
        const int sm = MapMat(shown->sketch);
        const float side = 474.f;
        if (sm > 0)
            hud_->Quad(sm, 383.f * SX, 145.f * SY, side * SX, side * SY,
                       playable ? 0xffffffffu : 0xffa0a0a0u);
    }
    const int bg = MapMat(blackEdition_ ? "HUD/Map/Map_black" : "HUD/Map/Map");
    if (bg > 0) hud_->Quad(bg, 0.f, 0.f, float(screenW_), float(screenH_), 0xffffffffu);

    // The levels around the ring, thirty degrees apart clockwise from the
    // top, level 1 at the top. The ring's own art holds a padlock at every
    // spot: an open level gets its digit drawn over the lock, a locked one
    // keeps the lock. The arched okienko plate is the SELECTOR - it sits on
    // the chosen level's spot, turned to follow the ring, with the digit in
    // its cutout (so with level 1 chosen it is the tab at the top) - and the
    // spot under the pointer shows the plate too, as the original's hover.
    mapLevelRects_.clear();
    {
        const int plate = MapMat("HUD/Map/okienko");
        const float pw = kOkienkoW * 0.83f, ph = kOkienkoH * 0.83f;
        // SIX fixed slots sixty degrees apart, level 1 at the top; a chapter
        // has four to six levels and the slots past its count stay locked.
        const float step = 2.f * kPi / 6.f;

        // The selector plate SLIDES around the ring from the level it was
        // on to the one chosen, the short way round, at about a third of a
        // second per slot.
        {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::min(0.1f, std::chrono::duration<float>(now - plateClock_).count());
            plateClock_ = now;
            const float target = float(mapCursor_) * step;
            float diff = target - plateAngle_;
            while (diff > kPi) diff -= 2.f * kPi;
            while (diff < -kPi) diff += 2.f * kPi;
            const float maxStep = 3.5f * dt;
            if (std::fabs(diff) <= maxStep) plateAngle_ = target;
            else plateAngle_ += diff > 0.f ? maxStep : -maxStep;
        }
        if (plate > 0) {
            // The plate is turned in AUTHORING space, where the ring is a
            // circle, and its four corners are then scaled to the screen one
            // by one. The screen scales the two axes differently on a
            // widescreen window, so a rectangle rotated on screen keeps its
            // sides at the wrong lengths at every angle but straight up and
            // down - the plate on the two o'clock slot came out squat while
            // the top one was right.
            const float a = plateAngle_;
            const float cx = kDialCX + kPlateR * std::sin(a);
            const float cy = kDialCY - kPlateR * std::cos(a);
            const float c = std::cos(a), s = std::sin(a);
            const float local[4][2] = {{-pw * 0.5f, -ph * 0.5f}, {pw * 0.5f, -ph * 0.5f},
                                       {pw * 0.5f, ph * 0.5f},   {-pw * 0.5f, ph * 0.5f}};
            float xy[8];
            for (int i = 0; i < 4; ++i) {
                const float ax = cx + local[i][0] * c - local[i][1] * s;
                const float ay = cy + local[i][0] * s + local[i][1] * c;
                xy[i * 2] = ax * SX;
                xy[i * 2 + 1] = ay * SY;
            }
            hud_->QuadCorners(plate, xy, 0xffffffffu);
        }
        for (size_t k = 0; k < 6; ++k) {
            const bool exists = k < levels.size();
            const MapLevel* l = exists ? &mapLevels_[size_t(levels[k])] : nullptr;
            const bool open = l && (l->status == 1 || l->status == 2);
            const bool chosen = int(k) == mapCursor_;
            const bool hover = int(k) == mapLevelHover_;
            const float a = float(k) * step;
            const float dx = std::sin(a), dy = -std::cos(a);
            Rect r;
            r.w = kDigitW * SX;
            r.h = kDigitH * SY;
            r.x = (kDialCX + kRingR * dx) * SX - r.w * 0.5f;
            r.y = (kDialCY + kRingR * dy) * SY - r.h * 0.5f;
            if (open) {
                // The pressed digit ships for 1..6 only; the lit one stands in.
                int m = chosen ? MapMat("HUD/Map/cyferka" + std::to_string(k + 1) + "_wcisnieta") : 0;
                if (m <= 0)
                    m = MapMat("HUD/Map/cyferka" + std::to_string(k + 1) +
                               ((chosen || hover) ? "_swiec" : "_normalna"));
                if (m > 0) hud_->Quad(m, r.x, r.y, r.w, r.h, 0xffffffffu);
            } else {
                // A locked slot - or one past the chapter's count - wears the
                // padlock: HUD/Map/question, the same 50 x 51 as a digit.
                const int lock = MapMat("HUD/Map/question");
                if (lock > 0) hud_->Quad(lock, r.x, r.y, r.w, r.h, 0xffffffffu);
            }
            r.x -= 8.f * SX; r.y -= 8.f * SY; r.w += 16.f * SX; r.h += 16.f * SY;
            // A slot past the chapter's count is not a level: no hit area.
            if (!exists) r.w = r.h = 0.f;
            mapLevelRects_.push_back(r);
        }
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
        mapPlateRect_ = {px, py, kPlateW * SX, kPlateH * SY};

        // The tarot card in its slot, bottom right: the karta art as shipped
        // (glowing under the pointer), which is the whole card, picture and
        // all - the original shows the same card whatever the level, and
        // laying the level's own picture over it put a second image on top.
        // It opens the board.
        const int frame = MapMat(mapCardHover_ ? "HUD/Map/karta_swiec" : "HUD/Map/karta_czysta");
        mapCardRect_ = {kCardX * SX, kCardY * SY, kCardW * SX, kCardH * SY};
        if (frame > 0)
            hud_->Quad(frame, mapCardRect_.x, mapCardRect_.y, mapCardRect_.w, mapCardRect_.h,
                       0xffffffffu);
    }
    // The pentagram marker is the way back to the main menu.
    const int pent = MapMat(mapPentHover_ ? "HUD/Map/pentagra_swiec" : "HUD/Map/pentagra_czysty");
    mapPentRect_ = {kPentX * SX, kPentY * SY, kPentW * SX, kPentH * SY};
    if (pent > 0)
        hud_->Quad(pent, mapPentRect_.x, mapPentRect_.y, mapPentRect_.w, mapPentRect_.h,
                   0xffffffffu);

    // Hovering the plate shows the level's statistics board - the same
    // Hud_RenderLevelStats the engine calls for the in-game Tab screen. It
    // draws through HUD.* into the batch that is open right now.
    if (mapPlateHover_ && shown && runAction_)
        runAction_("if Hud_RenderLevelStats then Hud_RenderLevelStats() end");
}

// ---------------------------------------------------------------- the board
//
// PMENU.SwitchToBoard is EngineGame::SwitchMagicBoard. The engine calls
// MagicBoard:Setup() back into Lua, which declares four slot rows through
// MBOARD.SetupSlots / SetSlotPosition (the top row of small permanent-card
// slots, the two large selected-permanent slots, the three large selected-
// tarot slots, the bottom row of small tarot slots - HUD/Board/board is drawn
// to fit them) and every card through MBOARD.AddCard with whether the player
// owns it and whether it is selected. Cards owned sit in their kind's small
// row; selected ones move to the large slots of their kind. Leaving runs
// MagicBoard_UpdateCardsStatus(), which reads the result back through
// IsCardInSlot and writes Game.CardsSelected.
//
// STAND-IN: a click moves a card between its row and the first free large
// slot of its kind, and the board's gold cost of equipping (MagicBoard::
// GetCash / SetCash, the counter beside the crystal) is not charged. The
// crystal accepts and returns to the map; so does Escape.
namespace {
constexpr float kBoardCrystalX = 490.f, kBoardCrystalY = 195.f;
constexpr float kBoardCrystalW = 173.f, kBoardCrystalH = 132.f;
constexpr float kBoardZoomX = 250.f, kBoardZoomY = 250.f, kBoardZoomSide = 220.f;
constexpr int kTimeAll = 0, kPermAll = 1, kTimeSel = 2, kPermSel = 3;
}  // namespace

void MenuSystem::EnterBoard() {
    ClearScreen();
    for (BoardSlots& s : boardSlots_) s = BoardSlots();
    boardCards_.clear();
    boardHover_ = -1;
    if (runAction_) runAction_("if MagicBoard and MagicBoard.Setup then MagicBoard:Setup() end");
    boardMode_ = true;
    active_ = true;
    showMouse_ = true;
    if (setPaused_) setPaused_(true);
    LogInfo("board: %zu cards, rows %d/%d/%d/%d", boardCards_.size(), boardSlots_[0].count,
            boardSlots_[1].count, boardSlots_[2].count, boardSlots_[3].count);
}

void MenuSystem::LeaveBoard() {
    if (!boardMode_) return;
    boardMode_ = false;
    if (runAction_)
        runAction_("if MagicBoard_UpdateCardsStatus then MagicBoard_UpdateCardsStatus() end");
    EnterMap();
}

void MenuSystem::BoardSetupSlots(int type, int count, float y, float w, float h, float space) {
    if (type < 0 || type > 3) return;
    BoardSlots& s = boardSlots_[type];
    s.count = std::max(0, count);
    s.y = y; s.w = w; s.h = h; s.space = space;
    s.x.assign(size_t(s.count), 0.f);
    for (int i = 0; i < s.count; ++i) s.x[size_t(i)] = float(i) * space;
}

void MenuSystem::BoardSetSlotX(int type, int slot, float x) {
    if (type < 0 || type > 3) return;
    BoardSlots& s = boardSlots_[type];
    if (slot >= 0 && size_t(slot) < s.x.size()) s.x[size_t(slot)] = x;
}

void MenuSystem::BoardAddCard(const BoardCard& card) { boardCards_.push_back(card); }

std::vector<const MenuSystem::BoardCard*> MenuSystem::BoardCardsOfType(int type) const {
    std::vector<const BoardCard*> out;
    for (const BoardCard& c : boardCards_)
        if (c.type == type) out.push_back(&c);
    return out;
}

bool MenuSystem::BoardCardInSlot(int type, int index) const {
    if (index < 0) return false;
    if (type == kTimeAll || type == kPermAll) {
        const std::vector<const BoardCard*> cards = BoardCardsOfType(type == kTimeAll ? 1 : 2);
        return size_t(index) < cards.size() && !cards[size_t(index)]->selected;
    }
    int filled = 0;
    for (const BoardCard* c : BoardCardsOfType(type == kTimeSel ? 1 : 2))
        if (c->selected) ++filled;
    return index < filled;
}

void MenuSystem::UpdateBoard(float mouseX, float mouseY, bool clicked) {
    boardHover_ = -1;
    for (const auto& rc : boardCardRects_)
        if (rc.first.Contains(mouseX, mouseY)) boardHover_ = rc.second;
    boardCrystalHover_ = boardCrystalRect_.Contains(mouseX, mouseY);
    if (!clicked) return;
    if (boardCrystalHover_) { LeaveBoard(); return; }
    if (boardHover_ < 0 || size_t(boardHover_) >= boardCards_.size()) return;
    BoardCard& card = boardCards_[size_t(boardHover_)];
    if (!card.available) return;
    if (card.selected) {
        card.selected = false;
    } else {
        // Only as many as the large slots of its kind hold.
        const int cap = boardSlots_[card.type == 1 ? kTimeSel : kPermSel].count;
        int used = 0;
        for (const BoardCard* c : BoardCardsOfType(card.type))
            if (c->selected) ++used;
        if (used < cap) card.selected = true;
    }
    if (playSound_) playSound_("menu/menu/option-accept");
}

void MenuSystem::DrawBoard() {
    const float SX = sx(), SY = sy();
    const int bg = MapMat(blackEdition_ ? "HUD/Board/board_black" : "HUD/Board/board");
    if (bg > 0) hud_->Quad(bg, 0.f, 0.f, float(screenW_), float(screenH_), 0xffffffffu);
    boardCardRects_.clear();

    // A card's picture is square; it sits in the top of its slot, the slot
    // being as wide as the picture.
    const auto drawCard = [&](const BoardCard& c, int index, const BoardSlots& s, int slot) {
        if (slot < 0 || size_t(slot) >= s.x.size()) return;
        Rect r{s.x[size_t(slot)] * SX, s.y * SY, s.w * SX, s.h * SY};
        const int art = MapMat(c.texture);
        const uint32_t tint = (index == boardHover_) ? 0xffffffffu : 0xffd8d8d8u;
        if (art > 0) hud_->Quad(art, r.x, r.y, r.w, r.w, tint);
        boardCardRects_.push_back({r, index});
    };
    for (int kind = 1; kind <= 2; ++kind) {
        const BoardSlots& all = boardSlots_[kind == 1 ? kTimeAll : kPermAll];
        const BoardSlots& sel = boardSlots_[kind == 1 ? kTimeSel : kPermSel];
        int ordinal = 0, chosen = 0;
        for (size_t i = 0; i < boardCards_.size(); ++i) {
            const BoardCard& c = boardCards_[i];
            if (c.type != kind) continue;
            const int slot = ordinal++;
            if (!c.available) continue;
            if (c.selected) drawCard(c, int(i), sel, chosen++);
            else drawCard(c, int(i), all, slot);
        }
    }

    // The crystal: accept and back to the map.
    const int crystal = MapMat(boardCrystalHover_ ? "HUD/Board/krysztal_board_swiec"
                                                  : "HUD/Board/krysztal_board_czysty");
    boardCrystalRect_ = {kBoardCrystalX * SX, kBoardCrystalY * SY, kBoardCrystalW * SX,
                         kBoardCrystalH * SY};
    if (crystal > 0)
        hud_->Quad(crystal, boardCrystalRect_.x, boardCrystalRect_.y, boardCrystalRect_.w,
                   boardCrystalRect_.h, 0xffffffffu);

    // The card under the pointer, large, with its name, cost and text.
    if (boardHover_ >= 0 && size_t(boardHover_) < boardCards_.size()) {
        const BoardCard& c = boardCards_[size_t(boardHover_)];
        const int big = MapMat(c.bigImage.empty() ? c.texture : c.bigImage);
        const float zx = kBoardZoomX * SX, zy = kBoardZoomY * SY, zs = kBoardZoomSide;
        if (big > 0) hud_->Quad(big, zx, zy, zs * SX, zs * SY, 0xffffffffu);
        const int size = int(std::lround(24.0 * SY));
        const int small = int(std::lround(18.0 * SY));
        hud_->Text("timesbd", size, zx, zy + (zs + 6.f) * SY, c.name, kMapTextRed);
        hud_->Text("timesbd", small, zx, zy + (zs + 34.f) * SY, std::to_string(c.cost), kMapText);
        hud_->Text("timesbd", small, zx, zy + (zs + 56.f) * SY, c.desc, kMapText);
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
