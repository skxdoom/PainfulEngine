#include "HudRenderer.h"
#include "ShaderLoad.h"

#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "MeshVertex.h"

#include <bx/math.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace painful {

namespace {

// Alpha over what is already on screen, no depth at all: the 2D layer is drawn
// last, in the order the scripts ask for, and a panel must not be rejected by
// the world's depth buffer.
constexpr uint64_t kState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                            BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;

} // namespace

bool HudRenderer::Init(const std::string& shaderDir, const std::string& fontsRoot) {
    namespace fs = std::filesystem;
    bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_hud");
    bgfx::ShaderHandle fsh = LoadShader(shaderDir, "fs_hud");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fsh)) {
        LogWarn("hud: vs_hud/fs_hud not found in %s", shaderDir.c_str());
        return false;
    }
    program_ = bgfx::createProgram(vs, fsh, true);
    if (!bgfx::isValid(program_)) return false;

    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    sDiffuse_ = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);
    sPattern_ = bgfx::createUniform("s_pattern", bgfx::UniformType::Sampler);
    uParams_ = bgfx::createUniform("u_hudParams", bgfx::UniformType::Vec4);

    // A solid quad is a textured quad with this bound, which keeps panels,
    // icons and glyphs in one batch and one shader.
    const uint32_t white = 0xffffffff;
    white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                   bgfx::copy(&white, 4));

    fonts_.SetRoot(fontsRoot);
    return true;
}

void HudRenderer::Shutdown() {
    fonts_.Shutdown();
    materials_.clear();
    if (bgfx::isValid(program_))  { bgfx::destroy(program_);  program_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sDiffuse_)) { bgfx::destroy(sDiffuse_); sDiffuse_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sPattern_)) { bgfx::destroy(sPattern_); sPattern_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uParams_))  { bgfx::destroy(uParams_);  uParams_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(white_))    { bgfx::destroy(white_);    white_    = BGFX_INVALID_HANDLE; }
}

HudRenderer::Material HudRenderer::CreateMaterial(const std::string& name,
                                                  TextureCache& textures,
                                                  const std::string& texturesRoot) {
    // The scripts name a texture the way everything else in the engine does -
    // "HUD/ammo" - and the cache resolves the extension and the archive.
    // The levelHint is empty: HUD art is global, not per level.
    (void)texturesRoot;
    bgfx::TextureHandle tex = textures.Get(name, "");
    if (!bgfx::isValid(tex)) return 0;

    Mat m;
    m.texture = tex;
    // MATERIAL.Size is how every script lays its HUD out: it asks the image how
    // big it is and scales from there, so this has to be the real size.
    if (!textures.Size(name, m.w, m.h)) { m.w = 0; m.h = 0; }
    m.used = true;

    for (size_t i = 0; i < materials_.size(); ++i) {
        if (materials_[i].used) continue;
        materials_[i] = m;
        return Material(i + 1);
    }
    materials_.push_back(m);
    return Material(materials_.size());
}

void HudRenderer::ReleaseMaterial(Material handle) {
    if (handle <= 0 || size_t(handle) > materials_.size()) return;
    // The texture belongs to the cache, which other things may still be
    // drawing with; only the slot is given back.
    materials_[size_t(handle) - 1] = Mat{};
}

bool HudRenderer::MaterialSize(Material handle, int& w, int& h) const {
    if (handle <= 0 || size_t(handle) > materials_.size()) return false;
    const Mat& m = materials_[size_t(handle) - 1];
    if (!m.used) return false;
    w = m.w;
    h = m.h;
    return true;
}

void HudRenderer::Begin(bgfx::ViewId view, int screenW, int screenH) {
    view_ = view;
    screenW_ = screenW;
    screenH_ = screenH;
    verts_.clear();
    batches_.clear();
    quads_ = 0;
    drawCalls_ = 0;
    active_ = true;

    // Pixels, origin top-left, y down - which is how the scripts think and how
    // every coordinate they compute is expressed.
    float ortho[16];
    bx::mtxOrtho(ortho, 0.f, float(screenW), float(screenH), 0.f, -1.f, 1.f, 0.f,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(view, nullptr, ortho);
    bgfx::setViewRect(view, 0, 0, uint16_t(screenW), uint16_t(screenH));
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);

    // The canvas: the window's height and 4/3 of it wide, on a window wider
    // than 4:3; otherwise the window itself, and every mapping below is the
    // identity - which is also what stretch mode asks for.
    canvasW_ = screenW;
    canvasH_ = screenH;
    if (aspect_ != Aspect::kStretch) {
        const int fourThree = int(std::lround(double(screenH) * 4.0 / 3.0));
        if (fourThree < screenW) canvasW_ = fourThree;
    }
}

float HudRenderer::OffsetFor(float centreX) const {
    if (!useCanvas_ || canvasW_ >= screenW_) return 0.f;
    if (grouped_) return groupOffset_;
    const float extra = float(screenW_ - canvasW_);
    if (!anchoring_ || aspect_ != Aspect::kAnchored) return extra * 0.5f;
    // The outer fifths anchor, the middle centres. Thirds took the soul and
    // gold counters, at 0.25 and 0.68 of the width, away from the top panel
    // they sit on; the corner readouts live within 0.05 of the edges.
    const float band = float(canvasW_) * 0.2f;
    if (centreX < band) return 0.f;
    if (centreX > float(canvasW_) - band) return extra;
    return extra * 0.5f;
}

float HudRenderer::CanvasOffsetX() const {
    if (canvasW_ >= screenW_) return 0.f;
    return float(screenW_ - canvasW_) * 0.5f;
}

void HudRenderer::BeginGroup(float centreX) {
    grouped_ = false;
    groupOffset_ = OffsetFor(centreX);
    grouped_ = true;
}

void HudRenderer::FillOutsideCanvas(uint32_t abgr) {
    if (canvasW_ >= screenW_) return;
    const float side = CanvasOffsetX();
    const bool was = useCanvas_;
    useCanvas_ = false;
    Quad(0, 0.f, 0.f, side, float(screenH_), abgr);
    Quad(0, float(screenW_) - side, 0.f, side, float(screenH_), abgr);
    useCanvas_ = was;
}

void HudRenderer::Cover(Material handle, uint32_t abgr) {
    int tw = 0, th = 0;
    if (!MaterialSize(handle, tw, th) || tw <= 0 || th <= 0) return;
    // Scale to cover the window, keep the aspect, crop the overflow evenly.
    const float scale = std::max(float(screenW_) / float(tw), float(screenH_) / float(th));
    const float w = float(tw) * scale, h = float(th) * scale;
    const float x = (float(screenW_) - w) * 0.5f, y = (float(screenH_) - h) * 0.5f;
    const bool was = useCanvas_;
    useCanvas_ = false;
    Quad(handle, x, y, w, h, abgr);
    useCanvas_ = was;
}

void HudRenderer::Push(bgfx::TextureHandle tex, const Vertex* quad,
                       bgfx::TextureHandle pattern, float patternW, float patternH) {
    if (!active_) return;
    if (!bgfx::isValid(tex)) tex = white_;
    if (!bgfx::isValid(pattern)) pattern = white_;

    // Two triangles. A new batch only when either texture changes, so a run of
    // glyphs from one atlas is still a single draw.
    if (batches_.empty() || batches_.back().texture.idx != tex.idx ||
        batches_.back().pattern.idx != pattern.idx) {
        Batch b;
        b.texture = tex;
        b.pattern = pattern;
        b.patternW = patternW > 0.f ? patternW : 1.f;
        b.patternH = patternH > 0.f ? patternH : 1.f;
        b.first = uint32_t(verts_.size());
        b.count = 0;
        batches_.push_back(b);
    }
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) verts_.push_back(quad[order[i]]);
    batches_.back().count += 6;
    ++quads_;
}

void HudRenderer::Quad(Material handle, float x, float y, float w, float h, uint32_t abgr,
                       float u1, float v1, float u2, float v2) {
    bgfx::TextureHandle tex = white_;
    if (handle > 0 && size_t(handle) <= materials_.size()) {
        const Mat& m = materials_[size_t(handle) - 1];
        if (m.used) tex = m.texture;
    }
    // A quad as wide as the canvas is a screen overlay - a fade, a damage
    // flash, a tint - and stretches over the whole window; anything narrower
    // is a layout element and is anchored. Only while anchoring: the menus'
    // full-canvas backgrounds are layout, the map and the board sit on them.
    if (useCanvas_ && anchoring_ && !grouped_ && canvasW_ > 0 && canvasW_ < screenW_ &&
        w >= float(canvasW_) - 1.f) {
        const float s = float(screenW_) / float(canvasW_);
        x *= s;
        w *= s;
    } else {
        x += OffsetFor(x + w * 0.5f);
    }
    const Vertex quad[4] = {
        {x,     y,     0.f, abgr, u1, v1},
        {x + w, y,     0.f, abgr, u2, v1},
        {x + w, y + h, 0.f, abgr, u2, v2},
        {x,     y + h, 0.f, abgr, u1, v2},
    };
    Push(tex, quad);
}

void HudRenderer::QuadRotated(Material handle, float x, float y, float w, float h,
                              float radians, float pivotX, float pivotY, uint32_t abgr) {
    bgfx::TextureHandle tex = white_;
    if (handle > 0 && size_t(handle) <= materials_.size()) {
        const Mat& m = materials_[size_t(handle) - 1];
        if (m.used) tex = m.texture;
    }
    // Centred on the PIVOT, turned in the authored 1024x768, then stretched
    // like the dial behind it - which is why x,y does not place the quad and
    // the width is not rigid on screen. Docs/Reference/Hud.md
    // The authored space is the canvas the scripts laid out against.
    const int cw = useCanvas_ ? canvasW_ : screenW_, chh = useCanvas_ ? canvasH_ : screenH_;
    const float ax = cw > 0 ? float(cw) / 1024.f : 1.f;
    const float ay = chh > 0 ? float(chh) / 768.f : 1.f;
    const float hw = (ax > 1e-6f ? w / ax : w) * 0.5f;
    const float hh = (ay > 1e-6f ? h / ay : h) * 0.5f;
    const float offset = OffsetFor(pivotX);
    const float px = ax > 1e-6f ? (pivotX + offset) / ax : pivotX + offset;
    const float py = ay > 1e-6f ? pivotY / ay : pivotY;

    // Negated: the script's bearing turns the opposite way to a y-down screen.
    const float c = std::cos(-radians), s = std::sin(-radians);
    const float corner[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    Vertex quad[4];
    for (int i = 0; i < 4; ++i) {
        const float dx = corner[i][0];
        const float dy = corner[i][1];
        quad[i].x = (px + dx * c - dy * s) * ax;
        quad[i].y = (py + dx * s + dy * c) * ay;
        quad[i].z = 0.f;
        quad[i].abgr = abgr;
        quad[i].u = uv[i][0];
        quad[i].v = uv[i][1];
    }
    Push(tex, quad);
}

// Four corners the caller has already placed, top-left, top-right,
// bottom-right, bottom-left. What a shape needs once it has been turned in a
// space the screen scales unevenly: rotate there, then scale each corner.
void HudRenderer::QuadCorners(Material handle, const float xy[8], uint32_t abgr) {
    bgfx::TextureHandle tex = white_;
    if (handle > 0 && size_t(handle) <= materials_.size()) {
        const Mat& m = materials_[size_t(handle) - 1];
        if (m.used) tex = m.texture;
    }
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const float offset = OffsetFor((xy[0] + xy[2] + xy[4] + xy[6]) * 0.25f);
    Vertex quad[4];
    for (int i = 0; i < 4; ++i) {
        quad[i].x = xy[i * 2] + offset;
        quad[i].y = xy[i * 2 + 1];
        quad[i].z = 0.f;
        quad[i].abgr = abgr;
        quad[i].u = uv[i][0];
        quad[i].v = uv[i][1];
    }
    Push(tex, quad);
}

void HudRenderer::Tiles(Material handle, float x, float y, float w, float h, uint32_t abgr) {
    int tw = 0, th = 0;
    if (!MaterialSize(handle, tw, th) || tw <= 0 || th <= 0) return;

    // A zero extent means "one texture", which is how the original spells an
    // edge that repeats along a single axis: DrawTiles(leftEdge, x, y, 0, h)
    // is a vertical strip one texture wide.
    if (w <= 0.f) w = float(tw);
    if (h <= 0.f) h = float(th);

    // The repeat comes from the sampler, not from geometry: the cache creates
    // its textures with BGFX_SAMPLER_NONE, which is wrap, so UVs beyond 1 tile
    // and the whole run stays a single quad.
    Quad(handle, x, y, w, h, abgr, 0.f, 0.f, w / float(tw), h / float(th));
}

float HudRenderer::Text(const std::string& fontName, int size, float x, float y,
                        const std::string& text, uint32_t abgr,
                        Material patternMaterial) {
    bgfx::TextureHandle pattern = BGFX_INVALID_HANDLE;
    float patternW = 1.f, patternH = 1.f;
    if (patternMaterial > 0 && size_t(patternMaterial) <= materials_.size()) {
        const Mat& pm = materials_[size_t(patternMaterial) - 1];
        if (pm.used) {
            pattern = pm.texture;
            patternW = float(pm.w > 0 ? pm.w : 1);
            patternH = float(pm.h > 0 ? pm.h : 1);
        }
    }
    const FontCache::Font* font = fonts_.Get(fontName, size);
    if (!font) return 0.f;

    const float width = FontCache::Measure(*font, text);
    // The scripts pass -1 to mean "centre this on the screen", which is how
    // every banner and every menu title is positioned.
    if (x < 0.f) x = (float(useCanvas_ ? canvasW_ : screenW_) - width) * 0.5f;
    // One anchor for the whole string, by its centre, so it never splits.
    x += OffsetFor(x + width * 0.5f);

    // y is the TOP of the line: the scripts lay out from the top edge, so the
    // baseline sits an ascent below it.
    const float baseline = y + font->ascent;
    const float iw = 1.f / float(font->atlasW);
    const float ih = 1.f / float(font->atlasH);

    float pen = x;
    for (unsigned char ch : text) {
        const FontCache::Glyph* g = font->Find(uint32_t(ch));
        if (!g) continue;
        if (g->w > 0 && g->h > 0) {
            const float gx = pen + g->offsetX;
            const float gy = baseline + g->offsetY;
            const Vertex quad[4] = {
                {gx,            gy,            0.f, abgr, g->x * iw,             g->y * ih},
                {gx + g->w,     gy,            0.f, abgr, (g->x + g->w) * iw,    g->y * ih},
                {gx + g->w,     gy + g->h,     0.f, abgr, (g->x + g->w) * iw,   (g->y + g->h) * ih},
                {gx,            gy + g->h,     0.f, abgr, g->x * iw,            (g->y + g->h) * ih},
            };
            Push(font->atlas, quad, pattern, patternW, patternH);
        }
        pen += g->advance;
    }
    return width;
}

float HudRenderer::TextWidth(const std::string& fontName, int size,
                             const std::string& text) {
    const FontCache::Font* font = fonts_.Get(fontName, size);
    return font ? FontCache::Measure(*font, text) : 0.f;
}

float HudRenderer::TextHeight(const std::string& fontName, int size) {
    const FontCache::Font* font = fonts_.Get(fontName, size);
    return font ? font->height() : 0.f;
}

void HudRenderer::Flush() {
    if (verts_.empty()) return;
    const uint32_t total = uint32_t(verts_.size());
    if (bgfx::getAvailTransientVertexBuffer(total, layout_) < total) {
        LogWarn("hud: %u vertices do not fit the transient buffer", total);
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, total, layout_);
    std::memcpy(tvb.data, verts_.data(), size_t(total) * sizeof(Vertex));

    for (const Batch& b : batches_) {
        if (b.count == 0) continue;
        bgfx::setVertexBuffer(0, &tvb, b.first, b.count);
        bgfx::setTexture(0, sDiffuse_, b.texture);
        bgfx::setTexture(1, sPattern_, bgfx::isValid(b.pattern) ? b.pattern : white_);
        const bool hasPattern = bgfx::isValid(b.pattern) && b.pattern.idx != white_.idx;
        const float params[4] = {1.f / b.patternW, 1.f / b.patternH,
                                 hasPattern ? 1.f : 0.f, 0.f};
        bgfx::setUniform(uParams_, params);
        bgfx::setState(kState);
        bgfx::submit(view_, program_);
        ++drawCalls_;
    }
}

void HudRenderer::End() {
    Flush();
    active_ = false;
}

} // namespace painful
