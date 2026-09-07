#pragma once
#include "../Assets/Mpk.h"
#include "../Core/Vectors.h"
#include "../Core/Matrix.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace painful {

// One Data/Scripts/Decals/<name>.ini, with DecalEffect's defaults
// (FUN_1008a740) where a key is absent. Docs/Reference/Decals.md.
struct DecalDef {
    std::string name;
    std::string texture;        // "Decals/<Texture>"; empty = white
    int blendMode = 6;          // invmodulate: the surface absorbs the texture
    float scale = 1.f;          // full width of the projection box
    float zScale = 0.f;         // depth of the box; 0 = unbounded
    float cullBackFaces = -1.f; // min dot(triangle normal, decal normal); -1 = none
    float lifeTime = 6.f;       // seconds; negative = immortal
    float animTime = 0.f;       // AnimationTime; read by nothing recovered
    float decayTime = 1.f;      // the fade-out at the end of the life
    float fps = 0.f;            // > 0 animates <Texture>_NN and ages at real time
    bool cutTris = true;        // clip triangles to the box, or take them whole
};

// The decal definitions, loaded on first use and dropped by
// ENTITY.ReloadDecalSystem. A missing file yields the defaults, as the
// original's ConfigFile::Load failure does.
class DecalLibrary {
public:
    void Init(const std::string& scriptsDir) { dir_ = scriptsDir + "/Decals"; }
    const DecalDef& Get(const std::string& name);
    void Reload() { defs_.clear(); }

private:
    std::string dir_;
    std::map<std::string, DecalDef> defs_;
};

struct DecalVertex {
    Vec3 pos;
    float u, v;
};

// One spawned decal: its box, the surface triangles clipped into it, and
// its clock. Geometry is in world space; the renderer only draws it.
struct DecalInstance {
    bool alive = false;
    DecalDef def;
    std::string textureOverride;    // SpawnStaticDecal's texture
    float scale = 1.f;              // CreateEntity's scale argument
    // Row-vector basis: X and Y span the box (length = full width), Z is the
    // depth axis pointing INTO the surface, T the spawn point.
    float basis[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    Vec3 normal{0, 1, 0};    // out of the surface; the render nudge
    std::vector<DecalVertex> verts;
    float life = 0.f;
    bool immortal = false;
    bool finished = false;
    uint8_t alpha = 255;            // the fade factor, 0x01010101 * alpha
    int objects = 0;                // map objects appended, for diagnostics
};

// Decals in the CPU: Decal::Spawn's projection and clip, Decal::Tick's clock.
// Lives beside the world rather than the renderer so a headless run spawns
// and ages the same decals the window draws.
class DecalSystem {
public:
    // A new decal, empty. Never fails; the slot is stable until Remove.
    int Create(const DecalDef& def, float scale);
    // Decal::Spawn(entity, pos, normal): a box facing the normal, spun by a
    // random angle about it when the decal is mortal.
    void SetBasis(int slot, const Vec3& pos, const Vec3& normal);
    // Decal::Spawn(entity, pos, normal, up, right): the box's own axes,
    // lengths kept - the blood leak stretches its U axis 2.5x.
    void SetBasisOriented(int slot, const Vec3& pos, const Vec3& normal,
                          const Vec3& up, const Vec3& right);
    void ClearGeometry(int slot);
    // Projects one map object's triangles into the box. objectToWorld takes
    // the object's raw vertices to the space the decal was spawned in.
    void Append(int slot, const MapObject& object, const Mat4& objectToWorld);
    // World AABB of the projection box, for choosing objects to append.
    void Box(int slot, Vec3& lo, Vec3& hi) const;
    bool HasGeometry(int slot) const;
    void SetTextureOverride(int slot, const std::string& texture);

    // Decal::Tick for every live decal.
    void Tick(float dt);
    bool Finished(int slot) const;
    void Remove(int slot);
    void Clear();

    // R3D.KeepDecals: mortal, unanimated decals stop ageing.
    void SetKeep(bool keep) { keep_ = keep; }
    bool keep() const { return keep_; }
    // Cfg.DecalsStayTime: the ageing rate of unanimated decals.
    void SetSpeed(float speed) { speed_ = speed; }
    float speed() const { return speed_; }

    const std::vector<DecalInstance>& decals() const { return decals_; }
    size_t live() const { return live_; }
    // Decals whose projection found no surface at all, for diagnostics.
    size_t empty() const { return empty_; }

    static constexpr int kMaxVertices = 2048;   // Decal::MAX_ELEMS

private:
    bool Valid(int slot) const {
        return slot >= 0 && size_t(slot) < decals_.size() && decals_[size_t(slot)].alive;
    }
    void ProjectTriangle(DecalInstance& d, const Vec3 w[3], const float inv[9]);

    std::vector<DecalInstance> decals_;
    size_t live_ = 0, empty_ = 0;
    bool keep_ = false;
    float speed_ = 0.4f;   // Cfg.lua's default for DecalsStayTime
    uint32_t rng_ = 0x2545f491u;
};

} // namespace painful
