#pragma once
#include "../Assets/Dat.h"
#include "../Assets/Pkmdl.h"
#include "../Assets/ShaderScript.h"
#include "MaterialState.h"
#include "../World/Level.h"
#include "../World/Templates.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <map>
#include <string>
#include <vector>

namespace painful {

// Draws the models placed in a level.
//
// Instances name a template (BaseObj); the template chain supplies the mesh and
// scale. Actors resolve to .pkmdl model files; items resolve to an object inside
// a .dat mesh pack (o.Pack names the pack, o.Mesh the object).
class EntityRenderer {
public:
    ~EntityRenderer() { Shutdown(); }

    bool Init(const std::string& shaderDir);
    void Shutdown();

    void Build(const Level& level, TemplateCache& templates, TextureCache& textures,
               const std::string& dataRoot, ShaderLibrary* shaders = nullptr);

    void Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
              const LevelInfo& info, float timeSeconds);

    // Disables frustum culling (the --novis flag).
    void SetVisibilityCulling(bool on) { visCulling_ = on; }

    // Moves one placed entity to where the simulation has put it. Entities
    // that became physics bodies are drawn wherever physics says they are,
    // which is the whole point of them being bodies; everything else keeps the
    // position the level authored.
    void SetEntityPose(size_t entityIndex, const float pos[3], const float rot[9]);

    // --- script-driven instances (the ENTITY.* native path) ---
    // Creates one instance of a .pkmdl model (the scale arrives with the
    // scripts' own *0.1 rule already applied) or of an object inside a .dat
    // pack. Returns the instance slot, or -1 when the source cannot be
    // resolved.
    int CreateScriptModel(const std::string& modelName, float scale,
                          TextureCache& textures, const std::string& modelsRoot);
    int CreateScriptPack(const std::string& packName, const std::string& meshName,
                         float scale, TextureCache& textures,
                         const std::string& itemsRoot);
    // rotWXYZ is an engine-order quaternion, converted with the engine's own
    // matrix form (see Properties.cpp ReadRotation).
    void SetScriptPose(int slot, const float pos[3], const float rotWXYZ[4]);
    void SetScriptVisible(int slot, bool visible);
    void ReleaseScript(int slot);

    size_t placed() const { return instances_.size(); }
    size_t distinctModels() const { return models_.size(); }
    size_t unresolved() const { return unresolved_; }
    size_t packed() const { return packed_; }
    size_t hidden() const { return hidden_; }
    size_t drawCalls() const { return drawCalls_; }

    // Diagnostic override: 0 = CCW, 1 = CW, 2 = none.
    void SetCullMode(int mode) { cullMode_ = mode; }

    // Live multiplier applied on top of every resolved entity scale (the run
    // command binds it to the + and - keys). Rebuilds the cached transforms.
    void SetScaleMultiplier(float k);
    float scaleMultiplier() const { return scaleMultiplier_; }

private:
    struct Part {                          // one mesh (or material run) of a model
        bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle diffuse = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        bool ownsVbo = true;               // parts of one pack object share a vbo
        // Per part, because a .shader override keys off the MESH name, not the
        // file name: Swamp_dirtywater.pkmdl holds a mesh called "dirtywater",
        // which is the entry that makes the swamp water scroll. One material
        // for a whole model could never see that.
        MaterialState material;
    };
    struct GpuModel {
        std::vector<Part> parts;
        // Largest bind-pose dimension, in the model file's own units.
        float extent = 1.f;
        // From the game's .shader scripts: models use the palskinned family
        // (cull cw - the Maya exporter's winding is authored, not guessed),
        // pack meshes the defaultNTU family (cull ccw, like world geometry).
        MaterialState material;
        float bboxLo[3] = {0, 0, 0}, bboxHi[3] = {0, 0, 0};   // local bounds
    };
    struct Instance {
        size_t model = 0;
        Mat4 transform;
        float aabbLo[3] = {0, 0, 0}, aabbHi[3] = {0, 0, 0};   // world bounds
        // Basis kept so the transform can be rebuilt when the live scale
        // multiplier changes; scaling is about each entity's own origin.
        float pos[3] = {0, 0, 0};
        float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        float scale = 1.f;
        // Which level entity this came from, so physics can say where it has
        // moved to.
        size_t entity = 0;
        // Script-driven instances are created and released at runtime; slots
        // stay put so handles remain stable, and Draw skips the dead and the
        // hidden.
        bool alive = true;
        bool visible = true;
    };

    // Recomputes the instance's world-space bounds from its model's bbox.
    void UpdateBounds(Instance& instance, const GpuModel& model) const;

    // Returns an index into models_, loading and uploading on first use.
    bool GetModel(const std::string& modelName, TextureCache& textures,
                  const std::string& modelsRoot, size_t& outIndex);
    // Same, for an object inside a .dat item pack.
    bool GetPack(const std::string& packName, const std::string& meshName,
                 TextureCache& textures, const std::string& itemsRoot, size_t& outIndex);

    ShaderLibrary* shaders_ = nullptr;           // material scripts, set by Build
    std::map<std::string, size_t> modelIndex_;   // model name -> models_ slot
    std::vector<GpuModel> models_;
    std::vector<Instance> instances_;

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sLightmap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uAmbient_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uUvAnim_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uDetail_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sDetail_ = BGFX_INVALID_HANDLE;
    // Entities have no per-slot UV transform, but the fragment shader is
    // shared with the world pass, so these must be set to identity every draw
    // or the last world chunk's tiling (up to 30x) leaks onto models.
    bgfx::UniformHandle uUv0_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uUv1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTile_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    size_t unresolved_ = 0, packed_ = 0, hidden_ = 0, drawCalls_ = 0;
    // Models wind the OPPOSITE way to world meshes: .pkmdl comes from the Maya
    // exporter, .mpk from ase2mpk. Verified visually - CCW turns models inside out.
    int cullMode_ = 1;
    float scaleMultiplier_ = 1.f;
    bool visCulling_ = true;
};

} // namespace painful
