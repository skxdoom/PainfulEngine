#include "Console.h"

#include "../Core/Log.h"
#include "../Render/HudRenderer.h"
#include "MenuSystem.h"

#include <algorithm>
#include <cmath>

namespace painful {

namespace {

uint32_t ArgbToAbgr(uint32_t argb) {
    const uint32_t a = (argb >> 24) & 0xFF, r = (argb >> 16) & 0xFF;
    const uint32_t g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// HUD::SetFont's scale: 1:1 at the 1024x768 the interface was authored at.
int FontPixels(int size, int screenW, int screenH) {
    const float scale = (float(screenH) / 768.f + float(screenW) / 1024.f) * 0.5f;
    const int px = int(std::lround(double(size) * double(scale)));
    return px > 0 ? px : 1;
}

float Round(float v) { return float(std::lround(v)); }

// The panel, in authoring units: 30 in from the corner, 964 wide, 360 tall
// for the full console and a single 42-tall line in the chat modes
// (0x100283e0 writes exactly these into the border). Text starts 10 in from
// the panel's edge and the log wraps 20 short of its width; the draw
// (0x100298f0) keeps 10 clear under the input line, 20 under the panel's
// bottom (12 in the chat modes).
constexpr float kPanelX = 30.f, kPanelY = 30.f, kPanelW = 964.f;
constexpr float kPanelH = 360.f, kPanelHSay = 42.f;
constexpr float kTextInset = 10.f, kWrapMargin = 20.f;

// Everything the panel prints is the interface tan.
constexpr uint32_t kTextColor = Console::kMessageColor;

}  // namespace

void Console::Activate(bool on, int mode) {
    mode_ = mode;
    if (on) {
        // 0x100283e0: input cleared, caret and scroll home, the panel resized
        // for the mode, prompt ">" - and the log left alone.
        text_.clear();
        cursor_ = 0;
        scroll_ = 1;
        historyAt_ = -1;
        opened_ = true;
    }
    active_ = on;
}

void Console::AddLine(const std::string& text, uint32_t argb) {
    lines_.push_back({text, argb});
    if (lines_.size() > kMaxLines) lines_.erase(lines_.begin());
    if (messages_.size() >= kMaxMessages) messages_.erase(messages_.begin());
    messages_.push_back({text, argb, now_});
}

void Console::AddMessage(const std::string& text, uint32_t argb) {
    size_t start = 0;
    for (;;) {
        const size_t nl = text.find('\n', start);
        const std::string part = text.substr(start, nl == std::string::npos ? nl : nl - start);
        AddLine(part, argb);
        LogInfo("[console] %s", part.c_str());
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void Console::SetCurrentText(const std::string& text) {
    text_ = text;
    cursor_ = text_.size();
}

void Console::SetFont(const std::string& name, int size) {
    if (!name.empty()) font_ = name;
    if (size > 0) fontSize_ = size;
}

void Console::SetMessageColor(int r, int g, int b) {
    msgColor_ = 0xFF000000u | (uint32_t(r & 0xFF) << 16) | (uint32_t(g & 0xFF) << 8) |
                uint32_t(b & 0xFF);
}

void Console::SetMessageFont(const std::string& name, int size) {
    if (!name.empty()) msgFont_ = name;
    if (size > 0) msgFontSize_ = size;
}

void Console::Clamp() {
    if (cursor_ > text_.size()) cursor_ = text_.size();
    const int total = int(lines_.size());
    if (scroll_ > total) scroll_ = total > 0 ? total : 1;
    if (scroll_ < 1) scroll_ = 1;
}

// Enter (0x1002ad00): echo the line into the log, push it onto the history,
// queue it for the scripts, and clear the input.
void Console::Submit() {
    if (!text_.empty()) {
        AddLine(prompt_ + text_, kTextColor);
        history_.push_back(text_);
        if (history_.size() > kMaxHistory) history_.erase(history_.begin());
        pending_ = mode_ == kSayAll ? kPendSayAll : mode_ == kSayTeam ? kPendSayTeam : kPendCommand;
        pendingText_ = text_;
    }
    historyAt_ = -1;
    text_.clear();
    cursor_ = 0;
    scroll_ = 1;
}

void Console::KeyPressed(int vk) {
    if (!active_) return;
    // A page is half the panel's height in lines (0x10027d60).
    const int page = std::max(1, int(std::lround(kPanelH * sy_ / std::max(lineH_, 1.f) * 0.5f)));
    const int total = int(lines_.size());
    switch (vk) {
    case 33:   // PageUp: older
        if (scroll_ + page < total - page) scroll_ += page;
        else if (page < total) scroll_ = total - page;
        break;
    case 34:   // PageDown: newer
        scroll_ = std::max(1, scroll_ - page);
        break;
    case 37:   // Left
        if (cursor_ > 0) --cursor_;
        break;
    case 39:   // Right
        if (cursor_ < text_.size()) ++cursor_;
        break;
    case 38:   // Up: the previous line, the newest first
        if (!history_.empty()) {
            if (historyAt_ < 0) historyAt_ = int(history_.size()) - 1;
            else if (historyAt_ > 0) --historyAt_;
            text_ = history_[size_t(historyAt_)];
            cursor_ = text_.size();
        }
        break;
    case 40:   // Down: forward through the history, then an empty line
        if (historyAt_ >= 0) {
            if (historyAt_ + 1 < int(history_.size())) {
                ++historyAt_;
                text_ = history_[size_t(historyAt_)];
            } else {
                historyAt_ = -1;
                text_.clear();
            }
            cursor_ = text_.size();
        }
        break;
    case 13:    // Enter
    case 252:   // the keypad's, as the engine numbers it
        Submit();
        break;
    case 8:    // Backspace
        if (cursor_ > 0) {
            text_.erase(cursor_ - 1, 1);
            --cursor_;
        }
        break;
    case 46:   // Delete
        if (cursor_ < text_.size()) text_.erase(cursor_, 1);
        break;
    case 9:    // Tab: Hud_OnConsoleTab(text) completes it script-side
        pending_ = kPendTab;
        pendingText_ = text_;
        break;
    default:
        break;
    }
    Clamp();
}

void Console::TextInput(const std::string& utf8) {
    if (!active_) return;
    for (const char c : utf8) {
        const unsigned char u = static_cast<unsigned char>(c);
        // 0x10028f60: nothing below space, and never '#'.
        if (u < 0x20 || u == '#') continue;
        text_.insert(cursor_, 1, c);
        ++cursor_;
    }
}

Console::Pending Console::TakePending(std::string& text) {
    const Pending p = pending_;
    if (p != kPendNone) text = pendingText_;
    pending_ = kPendNone;
    pendingText_.clear();
    return p;
}

void Console::Draw(HudRenderer& hud, MenuSystem& menu, int screenW, int screenH, float now) {
    now_ = now;
    const float sx = float(screenW) / 1024.f, sy = float(screenH) / 768.f;
    sy_ = sy;

    if (!active_) {
        // The strip: the last few lines, at HUD.mpMsgPosition, each dropped
        // 15 seconds after it arrived (0x100293f0), over a one-pixel black
        // shadow. A line that carries no colour of its own takes the strip's.
        while (!messages_.empty() && now - messages_.front().at >= kMessageLife)
            messages_.erase(messages_.begin());
        if (messages_.empty()) return;
        const int px = FontPixels(msgFontSize_, screenW, screenH);
        const float lineH = hud.TextHeight(msgFont_, px);
        const float x = Round(msgX_ * sx + kTextInset * sx);
        const float y0 = Round(msgY_ * sy) + kTextInset * sy;
        for (size_t i = 0; i < messages_.size(); ++i) {
            const Message& m = messages_[i];
            const float y = y0 + lineH * float(i);
            const uint32_t argb = m.argb == 0xFFFFFFFFu ? msgColor_ : m.argb;
            hud.Text(msgFont_, px, x + 1.f, y + 1.f, m.text, 0xFF000000u);
            hud.Text(msgFont_, px, x, y, m.text, ArgbToAbgr(argb));
        }
        return;
    }

    const float panelH = mode_ == kFull ? kPanelH : kPanelHSay;
    menu.DrawFrame(kPanelX, kPanelY, kPanelW, panelH);

    const int px = FontPixels(fontSize_, screenW, screenH);
    const float lineH = hud.TextHeight(font_, px);
    lineH_ = lineH;
    const float x = Round(kTextInset * sx + Round(kPanelX * sx));
    const float top = Round(kPanelY * sy);
    const float inner = Round(panelH * sy) - (mode_ == kFull ? 20.f : 12.f);
    const float wrapW = Round(kPanelW * sx) - kWrapMargin;
    const uint32_t color = ArgbToAbgr(kTextColor);

    // The input line: prompt, then as much of the text as fits with the
    // caret in view, then the caret.
    const float inputY = top + 10.f + inner - lineH;
    const float promptW = hud.TextWidth(font_, px, prompt_);
    size_t start = 0;
    while (start < cursor_ &&
           promptW + hud.TextWidth(font_, px, text_.substr(start, cursor_ - start)) > wrapW - 12.f)
        ++start;
    std::string shown = text_.substr(start);
    while (!shown.empty() && promptW + hud.TextWidth(font_, px, shown) > wrapW) shown.pop_back();
    hud.Text(font_, px, x, inputY, prompt_, color);
    hud.Text(font_, px, x + promptW, inputY, shown, color);
    const float caretX = x + promptW + hud.TextWidth(font_, px, text_.substr(start, cursor_ - start));
    hud.Text(font_, px, caretX, inputY, "_", color);
    if (mode_ != kFull) return;

    // The log above it, newest at the bottom, wrapped to the panel by
    // character the way 0x1002a530 wraps on the way in, up to the panel's
    // top edge.
    struct Row {
        std::string text;
        uint32_t argb;
    };
    std::vector<Row> rows;
    const int last = int(lines_.size()) - scroll_;   // index of the newest row shown
    const int visible = int((inputY - top) / lineH) + 1;
    for (int i = last; i >= 0 && int(rows.size()) < visible; --i) {
        const Line& line = lines_[size_t(i)];
        std::vector<std::string> pieces;
        std::string rest = line.text;
        while (!rest.empty() && hud.TextWidth(font_, px, rest) > wrapW) {
            size_t n = 1;
            while (n < rest.size() && hud.TextWidth(font_, px, rest.substr(0, n + 1)) <= wrapW) ++n;
            pieces.push_back(rest.substr(0, n));
            rest = rest.substr(n);
        }
        pieces.push_back(rest);
        // Pushed newest-first, so a wrapped line's tail goes in before its head.
        for (auto it = pieces.rbegin(); it != pieces.rend(); ++it) rows.push_back({*it, line.argb});
    }
    float y = inputY;
    for (const Row& r : rows) {
        y -= lineH;
        if (y < top) break;
        hud.Text(font_, px, x, y, r.text, ArgbToAbgr(r.argb));
    }
}

}  // namespace painful
