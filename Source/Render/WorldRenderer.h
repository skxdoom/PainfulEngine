#pragma once
#include "../Assets/Mpk.h"
#include "../Assets/ShaderScript.h"
#include "MaterialState.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

// Draws a level's static world mesh.
//
// One vertex/index buffer per MPK object, and one draw call per material, since
// materials tile the object's index array as contiguous triangle runs.
class WorldRenderer {
public:
    ~WorldRenderer() { Shutdown(); }

    // shaderDir holds the compiled vs_world.bin / fs_world.bin.
    bool Init(const std::string& shaderDir);
    void Shutdown();

    // levelHint is the map name, used to find per-level textures.
    // shaders may be null; material state then falls back to built-in defaults.
    void Upload(const MapMesh& map, TextureCache& textures, const std::string& levelHint,
                float worldScale = 1.f, ShaderLibrary* shaders = nullptr,
                bool overbright = false);

    // ambient/fogColor are 0-255 as stored in the level file.
    void Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
              const float ambient[3], const float fogColor[3], float fogDensity, float fogStart);

    size_t drawCalls() const { return drawCalls_; }

    // Diagnostic: 0 = CCW, 1 = CW, 2 = none.
    void SetCullMode(int mode) { cullMode_ = mode; }
    size_t trianglesUploaded() const { return triangles_; }

private:
    struct Batch {                       // one material of one object
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        bgfx::TextureHandle diffuse = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle lightmap = BGFX_INVALID_HANDLE;
        bool hasLightmap = false;
    };
    struct Chunk {                       // one MPK object
        bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibo = BGFX_INVALID_HANDLE;
        std::vector<Batch> batches;
        Mat4 transform;
        MaterialState material;          // from the game's .shader scripts
    };

    std::vector<Chunk> chunks_;
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sLightmap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uAmbient_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
    size_t drawCalls_ = 0, triangles_ = 0;
    int cullMode_ = 0;
};

} // namespace painful
