#pragma once
#include "../Assets/Dat.h"
#include "../Assets/Pkmdl.h"
#include "../Assets/Skeleton.h"
#include "MeshVertex.h"
#include "../Assets/ShaderScript.h"
#include "MaterialState.h"
#include "../World/Level.h"
#include "../World/Lighting.h"
#include "../World/Templates.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <map>
#include <set>
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

    // The CLight and CEnvironment placements that light the models. Build
    // does this too; the script-driven path has no Level to pass to Build
    // and calls this on its own.
    void BuildLighting(const Level& level, TemplateCache& templates);
    void SetLevelAmbient(const float rgb255[3]) { lighting_.SetLevelAmbient(rgb255); }
    size_t lightCount() const { return lighting_.lightCount(); }
    size_t environmentCount() const { return lighting_.environmentCount(); }

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
    // One instance of a world-mesh object that physics owns (an active mesh).
    // Its vertices are taken to world space and re-based on `origin`, the
    // body's centre, so SetScriptPose places it exactly as the body moves.
    // Lit as an entity: these objects carry no lightmap, and the original
    // makes each one an Entity in the world (World::LoadMeshPakFile).
    int CreateWorldObject(const MapObject& object, float worldScale, const float origin[3],
                          TextureCache& textures, const std::string& levelHint);
    int CreateScriptPack(const std::string& packName, const std::string& meshName,
                         float scale, TextureCache& textures,
                         const std::string& itemsRoot);
    // rotWXYZ is an engine-order quaternion, converted with the engine's own
    // matrix form (see Properties.cpp ReadRotation).
    void SetScriptPose(int slot, const float pos[3], const float rotWXYZ[4]);
    // This instance's pose for the frame: one skinning matrix per bone, in the
    // model's own bone order. The script side owns the skeleton and computes
    // this (see SkeletonCache), because the joint natives have to answer from
    // the same pose with no window open. Passing null or an empty count
    // returns the instance to its bind pose.
    void SetScriptSkinning(int slot, const Mat4* skin, size_t count);
    void SetScriptVisible(int slot, bool visible);
    // MDL.SetMeshVisibility(entity, meshName, on) - hides ONE named mesh of an
    // instance. The Painkiller hides nine of them so the gun reads as empty
    // while its head is away; monsters hide gib parts the same way.
    void SetScriptMeshVisibility(int slot, const std::string& meshName, bool visible);
    // MDL.SetMaterial(entity, name) - swaps this instance to another material
    // from the shader scripts. Unknown names are left alone and reported.
    void SetScriptMaterial(int slot, const std::string& name, TextureCache& textures);
    void ReleaseScript(int slot);
    // World-space size of the instance's model (bind-pose bounds times its
    // scale) - what ENTITY.GetDimensions reports.
    bool GetScriptDimensions(int slot, float out[3]) const;

    size_t placed() const { return instances_.size(); }
    size_t distinctModels() const { return models_.size(); }
    size_t unresolved() const { return unresolved_; }
    size_t packed() const { return packed_; }
    size_t hidden() const { return hidden_; }
    size_t drawCalls() const { return drawCalls_; }
    // Instances that were CPU-skinned this frame (visible, animated, skinned).
    size_t posedInstances() const { return posedInstances_; }
    // Which models those were - the check that actors animate, not just the
    // weapon in the player's hands.
    const std::set<std::string>& posedModels() const { return posedModels_; }

    // Diagnostic override: 0 = CCW, 1 = CW, 2 = none.
    void SetCullMode(int mode) { cullMode_ = mode; }
    // The material scripts. Build() sets these for the viewer; the script-driven
    // game never calls Build, so without this every model fell back to plain
    // opaque state - no alpha test, no 2sided, no stage 1.
    void SetShaders(ShaderLibrary* shaders) { shaders_ = shaders; }

    // Live multiplier applied on top of every resolved entity scale (the run
    // command binds it to the + and - keys). Rebuilds the cached transforms.
    void SetScaleMultiplier(float k);
    float scaleMultiplier() const { return scaleMultiplier_; }

private:
    struct Part {                          // one mesh (or material run) of a model
        // The CPU-side mesh, retained ONLY for a skinned model: posing it each
        // frame needs the bind-pose vertices and the bone weights back.
        // Everything else drops its copy once the data is on the GPU.
        ModelMesh cpu;
        // The highest bone index the weights reference. A skin matrix array
        // shorter than this leaves those vertices in the bind pose - the
        // one thing that can detach a hand from an otherwise animated arm -
        // and SkinMeshVertices drops such influences silently, so it is
        // checked and reported once at draw time instead.
        uint16_t maxBone = 0;
        bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
        // ONE index buffer per mesh or object; each part draws its own range
        // of it. A buffer per material run spent bgfx's 4096 index-buffer
        // handles on the Enclave's 1585 active meshes, and every buffer made
        // after that - the weapons included - came back invalid and drew with
        // someone else's indices. Docs/Reference/Physics.md, "Active meshes".
        bgfx::IndexBufferHandle ibo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle diffuse = BGFX_INVALID_HANDLE;
        // The material's stage-1 texture, when it has one (blood, freeze).
        bgfx::TextureHandle stage1 = BGFX_INVALID_HANDLE;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        bool ownsVbo = true;               // parts of one pack object share a vbo
        bool ownsIbo = true;               // and the index buffer, likewise
        // Which part owns the vertex buffer this one draws from. A model mesh
        // split across material slots shares one set of vertices and one posed
        // buffer; each slot brings only its own index run.
        uint32_t vboOwner = 0;
        // Per part, because a .shader override keys off the MESH name, not the
        // file name: Swamp_dirtywater.pkmdl holds a mesh called "dirtywater",
        // which is the entry that makes the swamp water scroll. One material
        // for a whole model could never see that.
        MaterialState material;
        // The mesh this part came from. SetMeshVisibility addresses parts by
        // name, and cpu is dropped for anything unskinned, so it is kept here.
        std::string name;
    };
    struct GpuModel {
        std::vector<Part> parts;
        // Largest bind-pose dimension, in the model file's own units.
        float extent = 1.f;
        std::string name;                  // for diagnostics only
        // From the game's .shader scripts: models use the palskinned family
        // (cull cw - the Maya exporter's winding is authored, not guessed),
        // pack meshes the defaultNTU family (cull ccw, like world geometry).
        MaterialState material;
        float bboxLo[3] = {0, 0, 0}, bboxHi[3] = {0, 0, 0};   // local bounds
        // Whether any part carries skin weights, so a pose pushed at this
        // model can be used. The skeleton itself belongs to the script side.
        bool skinned = false;
    };
    struct Instance {
        size_t model = 0;
        // MDL.SetMaterial: this instance draws with another material family
        // (gibs turn palskinned_bloody). Per instance, since the GpuModel and
        // its parts are shared by every entity using that model.
        bool materialOverride = false;
        MaterialState material;
        bgfx::TextureHandle stage1 = BGFX_INVALID_HANDLE;
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
        // This instance's pose, pushed from the script side each frame:
        // inverseBind * boneWorld, one per bone. The renderer does not compute
        // it, because the joint natives need the same pose with no window open
        // and two copies of that arithmetic could drift apart - a muzzle flash
        // drawn at one pose and spawned at another.
        std::vector<Mat4> skin;
        // One posed buffer per part. A pose is per INSTANCE, so these cannot
        // be shared with the model the way the bind-pose buffers are.
        std::vector<bgfx::DynamicVertexBufferHandle> posed;
        // Script-driven instances are created and released at runtime; slots
        // stay put so handles remain stable, and Draw skips the dead and the
        // hidden.
        bool alive = true;
        bool visible = true;
        // MDL.SetMeshVisibility: which of the model's parts this instance hides.
        // Per instance, not per model - the viewmodel hides its blades while
        // another copy of the same model keeps them. Empty means all shown.
        std::vector<uint8_t> hiddenParts;
        // The environment cross-fade this instance is in the middle of. Per
        // instance because two monks either side of a doorway are at different
        // points of the same fade.
        EntityLightFade lightFade;
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
    // Per-frame scratch for CPU skinning, kept here so posing an actor does
    // not allocate every frame.
    std::vector<float> vertScratch_;
    std::vector<MeshVertex> posedVerts_;

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
    // The model lighting block - the engine's c10 ambient and its c12..c23
    // four-light set. See World/Lighting.h.
    bgfx::UniformHandle uLightColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uLightDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uLightHalf_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSpecular_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sStage1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uStage1_ = BGFX_INVALID_HANDLE;
    EntityLighting lighting_;
    float lastTime_ = 0.f;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    size_t unresolved_ = 0, packed_ = 0, hidden_ = 0, drawCalls_ = 0, posedInstances_ = 0;
    std::set<std::string> posedModels_;
    // Models wind the OPPOSITE way to world meshes: .pkmdl comes from the Maya
    // exporter, .mpk from ase2mpk. Verified visually - CCW turns models inside out.
    int cullMode_ = 1;
    float scaleMultiplier_ = 1.f;
    bool visCulling_ = true;
};

} // namespace painful
