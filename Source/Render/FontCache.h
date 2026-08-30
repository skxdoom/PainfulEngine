#pragma once
#include <bgfx/bgfx.h>

#include <map>
#include <string>
#include <vector>

namespace painful {

// The HUD's text, rasterised from the game's own TrueType fonts.
//
// PainEngine draws its interface with real TTF - `Fonts/tahomabd.ttf`,
// `timesbd.ttf`, and a `painfont.ttf` for the game's own glyphs - and the
// scripts pick one by name and pixel size: `HUD.SetFont("timesbd", 26)`. There
// are 152 `HUD.PrintXY` sites against 17 `DrawQuad`, so the HUD is mostly text
// and this is most of the work.
//
// Each (name, size) is baked once into an atlas: stb_truetype rasterises the
// glyphs, stb_rect_pack arranges them, and the result becomes one texture.
// Drawing a string is then a quad per character out of that texture, which is
// what lets the whole HUD stay in one batch.
class FontCache {
public:
    ~FontCache() { Shutdown(); }

    void SetRoot(const std::string& fontsRoot) { root_ = fontsRoot; }
    void Shutdown();

    struct Glyph {
        // Atlas rectangle, in texels.
        uint16_t x = 0, y = 0, w = 0, h = 0;
        // Offset from the pen position to the top-left of the quad, in pixels.
        float offsetX = 0.f, offsetY = 0.f;
        float advance = 0.f;
    };

    struct Font {
        bgfx::TextureHandle atlas = BGFX_INVALID_HANDLE;
        uint16_t atlasW = 0, atlasH = 0;
        float ascent = 0.f, descent = 0.f, lineGap = 0.f;
        std::map<uint32_t, Glyph> glyphs;
        bool ok = false;

        float height() const { return ascent - descent + lineGap; }
        const Glyph* Find(uint32_t codepoint) const {
            auto it = glyphs.find(codepoint);
            return it == glyphs.end() ? nullptr : &it->second;
        }
    };

    // Baked on first use and kept. Null when the .ttf cannot be read, which
    // leaves the caller drawing nothing rather than crashing.
    const Font* Get(const std::string& name, int pixelSize);

    // Width of a string in pixels, and the font's line height. Both are what
    // HUD.GetTextWidth / GetTextHeight answer, and the scripts centre text by
    // subtracting the width from the screen width, so this has to agree with
    // what Draw actually produces.
    static float Measure(const Font& font, const std::string& text);

    size_t baked() const { return fonts_.size(); }

private:
    std::string root_;
    std::map<std::string, Font> fonts_;    // keyed "name|size"
};

} // namespace painful
