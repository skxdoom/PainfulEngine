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
    runAction_("PainMenu:OpenMenu(); PainMenu:ActivateScreen(PainMenu.mainScreen)");
}

void MenuSystem::Close() {
    if (!active_) return;
    if (runAction_) runAction_("PainMenu:CloseMenu()");
    ClearScreen();
    active_ = false;
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
    std::vector<Item*> all = Ordered();
    std::vector<Item*> reachable;
    for (Item* i : all)
        if (i->kind == Kind::TextButton && i->visible && !i->disabled)
            reachable.push_back(i);
    if (reachable.empty()) return;

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

void MenuSystem::NavUp() { MoveFocus(-1); }
void MenuSystem::NavDown() { MoveFocus(1); }

void MenuSystem::NavActivate() {
    if (Item* item = Find(focused_)) Choose(*item);
}

void MenuSystem::Update(float mouseX, float mouseY, bool clicked) {
    if (!active_) return;

    // The mouse wins over the keyboard whenever it is over a row: hover moves
    // focus, so the description text and the highlight follow the pointer.
    if (showMouse_) {
        for (Item* item : Ordered()) {
            if (item->kind != Kind::TextButton || !item->visible || item->disabled) continue;
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

    const Item* focusedItem = nullptr;

    for (Item* item : Ordered()) {
        if (!item->visible) {
            item->hitW = item->hitH = 0.f;
            continue;
        }
        const bool isFocused = (item->name == focused_) && !item->disabled &&
                               item->kind == Kind::TextButton;
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
        float x;
        if (item->x < 0.f || item->align == kAlignCenter)
            x = (float(screenW) - w) * 0.5f;
        else if (item->align == kAlignRight)
            x = item->x * sx() - w;
        else
            x = item->x * sx();
        const float y = item->y * sy();

        uint32_t colour = item->textColor;
        if (item->disabled)     colour = item->disabledColor;
        else if (isFocused)     colour = item->underMouseColor;

        hud_->Text(item->fontBig, size, x, y, item->text, ArgbToAbgr(colour));

        item->hitX = x;
        item->hitY = y;
        item->hitW = w;
        item->hitH = h;
    }

    // The focused row's blurb, centred near the bottom - where the shipped
    // menu puts it.
    if (focusedItem && !focusedItem->desc.empty()) {
        const int size = int(std::lround(double(focusedItem->fontSmallSize) * double(sy())));
        hud_->Text(focusedItem->fontSmall, size, -1.f, float(screenH) - 80.f * sy(),
                   focusedItem->desc, ArgbToAbgr(focusedItem->descColor));
    }
}

} // namespace painful
