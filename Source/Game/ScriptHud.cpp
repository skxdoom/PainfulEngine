// ScriptEngine: the 2D layer - MATERIAL, HUD.PrintXY, fonts and colour codes.

#include "ScriptEngineInternal.h"

namespace painful {

// ------------------------------------------------------------- the 2D layer
//
// Everything the shipped game draws over the world - health, ammo, the tarot
// board, the loading screens, the menus - is drawn from Lua through these.
// Argument order, defaults and colour packing below are read out of
// Engine.dll rather than guessed; Docs/Reference/Hud.md records where each came from.

namespace {

// PainEngine packs colours as D3D ARGB. HUD.PrintXY builds one out of three
// script arguments with `((r | 0xffffff00) << 8 | g) << 8 | b`, which is
// 0xFF_RR_GG_BB; DrawQuadRGBA builds `((a << 8 | r) << 8 | g) << 8 | b`, the
// same layout with a real alpha. R3D.RGB / R3D.RGBA agree, so one unpack
// serves the lot.
//
// The alpha passed here is final. HUD.SetTransparency is NOT folded in: the
// original stores that byte and nothing in the draw path reads it. The
// scripts apply it themselves - Hud:QuadTrans reads it back with
// HUD.GetTransparency and passes it as the RGBA alpha - so multiplying it in
// again would square the fade and leave the whole interface nearly
// invisible.
uint32_t ArgbToAbgr(uint32_t argb) {
    const uint32_t a = (argb >> 24) & 0xFF;
    const uint32_t r = (argb >> 16) & 0xFF;
    const uint32_t g = (argb >> 8) & 0xFF;
    const uint32_t b = argb & 0xFF;
    // bgfx vertex colours are little-endian ABGR.
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// The sixteen colours `#0`..`#f` select, read straight out of the table at
// 0x103e6220: four four-step ramps - parchment, grey, blood, leather - which
// is the whole of Painkiller's interface palette.
const uint32_t kColorCodes[16] = {
    0xffffba7a, 0xffe6a161, 0xffcd8848, 0xff9b5616,
    0xffd1d1d1, 0xffb8b8b8, 0xff9f9f9f, 0xff6d6d6d,
    0xffd60017, 0xffbd0000, 0xffa40000, 0xff720000,
    0xff6c483a, 0xff532f21, 0xff3a1608, 0xff080000,
};

bool ColorCodeDigit(char c, int& out) {
    if (c >= '0' && c <= '9') { out = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { out = 10 + (c - 'a'); return true; }
    return false;
}

// `#` plus one hex digit is a colour marker, and HUD::GetTextWidth steps over
// both without measuring them. Anything else after a `#` is literal text.
std::string StripColorCodes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        int idx = 0;
        if (text[i] == '#' && i + 1 < text.size() && ColorCodeDigit(text[i + 1], idx)) {
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

// A material handle travels through Lua as light userdata, because that is
// what MATERIAL.Create returns in the original - a Texture* the scripts hold
// opaquely and hand back. Scripts also pass a literal 0 to mean "no texture",
// which arrives as a number and reads back as handle 0.
void PushMaterial(lua_State* L, int handle) {
    if (handle <= 0) {
        lua_pushnil(L);
        return;
    }
    lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<intptr_t>(handle)));
}

int ToMaterial(lua_State* L, int index) {
    if (lua_islightuserdata(L, index))
        return int(reinterpret_cast<intptr_t>(lua_touserdata(L, index)));
    return 0;
}

} // namespace

int ScriptEngine::HudFontPixels(int size) const {
    if (size <= 0) size = hudFontSize_;
    // round(size * (H/768 + W/1024) * 0.5), which is 1:1 at the 1024x768 the
    // interface was authored at.
    const float scale =
        (float(screenH_) / 768.f + float(screenW_) / 1024.f) * 0.5f;
    const int px = int(std::lround(double(size) * double(scale)));
    return px > 0 ? px : 1;
}

void ScriptEngine::HudResolveFont(const char* name, int size, std::string& outName,
                                  int& outPixels) const {
    // PrintXY calls SetFont(0) when the script names no font - slot 0, the
    // default, not whatever HUD.SetFont last selected. timesbd is the game's
    // own default: it is what all but one of the shipped SetFont calls ask
    // for, and the only face the HUD scripts print with.
    if (name && *name) {
        outName = name;
        outPixels = HudFontPixels(size);
    } else {
        outName = "timesbd";
        outPixels = HudFontPixels(size);
    }
}

// MATERIAL.Create(name, flags) -> a texture handle. The flags are the
// TextureFlags bitfield (NoLOD, NoMipMaps and friends); our cache decides
// sampling from the image itself, so they are read and ignored.
int ScriptEngine::L_MATERIAL_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!self->hudTextures_ || !name || !*name) {
        lua_pushnil(L);
        return 1;
    }
    if (self->hud_) {
        PushMaterial(L, self->hud_->CreateMaterial(name, *self->hudTextures_, ""));
        return 1;
    }
    // Headless: no renderer to hold the texture, but the size still has to be
    // right or every script that lays itself out from MATERIAL.Size takes its
    // "material not found" branch and the HUD never runs on this path.
    int w = 0, h = 0;
    if (!self->hudTextures_->Measure(name, "", w, h)) {
        lua_pushnil(L);
        return 1;
    }
    self->headlessMaterials_.emplace_back(w, h);
    PushMaterial(L, int(self->headlessMaterials_.size()));
    return 1;
}

int ScriptEngine::L_MATERIAL_Release(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->hud_) self->hud_->ReleaseMaterial(ToMaterial(L, 1));
    return 0;
}

// MATERIAL.Size(mat) -> width, height. Every HUD script lays itself out by
// asking an image how big it is, so this has to be the real size. The
// original answers -1, -1 for a null material rather than a plausible
// guess - a script that divides by it then produces something visibly wrong
// instead of something subtly wrong.
int ScriptEngine::L_MATERIAL_Size(lua_State* L) {
    ScriptEngine* self = From(L);
    int w = -1, h = -1;
    const int m = ToMaterial(L, 1);
    if (self->hud_) {
        self->hud_->MaterialSize(m, w, h);
    } else if (m > 0 && size_t(m) <= self->headlessMaterials_.size()) {
        w = self->headlessMaterials_[size_t(m) - 1].first;
        h = self->headlessMaterials_[size_t(m) - 1].second;
    }
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

// HUD.PrintXY(x, y, text, font, r, g, b, size)
//
// The colour defaults are (0, 255, 0): text with no colour given is green,
// which is the shipped console's colour. A negative x centres the string
// horizontally and a negative y centres it vertically, both against the real
// screen size - that is how every banner in the game is positioned.
int ScriptEngine::L_HUD_PrintXY(lua_State* L) {
    ScriptEngine* self = From(L);
    const int rawX = int(luaL_optnumber(L, 1, 0));
    const int rawY = int(luaL_optnumber(L, 2, 0));
    const char* text = luaL_optstring(L, 3, nullptr);
    const char* font = luaL_optstring(L, 4, nullptr);
    const uint32_t r = uint32_t(int(luaL_optnumber(L, 5, 0))) & 0xFF;
    const uint32_t g = uint32_t(int(luaL_optnumber(L, 6, 255))) & 0xFF;
    const uint32_t b = uint32_t(int(luaL_optnumber(L, 7, 0))) & 0xFF;
    const int size = int(luaL_optnumber(L, 8, 0));
    if (!self->hud_ || !text) return 0;

    std::string fontName;
    int pixels = 0;
    self->HudResolveFont(font, size, fontName, pixels);

    float x = float(rawX), y = float(rawY);
    if (rawX < 0)
        x = std::floor((float(self->screenW_) -
                        self->hud_->TextWidth(fontName, pixels, StripColorCodes(text))) *
                       0.5f);
    if (rawY < 0)
        y = std::floor((float(self->screenH_) - self->hud_->TextHeight(fontName, pixels)) * 0.5f);

    // `#<hex digit>` switches colour mid-string and is not itself drawn, so a
    // run is emitted per colour and the pen carries across.
    const std::string s = text;
    uint32_t argb = 0xFF000000u | (r << 16) | (g << 8) | b;
    std::string run;
    for (size_t i = 0; i <= s.size(); ++i) {
        int idx = 0;
        const bool marker = i + 1 < s.size() && s[i] == '#' && ColorCodeDigit(s[i + 1], idx);
        if (i == s.size() || marker) {
            if (!run.empty()) {
                x += self->hud_->Text(fontName, pixels, x, y, run,
                                      ArgbToAbgr(argb));
                run.clear();
            }
            if (marker) {
                // The original only honours a marker when the running colour
                // has any RGB at all, so text explicitly drawn black stays
                // black through one.
                if ((argb & 0xFFFFFFu) != 0) argb = kColorCodes[idx];
                ++i;
            }
            continue;
        }
        run.push_back(s[i]);
    }
    return 0;
}

// HUD.DrawQuad(mat, x, y, w, h, color, u1, v1, u2, v2)
// The colour defaults to -1, which is 0xFFFFFFFF: opaque white, drawing the
// texture as it is. The UVs default to the whole image.
int ScriptEngine::L_HUD_DrawQuad(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const int mat = ToMaterial(L, 1);
    const float x = float(luaL_optnumber(L, 2, 0));
    const float y = float(luaL_optnumber(L, 3, 0));
    const float w = float(luaL_optnumber(L, 4, 0));
    const float h = float(luaL_optnumber(L, 5, 0));
    const uint32_t argb = uint32_t(int64_t(luaL_optnumber(L, 6, -1)));
    const float u1 = float(luaL_optnumber(L, 7, 0.0));
    const float v1 = float(luaL_optnumber(L, 8, 0.0));
    const float u2 = float(luaL_optnumber(L, 9, 1.0));
    const float v2 = float(luaL_optnumber(L, 10, 1.0));
    self->hud_->Quad(mat, x, y, w, h, ArgbToAbgr(argb), u1, v1, u2, v2);
    return 0;
}

// HUD.DrawQuadRGBA(mat, x, y, w, h, r, g, b, a, u1, v1, u2, v2)
// The UV defaults are 0.01 and 0.99, not 0 and 1: an inset that keeps the
// filter off the edge texels of an icon packed against its neighbours.
int ScriptEngine::L_HUD_DrawQuadRGBA(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const int mat = ToMaterial(L, 1);
    const float x = float(luaL_optnumber(L, 2, 0));
    const float y = float(luaL_optnumber(L, 3, 0));
    const float w = float(luaL_optnumber(L, 4, 0));
    const float h = float(luaL_optnumber(L, 5, 0));
    const uint32_t r = uint32_t(int(luaL_optnumber(L, 6, 255))) & 0xFF;
    const uint32_t g = uint32_t(int(luaL_optnumber(L, 7, 255))) & 0xFF;
    const uint32_t b = uint32_t(int(luaL_optnumber(L, 8, 255))) & 0xFF;
    const uint32_t a = uint32_t(int(luaL_optnumber(L, 9, 255))) & 0xFF;
    const float u1 = float(luaL_optnumber(L, 10, 0.01));
    const float v1 = float(luaL_optnumber(L, 11, 0.01));
    const float u2 = float(luaL_optnumber(L, 12, 0.99));
    const float v2 = float(luaL_optnumber(L, 13, 0.99));
    const uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
    self->hud_->Quad(mat, x, y, w, h, ArgbToAbgr(argb), u1, v1, u2, v2);
    return 0;
}

// HUD.DrawQuadRotated(mat, x, y, w, h, angle, pivotX, pivotY, r, g, b, a)
//
// The compass needle. The pivot is an absolute screen point, not an offset
// and not the quad's centre: Hud:QuadRot draws the arrow at one place and
// turns it about the dial's hub a few pixels away. The original rounds the
// pivot to whole pixels before using it.
int ScriptEngine::L_HUD_DrawQuadRotated(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const int mat = ToMaterial(L, 1);
    const float x = float(luaL_optnumber(L, 2, 0));
    const float y = float(luaL_optnumber(L, 3, 0));
    const float w = float(luaL_optnumber(L, 4, 0));
    const float h = float(luaL_optnumber(L, 5, 0));
    const float angle = float(luaL_optnumber(L, 6, 0.0));
    const float px = float(std::lround(luaL_optnumber(L, 7, 0.0)));
    const float py = float(std::lround(luaL_optnumber(L, 8, 0.0)));
    const uint32_t r = uint32_t(int(luaL_optnumber(L, 9, 255))) & 0xFF;
    const uint32_t g = uint32_t(int(luaL_optnumber(L, 10, 255))) & 0xFF;
    const uint32_t b = uint32_t(int(luaL_optnumber(L, 11, 255))) & 0xFF;
    const uint32_t a = uint32_t(int(luaL_optnumber(L, 12, 255))) & 0xFF;
    const uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
    self->hud_->QuadRotated(mat, x, y, w, h, angle, px, py, ArgbToAbgr(argb));
    return 0;
}

// HUD.DrawRect(x, y, w, h, color): an untextured filled rectangle.
int ScriptEngine::L_HUD_DrawRect(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const float x = float(luaL_optnumber(L, 1, 0));
    const float y = float(luaL_optnumber(L, 2, 0));
    const float w = float(luaL_optnumber(L, 3, 0));
    const float h = float(luaL_optnumber(L, 4, 0));
    const uint32_t argb = uint32_t(int64_t(luaL_optnumber(L, 5, -1)));
    self->hud_->Quad(0, x, y, w, h, ArgbToAbgr(argb));
    return 0;
}

// HUD.DrawBorder(x, y, w, h), defaulting to the whole 1024x768 reference
// screen.
//
// This is not a line rectangle: HUD::DrawBorder at 0x1008b510 builds a
// MenuItemBorder - the carved stone frame - and renders it, which is why it
// takes no colour. Now that the menu owns that widget, the HUD borrows it, so
// a script drawing a frame gets the shipped art either way.
int ScriptEngine::L_HUD_DrawBorder(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    self->menu_.DrawFrame(float(luaL_optnumber(L, 1, 0)), float(luaL_optnumber(L, 2, 0)),
                          float(luaL_optnumber(L, 3, 1024)),
                          float(luaL_optnumber(L, 4, 768)));
    return 0;
}

int ScriptEngine::L_HUD_SetFont(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, "");
    const int size = int(luaL_optnumber(L, 2, 0));
    if (*name) self->hudFont_ = name;
    if (size > 0) self->hudFontSize_ = size;
    return 0;
}

// HUD.GetTextWidth(text) -> pixels, measured in the font HUD.SetFont chose.
// Colour markers are stepped over rather than measured, and a multi-line
// string measures as its widest line.
int ScriptEngine::L_HUD_GetTextWidth(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* text = luaL_optstring(L, 1, "");
    if (!self->hud_) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const int pixels = self->HudFontPixels(0);
    const std::string clean = StripColorCodes(text);
    float widest = 0.f;
    size_t start = 0;
    while (start <= clean.size()) {
        const size_t end = clean.find('\n', start);
        const std::string line =
            clean.substr(start, end == std::string::npos ? std::string::npos : end - start);
        widest = std::max(widest, self->hud_->TextWidth(self->hudFont_, pixels, line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    lua_pushnumber(L, double(int(widest)));
    return 1;
}

// HUD.GetTextHeight(text) -> (newlines + 1) * the font's line height.
int ScriptEngine::L_HUD_GetTextHeight(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* text = luaL_optstring(L, 1, "");
    if (!self->hud_) {
        lua_pushnumber(L, 0);
        return 1;
    }
    int lines = 1;
    for (const char* p = text; *p; ++p)
        if (*p == '\n') ++lines;
    const float line = self->hud_->TextHeight(self->hudFont_, self->HudFontPixels(0));
    lua_pushnumber(L, double(int(line) * lines));
    return 1;
}

// HUD.SetTransparency(percent) / GetTransparency() -> 0-255.
//
// The argument is a PERCENTAGE - it comes from the HUD Transparency slider in
// the options menu - and the original stores round(percent * 2.55) in a byte,
// defaulting to 100. Nothing in the draw path reads that byte; the scripts
// read it back themselves and pass it as an RGBA alpha, so the conversion is
// the whole of what this native does.
int ScriptEngine::L_HUD_SetTransparency(lua_State* L) {
    ScriptEngine* self = From(L);
    const long v = std::lround(luaL_optnumber(L, 1, 100) * 2.55);
    self->hudAlpha_ = int(v < 0 ? 0 : (v > 255 ? 255 : v));
    return 0;
}

int ScriptEngine::L_HUD_GetTransparency(lua_State* L) {
    lua_pushnumber(L, From(L)->hudAlpha_);
    return 1;
}

int ScriptEngine::L_HUD_StripColorInfo(lua_State* L) {
    const std::string out = StripColorCodes(luaL_optstring(L, 1, ""));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// HUD.ColorSubstr(text, n) -> the first n VISIBLE characters, carrying the
// colour markers along so the trimmed string still draws in its own colours.
// The typing effect on the loading screens is this called with a rising n.
int ScriptEngine::L_HUD_ColorSubstr(lua_State* L) {
    const std::string s = luaL_optstring(L, 1, "");
    const int want = int(luaL_optnumber(L, 2, 0));
    std::string out;
    int visible = 0;
    for (size_t i = 0; i < s.size() && visible < want; ++i) {
        int idx = 0;
        if (s[i] == '#' && i + 1 < s.size() && ColorCodeDigit(s[i + 1], idx)) {
            out.push_back(s[i]);
            out.push_back(s[i + 1]);
            ++i;
            continue;
        }
        out.push_back(s[i]);
        ++visible;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// R3D.ScreenSize() -> the real window, which every HUD script scales its
// layout from.
int ScriptEngine::L_R3D_ScreenSize(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushnumber(L, self->screenW_);
    lua_pushnumber(L, self->screenH_);
    return 2;
}

// R3D.GetFPS() -> frames per second. The HUD formats it with string.format
// '%d', so returning nothing is a script error rather than a missing number.
int ScriptEngine::L_R3D_GetFPS(lua_State* L) {
    ScriptEngine* self = From(L);
    const float dt = self->frameDelta_;
    lua_pushnumber(L, dt > 0.f ? double(int(1.f / dt + 0.5f)) : 0.0);
    return 1;
}


}  // namespace painful
