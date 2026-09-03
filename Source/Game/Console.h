#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

class HudRenderer;
class MenuSystem;

// The drop-down console: `~` in the original, the CONSOLE natives, and the
// on-screen message strip that cheat confirmations and chat land on.
//
// The state and the key vocabulary are Engine.dll's Console (embedded in the
// HUD object at renderer+0x5d6bd0; constructor 0x10029f10, key handler
// 0x1002aef0, Activate 0x10029860). What it does with a line is the SCRIPTS':
// Enter hands the text to Hud_OnConsoleCommand (or Hud_OnSayToAll /
// Hud_OnSayToTeam in the chat modes) and Tab to Hud_OnConsoleTab, and
// Console.lua dispatches from there. Docs/Reference/Console.md.
class Console {
public:
    // ConsoleMode in Definitions.lua.
    enum Mode { kFull = 0, kSayAll = 1, kSayTeam = 2 };

    // CONSOLE.AddMessage's default colour, ARGB: the tan the whole interface
    // prints in (0x10029d70 reads the second argument with this default).
    static constexpr uint32_t kMessageColor = 0xFFFFBA7Au;

    // CONSOLE.Activate(on, mode). Opening resets the input line, the scroll
    // and the panel size for the mode; it does not clear the log.
    void Activate(bool on, int mode);
    bool active() const { return active_; }
    int mode() const { return mode_; }
    // True once after each Activate(true): the game loop releases the keys the
    // player was holding, as the original zeroes its action masks.
    bool TakeOpened() {
        const bool o = opened_;
        opened_ = false;
        return o;
    }

    // CONSOLE.AddMessage / Print: one line into the log AND the on-screen
    // strip. A '\n' splits into several lines, as the original's strchr does.
    void AddMessage(const std::string& text, uint32_t argb = kMessageColor);

    // CONSOLE.SetCurrentText / GetCurrentText: the input line.
    void SetCurrentText(const std::string& text);
    const std::string& currentText() const { return text_; }
    // CONSOLE.GetCursorPos: the SCROLL position, 1 = newest line at the bottom.
    int scrollPos() const { return scroll_; }

    // CONSOLE.SetFont(name, size) and the SetMPMsg* trio HUD:LoadData calls.
    void SetFont(const std::string& name, int size);
    void SetMessageColor(int r, int g, int b);
    void SetMessagePosition(float x, float y) { msgX_ = x; msgY_ = y; }
    void SetMessageFont(const std::string& name, int size);

    // --- input, while active ------------------------------------------------
    // One key press, Windows virtual-key code, repeats included. Everything
    // the original's handler (0x1002aef0) understands: PageUp/PageDown
    // scroll, Up/Down walk the history, Left/Right move the caret, Enter
    // submits, Backspace/Delete edit, Tab asks the scripts to complete.
    void KeyPressed(int vk);
    // Typed characters, UTF-8 (printable ASCII is what the fonts carry).
    // '#' is refused, as the original refuses it: it is the colour marker.
    void TextInput(const std::string& utf8);

    // --- the engine loop ----------------------------------------------------
    // What Enter and Tab queued, for the loop to hand to the scripts. The
    // original defers the same way: the key handler stores the line and the
    // tick calls Hud_OnConsoleCommand (0x10027e90).
    enum Pending { kPendNone, kPendCommand, kPendSayAll, kPendSayTeam, kPendTab };
    Pending TakePending(std::string& text);

    // Draws the panel when open, or the message strip when closed. The frame
    // is the menu's carved border (the original builds a MenuItemBorder for
    // it); `now` is seconds, for the caret blink and the strip's expiry.
    void Draw(HudRenderer& hud, MenuSystem& menu, int screenW, int screenH, float now);

    // Kept so the strip can time its lines out: the loop feeds its clock.
    void SetClock(float now) { now_ = now; }

private:
    struct Line {
        std::string text;
        uint32_t argb;
    };
    struct Message {
        std::string text;
        uint32_t argb;
        float at;
    };

    void AddLine(const std::string& text, uint32_t argb);
    void Submit();
    void Clamp();

    bool active_ = false;
    bool opened_ = false;
    int mode_ = kFull;

    // The log: 100 lines at most (constructor +0x84), oldest dropped.
    static constexpr size_t kMaxLines = 100;
    std::vector<Line> lines_;
    // 1-based from the bottom, as the original keeps it (+0x88).
    int scroll_ = 1;

    // The input line and its caret (byte index).
    std::string text_;
    size_t cursor_ = 0;
    std::string prompt_ = ">";

    // Up/Down history: +0x98 array, walked by +0x80. -1 = not browsing.
    static constexpr size_t kMaxHistory = 200;
    std::vector<std::string> history_;
    int historyAt_ = -1;

    Pending pending_ = kPendNone;
    std::string pendingText_;

    // courbd 20: the constructor's defaults for both the log and the strip.
    std::string font_ = "courbd";
    int fontSize_ = 20;

    // The on-screen strip: four lines at most (0x100294a0 drops the oldest
    // at 4), at HUD.mpMsgPosition in HUD.mpMsgFont, each gone 15 seconds
    // after it arrived (0x100293f0). Drawn only while the console is down.
    static constexpr size_t kMaxMessages = 4;
    static constexpr float kMessageLife = 15.f;
    std::vector<Message> messages_;
    std::string msgFont_ = "courbd";
    int msgFontSize_ = 20;
    // -1 in the constructor: a line with no colour of its own takes this.
    uint32_t msgColor_ = 0xFFFFFFFFu;
    float msgX_ = 0.f, msgY_ = 0.f;
    float now_ = 0.f;
    // What the last Draw measured, for the page size and the caret.
    float lineH_ = 20.f;
    float sy_ = 1.f;
};

}  // namespace painful
