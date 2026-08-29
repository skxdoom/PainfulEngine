#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../Assets/Mpk.h"
#include "../Script/LuaHost.h"
#include "../World/Level.h"
#include "../World/PhysicsWorld.h"

namespace painful {

class BillboardRenderer;
class EmitterLibrary;
class EntityRenderer;
class ParticleRenderer;
class TextureCache;

// The engine-side state behind the script natives: the entity registry that
// ENTITY.* manipulates and the world state WORLD.* accumulates. This is the
// seam between the Lua layer and the engine subsystems - the same seam the
// original engine has, where every native takes an entity handle first.
//
// Headless-safe by design: with no renderer attached, Create still hands out
// real handles and the registry tracks transforms - which is what the `lua`
// diagnostic exercises. The windowed game loop attaches the renderer and the
// same natives start putting things on screen.
class ScriptEngine {
public:
    // ETypes, verbatim from Definitions.lua.
    enum EType {
        kNone = 0, kMesh = 1, kLight = 2, kParticleFX = 3, kModel = 4,
        kRegion = 5, kDecal = 6, kBillboard = 8, kEnvironment = 9,
        kTrail = 10, kSound = 11,
    };

    struct Entity {
        int type = kNone;
        std::string source;         // model name (Model) or pack path (Mesh)
        std::string mesh;           // pack object name (Mesh only)
        std::string name;           // the "Name:Tag" the script passed
        float scale = 1.f;
        float pos[3] = {0, 0, 0};
        float rotWXYZ[4] = {1, 0, 0, 0};
        bool visible = true;
        bool inWorld = false;       // WORLD.AddEntity was called
        bool worldObject = false;   // WORLD.FindEntityByName pseudo-entity
        int rendererInstance = -1;  // EntityRenderer slot, -1 when headless/unresolved
        int physicsBody = -1;       // PhysicsWorld script-body slot
        int spriteSlot = -1;        // BillboardRenderer slot (Billboard type)
        // ParticleRenderer slots, indexed by the per-entity emitter index the
        // scripts hold (-1 entries when running headless).
        std::vector<int> emitterSlots;
    };

    // What the scripts told WORLD.* to set up; the game loop turns this into
    // the actual renderer state. Colours arrive packed by our own R3D.RGBA
    // (Color:Compose routes through it), so the layout is ours on both ends.
    struct WorldState {
        std::string mapPath;        // engine-style, "../Data/Maps/<map>"
        std::string levelName;
        float scale = 1.f;
        bool overbright = false;
        bool loadRequested = false;

        int fogMode = 0;
        float fogStart = 0.f, fogEnd = 90.f, fogDensity = 0.f;
        float fogColor[3] = {0, 0, 0};          // 0-255
        float farClip = 1024.f;
        float ambient[3] = {128, 128, 128};     // 0-255
        std::string detailTex;
        float detailTileU = 8.2f, detailTileV = 7.1f;

        // Sky, via WORLD.LoadSky / SetupSkyLayer / LoadLowQualitySky - the
        // same data CLevel:ReloadSky pushes at the original.
        std::string skyDomeMap;      // layered dome mesh, basename
        int skyLayerCount = 0;
        SkyLayer skyLayers[4];
        std::string skyMap;          // low-quality dome, basename
        std::string skyTexture;      // low-quality single texture
        float skyAngle = 0.f;
    };

    // Installs the real natives over their stubs. Call after LuaHost::Init.
    void Bind(LuaHost& host);

    // Attaches the render side; entities created from then on (and existing
    // ones, on Flush) become renderer instances. dataRoot is the mounted
    // data directory.
    void AttachRenderer(EntityRenderer* entities, TextureCache* textures,
                        const std::string& dataRoot);

    // Attaches the simulation: WORLD.LoadMap builds the static world the
    // moment the scripts ask for it (entity bodies follow through
    // ENTITY.PO_Create during the same level load), and PO_* becomes real.
    void AttachPhysics(PhysicsWorld* physics, const std::string& dataRoot);

    // Attaches the effect renderers: PARTICLE.* and BILLBOARD.SetupCorona
    // become real.
    void AttachParticles(ParticleRenderer* particles, EmitterLibrary* library);
    void AttachBillboards(BillboardRenderer* billboards);

    // Writes the simulation's poses back into the registry and the renderer.
    // Call after every PhysicsWorld::Update. activeOnly=false sweeps sleeping
    // bodies too - needed once after the load-time settle, because settled
    // means asleep and asleep is exactly what the per-frame sync skips.
    void SyncFromPhysics(bool activeOnly = true);

    // The map mesh WORLD.LoadMap loaded (physics path); null until then.
    const MapMesh* map() const { return mapLoaded_ ? &map_ : nullptr; }

    const WorldState& world() const { return world_; }
    void ClearLoadRequest() { world_.loadRequested = false; }
    const std::unordered_map<int, Entity>& entities() const { return entities_; }
    size_t created() const { return created_; }
    size_t released() const { return released_; }

    // Pushes every registry entity that has none into the attached renderer.
    void FlushToRenderer();

private:
    static ScriptEngine* From(lua_State* L);

    Entity* Find(int handle);
    void SyncPose(Entity& e);
    void CreateRendererInstance(Entity& e);
    void UpdateAttachments(Entity& e);
    bool SplitPackSource(const std::string& source, std::string& packName) const;

    // --- natives ---
    static int L_Create(lua_State* L);
    static int L_Release(lua_State* L);
    static int L_SetPosition(lua_State* L);
    static int L_GetPosition(lua_State* L);
    static int L_SetRotationQ(lua_State* L);
    static int L_GetRotationQ(lua_State* L);
    static int L_SetOrientation(lua_State* L);
    static int L_GetOrientation(lua_State* L);
    static int L_SetScale(lua_State* L);
    static int L_EnableDraw(lua_State* L);
    static int L_PARTICLE_AddEmitter(lua_State* L);
    static int L_PARTICLE_SetupEmitter(lua_State* L);
    static int L_NoOpNative(lua_State* L);
    static int L_BILLBOARD_SetupCorona(lua_State* L);
    static int L_GetVelocity(lua_State* L);
    static int L_PO_Create(lua_State* L);
    static int L_PO_Exist(lua_State* L);
    static int L_PO_GetMaxSphereRay(lua_State* L);
    static int L_PO_SetMass(lua_State* L);
    static int L_PO_SetFriction(lua_State* L);
    static int L_PO_SetRestitution(lua_State* L);
    static int L_PO_SetLinearDamping(lua_State* L);
    static int L_PO_SetAngularDamping(lua_State* L);
    static int L_WORLD_Init(lua_State* L);
    static int L_WORLD_AddEntity(lua_State* L);
    static int L_WORLD_FindEntityByName(lua_State* L);
    static int L_WORLD_LoadMap(lua_State* L);
    static int L_WORLD_SetupFog(lua_State* L);
    static int L_WORLD_SetFarClipDist(lua_State* L);
    static int L_WORLD_AmbientColor(lua_State* L);
    static int L_WORLD_LoadSky(lua_State* L);
    static int L_WORLD_LoadLowQualitySky(lua_State* L);
    static int L_WORLD_SetupSkyLayer(lua_State* L);
    static int L_MESH_SetDefaultDetailMaps(lua_State* L);

    LuaHost* host_ = nullptr;
    std::unordered_map<int, Entity> entities_;
    int nextHandle_ = 1;
    size_t created_ = 0, released_ = 0;
    WorldState world_;

    EntityRenderer* renderer_ = nullptr;
    TextureCache* textures_ = nullptr;
    PhysicsWorld* physics_ = nullptr;
    ParticleRenderer* particles_ = nullptr;
    EmitterLibrary* emitterLib_ = nullptr;
    BillboardRenderer* billboards_ = nullptr;
    std::string dataRoot_;

    MapMesh map_;
    bool mapLoaded_ = false;
    std::unordered_map<int, int> bodyToEntity_;   // body slot -> entity handle
    std::vector<ScriptBodyPose> poseScratch_;
};

} // namespace painful
