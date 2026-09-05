#pragma once
#include "../World/CollisionMesh.h"
#include "../World/Level.h"
#include "../World/Templates.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

// Draws the level's CBillboard entities: light coronas and plain sprites.
//
// One entity type covers both. Corona.Enabled decides which: a corona grows
// with distance, fades in and out as the line of sight to it opens and closes,
// and ignores the depth buffer; a plain billboard is a fixed-size depth-tested
// sprite at full alpha. There is no separate lens-flare chain anywhere in the
// data - the "flare" look comes from the sprite textures (sflare1b, flarka)
// hung on ordinary coronas.
//
// The parameters all arrive through one native call, which is what pins them
// down: CBillboard:Apply does
//
//     BILLBOARD.SetupCorona(entity, Alpha, FadeInTime, FadeOutTime, MinSize,
//         MinDistance, Size, MaxDistance, OffDistance, TraceMargin,
//         "Particles/"..Texture, Color:Compose(), BlendMode, not Corona.Enabled)
//
// and its implementation (Engine.dll 0x10137b70) gives the field-for-field
// mapping. Behaviour follows Billboard::Draw (0x101ccae0) and
// Billboard::FadeTick (0x101cc120).
class BillboardRenderer {
public:
    ~BillboardRenderer() { Shutdown(); }

    bool Init(const std::string& shaderDir);
    void Shutdown();

    void Build(const Level& level, TemplateCache& templates, TextureCache& textures);

    // --- script-driven sprites (the BILLBOARD.SetupCorona native path) ---
    // Arguments arrive exactly as the native passes them - the script has
    // already done the old light-corona conversions Build must handle
    // itself. args holds, in the native's order: alpha, fadeIn, fadeOut,
    // minSize, minDistance, size, maxDistance, offDistance, traceMargin.
    // slot -1 creates the sprite; an existing slot reconfigures in place.
    // Returns the slot.
    int SetupScriptCorona(int slot, const float args[9], const std::string& texture,
                          uint32_t packedColor, int blendMode, bool spriteOnly,
                          TextureCache& textures, const std::string& levelHint);
    void SetScriptSpritePos(int slot, const float pos[3]);
    void SetScriptSpriteVisible(int slot, bool visible);

    // R3D.DrawSprite: one billboard, this frame only. The muzzle flash is a
    // CProcess that calls it from Render every frame it lives, so there is no
    // slot to keep - and unlike a corona it carries a ROTATION, which is what
    // stops four shots in a row looking like the same picture.
    void DrawBeamImmediate(const float a[3], const float b[3], float width,
                           uint32_t abgr, bgfx::TextureHandle texture);
    void DrawImmediate(const float pos[3], float size, float rot, uint32_t abgr,
                       bgfx::TextureHandle texture);
    void RemoveScriptSprite(int slot);

    // Distance, occlusion tracing and fading, all of which the original does
    // inside Draw. Split out so the frame's simulation and its submission stay
    // separate, as they are for particles.
    void Update(const Camera& camera, float dt, const CollisionMesh& collision);

    void Draw(bgfx::ViewId view, const Camera& camera);

    // The level o.Scale multiplier, applied to positions and sizes the same
    // way EntityRenderer applies it to placed models.
    void SetScaleMultiplier(float k);
    float scaleMultiplier() const { return scaleMultiplier_; }
    // RGB multiplier on placed coronas and billboards: the level's
    // BloomFX.DimScale while bloom is on (Billboard::Draw). Particles.md, "Bloom dims".
    void SetColorScale(float k) { colorScale_ = k; }
    // The level fog, applied to sprite colour as the original's vertex fog did.
    void SetFog(int mode, float start, float end, float density, const float color255[3]) {
        fog_[0] = float(mode); fog_[1] = start; fog_[2] = end; fog_[3] = density;
        for (int i = 0; i < 3; ++i) fogColor_[i] = color255[i] / 255.f;
        fogColor_[3] = 1.f;
    }

    size_t placed() const { return sprites_.size(); }
    size_t coronas() const { return coronas_; }
    size_t visible() const { return visible_; }
    size_t traces() const { return traces_; }
    size_t drawCalls() const { return drawCalls_; }

private:
    struct Sprite {
        float pos[3] = {0, 0, 0};
        uint8_t r = 255, g = 255, b = 255;
        float alpha = 0.5f;          // the TARGET alpha; the fade ramps up to it
        float size = 5.f;            // max size, reached at MaxDistance
        float minSize = 0.8f;
        float minDistance = 5.f, maxDistance = 20.f, offDistance = 70.f;
        float fadeInTime = 0.5f, fadeOutTime = 0.5f;
        float traceMargin = 1.f;
        bool  corona = false;

        // Runtime state, named after the fields they mirror in the original.
        float curAlpha = 0.f;        // +0x69c
        float fadeTimer = 0.f;       // +0x690
        float curSize = 5.f;         // +0x6d8
        float distance = 0.f;        // +0x6a4
        float traceTimer = 0.f;      // +0x6fc, counts down to the next trace
        bool  wasVisible = false;    // +0x6c0
        bool  blocked = true;        // +0x6c4, last trace result
        // ENTITY.EnableDraw, which never reached a sprite at all. Hiding a
        // billboard has to go through the FADE like every other visibility
        // change, or a VFX that should dim out pops instead.
        bool  scriptVisible = true;

        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        uint64_t blendState = 0;
        int blendMode = 1;              // material enum, for the fog colour
        // Script-driven sprites are created and released at runtime; slots
        // stay put so handles remain stable.
        bool alive = true;
    };

    struct Immediate {
        float pos[3];
        float size;
        float rot;
        uint32_t abgr;
        bgfx::TextureHandle texture;
    };
    std::vector<Immediate> immediate_;

    // R3D.DrawSprite1DOF: a quad stretched between two world points that spins
    // about that axis to face the viewer - one degree of freedom, hence the
    // name. The Painkiller draws its energy beam from the gun to its stuck
    // head this way, one per frame while the head is attached.
    struct Beam {
        float a[3], b[3];
        float width;
        uint32_t abgr;
        bgfx::TextureHandle texture;
    };
    std::vector<Beam> beams_;

    void FadeTick(Sprite& s, bool nowVisible, float dt) const;

    std::vector<Sprite> sprites_;
    float scaleMultiplier_ = 1.f;
    float colorScale_ = 1.f;
    float fog_[4] = {0, 0, 90.f, 0};
    float fogColor_[4] = {0, 0, 0, 1.f};
    bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
    size_t coronas_ = 0, visible_ = 0, traces_ = 0, drawCalls_ = 0;

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
