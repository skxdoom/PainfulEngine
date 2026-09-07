#pragma once
#include "../World/Decals.h"
#include "../Core/Vectors.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace painful {

// Draws the DecalSystem's live decals: the clipped surface triangles, the
// decal's texture (or its animation frame), the fade factor as vertex colour,
// and the blend mode from the .ini. Docs/Reference/Decals.md.
class DecalRenderer {
public:
    ~DecalRenderer() { Shutdown(); }
    // Owns GPU handles that Shutdown destroys, so it is not copyable: a copy
    // would free them twice.
    DecalRenderer() = default;
    DecalRenderer(const DecalRenderer&) = delete;
    DecalRenderer& operator=(const DecalRenderer&) = delete;

    bool Init(const std::string& shaderDir);
    void Shutdown();
    // Drops the texture bindings - a level switch, or ENTITY.ReloadDecalSystem.
    void Clear() { textures_.clear(); }

    void Draw(bgfx::ViewId view, const Camera& camera, const DecalSystem& decals,
              TextureCache& textures);

    // The level fog, applied to decal colour as D3D's vertex fog did.
    void SetFog(int mode, float start, float end, float density, const Vec3& color255) {
        fog_[0] = float(mode); fog_[1] = start; fog_[2] = end; fog_[3] = density;
        for (int i = 0; i < 3; ++i) fogColor_[i] = color255[i] / 255.f;
        fogColor_[3] = 1.f;
    }

    size_t drawCalls() const { return drawCalls_; }
    size_t triangles() const { return triangles_; }

private:
    struct Frames {
        std::vector<bgfx::TextureHandle> frames;   // one, or the _NN sequence
    };
    const Frames& Resolve(const std::string& texture, bool animated, TextureCache& textures);

    std::map<std::string, Frames> textures_;
    float fog_[4] = {0, 0, 90.f, 0};
    float fogColor_[4] = {0, 0, 0, 1.f};
    size_t drawCalls_ = 0, triangles_ = 0;
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
