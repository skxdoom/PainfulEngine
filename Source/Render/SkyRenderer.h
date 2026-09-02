#pragma once
#include "../Assets/Mpk.h"
#include "../World/Level.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

// Draws the sky dome.
//
// PainEngine's sky is up to four layers over a dome mesh. Each layer blends two
// independently scrolling/rotating textures through a mask and modulates the
// result by a lightmap. Layer 1 is the opaque base; the rest alpha-blend over it.
//
// If a level declares no layers, the engine's own LowQuality fallback is used
// instead: one mesh, one texture, one yaw offset.
//
// The dome is centred on the camera and drawn with no depth write, so world
// geometry always paints over it.
class SkyRenderer {
public:
    ~SkyRenderer() { Shutdown(); }

    bool Init(const std::string& shaderDir);
    void Shutdown();
    // Drops the dome and keeps the program - a level switch.
    void Unload();

    bool Load(const std::string& mapsRoot, const LevelInfo& info, TextureCache& textures);

    void Draw(bgfx::ViewId view, const Camera& camera, int width, int height, float timeSeconds);

    bool loaded() const { return !parts_.empty(); }
    int  layerCount() const { return layerCount_; }
    bool layered() const { return layered_; }

private:
    struct Part {
        bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibo = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        // Which layer this shell belongs to, read from the object name
        // ("layer01shape", "_trans_layer03shape"). 1-based; 0 means unknown.
        int layer = 0;
        // The "_trans_" prefix marks a shell that blends over the ones below.
        bool blend = false;
    };
    struct GpuLayer {
        bgfx::TextureHandle tex1 = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle tex2 = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle mask = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle lmap = BGFX_INVALID_HANDLE;
        SkyTexture anim1, anim2;
        int number = 0;   // 1-based layer index as declared in the level
    };

    bool LoadDome(const std::string& path);

    std::vector<Part> parts_;
    GpuLayer layers_[4];
    int layerCount_ = 0;
    bool layered_ = false;
    float angle_ = 0.f;

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sTex1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sTex2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sMask_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sLmap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uXform1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uXform2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uRot_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
