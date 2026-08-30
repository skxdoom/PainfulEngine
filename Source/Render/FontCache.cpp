#include "FontCache.h"

#include "../Core/Common.h"
#include "../Core/Log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <cmath>
#include <cstring>

namespace painful {

namespace {

// Latin-1, which covers every string the shipped scripts print - the game's
// text is English with the odd accented character in the credits. Baking the
// whole range costs one small texture per (font, size) and removes any
// question of a glyph being missing at draw time.
constexpr uint32_t kFirstChar = 32;
constexpr uint32_t kLastChar = 255;

// Big enough for the largest size the scripts ask for (36 px) with room to
// spare; stb_truetype reports failure if a bake overflows, and then the size
// is doubled and tried again.
constexpr int kInitialAtlas = 512;

} // namespace

void FontCache::Shutdown() {
    for (auto& kv : fonts_)
        if (bgfx::isValid(kv.second.atlas)) bgfx::destroy(kv.second.atlas);
    fonts_.clear();
}

const FontCache::Font* FontCache::Get(const std::string& name, int pixelSize) {
    if (pixelSize <= 0) pixelSize = 16;
    const std::string key = name + "|" + std::to_string(pixelSize);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second.ok ? &it->second : nullptr;

    Font& font = fonts_[key];

    // Through the VFS: the shipped game keeps its fonts in Fonts.pak, and a
    // loader that opens a filesystem path finds nothing there.
    std::vector<uint8_t> ttf;
    if (!ReadFile(root_ + "/" + name + ".ttf", ttf) || ttf.empty()) {
        LogWarn("font: cannot read %s.ttf", name.c_str());
        font.ok = false;
        return nullptr;
    }

    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        LogWarn("font: %s.ttf is not a TrueType this build can read", name.c_str());
        font.ok = false;
        return nullptr;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, float(pixelSize));
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    font.ascent = float(ascent) * scale;
    font.descent = float(descent) * scale;
    font.lineGap = float(lineGap) * scale;

    // Grow until every glyph fits rather than guessing one size for a 36-pixel
    // face and a 12-pixel one.
    int dim = kInitialAtlas;
    std::vector<uint8_t> alpha;
    std::vector<stbtt_bakedchar> baked(kLastChar - kFirstChar + 1);
    for (; dim <= 4096; dim *= 2) {
        alpha.assign(size_t(dim) * size_t(dim), 0);
        const int rc = stbtt_BakeFontBitmap(ttf.data(), 0, float(pixelSize), alpha.data(),
                                            dim, dim, int(kFirstChar),
                                            int(kLastChar - kFirstChar + 1), baked.data());
        if (rc > 0) break;          // every glyph fitted
    }
    if (dim > 4096) {
        LogWarn("font: %s at %d px does not fit an atlas", name.c_str(), pixelSize);
        font.ok = false;
        return nullptr;
    }

    for (uint32_t c = kFirstChar; c <= kLastChar; ++c) {
        const stbtt_bakedchar& b = baked[c - kFirstChar];
        Glyph g;
        g.x = b.x0;
        g.y = b.y0;
        g.w = uint16_t(b.x1 - b.x0);
        g.h = uint16_t(b.y1 - b.y0);
        g.offsetX = b.xoff;
        g.offsetY = b.yoff;
        g.advance = b.xadvance;
        font.glyphs[c] = g;
    }

    // Expanded to RGBA so one shader draws glyphs and icons alike: white with
    // the coverage in alpha, which then modulates by the vertex colour exactly
    // as a texture does.
    std::vector<uint8_t> rgba(size_t(dim) * size_t(dim) * 4);
    for (size_t i = 0; i < alpha.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }

    font.atlas = bgfx::createTexture2D(
        uint16_t(dim), uint16_t(dim), false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    font.atlasW = uint16_t(dim);
    font.atlasH = uint16_t(dim);
    font.ok = bgfx::isValid(font.atlas);
    if (font.ok)
        LogInfo("font: %s at %d px baked into a %dx%d atlas", name.c_str(), pixelSize,
                dim, dim);
    return font.ok ? &font : nullptr;
}

float FontCache::Measure(const Font& font, const std::string& text) {
    float width = 0.f;
    for (unsigned char c : text) {
        const Glyph* g = font.Find(uint32_t(c));
        if (g) width += g->advance;
    }
    return width;
}

} // namespace painful
