#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../Assets/AnimationCache.h"
#include "../Assets/Mpk.h"
#include "../Script/LuaHost.h"
#include "../World/Level.h"
#include "../World/PhysicsWorld.h"
#include "Input.h"

namespace painful {

class BillboardRenderer;
class EmitterLibrary;
class EntityRenderer;
class ParticleRenderer;
class PlayerPawn;
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
        // REGION.BuildFromPoint volume: the points' AABB, held in the
        // entity's LOCAL space and offset by pos at test time. That is the
        // shipped convention - CreateRegion and Teleport.CBox both author
        // extents about the origin and then call ENTITY.SetPosition. CArea
        // authors world points and never positions the entity, so it would
        // need pos to stay zero; no shipped level takes that path (none sets
        // HasRegion), and CArea's points are a walk path rather than a box
        // anyway, so an AABB would be the wrong shape for it regardless.
        bool isRegion = false;
        bool playerInside = false;
        float velocity[3] = {0, 0, 0};   // ENTITY.SetVelocity, for bodyless entities
        // ENTITY.SetTimeToDie countdown in seconds; negative means no timer.
        float timeToDie = -1.f;

        // The animation clock. MDL.SetAnim hands the scripts an INDEX which
        // they keep as _CurAnimIndex and pass back to every other MDL call,
        // so the indices have to be stable per entity: a slot is appended the
        // first time an animation is named and never moves afterwards.
        //
        // Only the current animation actually runs. The scripts only ever
        // query the index they last set, so a query about any other slot
        // answers with that track's length and a time of zero rather than
        // pretending to keep a clock per track.
        struct AnimSlot {
            std::string name;
            float length = 0.f;
            // The cache owns this and is node-based, so the pointer stays
            // valid as more animations load.
            const Animation* anim = nullptr;
        };
        std::vector<AnimSlot> animSlots;
        int animIndex = -1;      // slot now playing, -1 = none
        float animTime = 0.f;    // seconds into it
        float animScale = 1.f;   // MDL.Get/SetAnimTimeScale; 0 pauses
        bool animLoop = false;
        float regionMin[3] = {0, 0, 0};
        float regionMax[3] = {0, 0, 0};
        // The Actions bitmask ENTITY.PO_SetAction stores on the physics
        // object (PlayerAction reads it from this+0x78). The mover consumes
        // only Act::MoveMask; the rest is the scripts talking to themselves
        // through PO_IsActionState - which is how the weapon code learns
        // that fire was held this tick.
        uint32_t action = 0;
        bool jumpedLastAction = false;
        // ENTITY.Add/RemoveFromIntersectionSolver: whether line traces can
        // see this entity. The scripts bracket a trace with a Remove/Add pair
        // so a shot does not hit whatever fired it - that is what the
        // "intersection solver" is, a trace visibility set.
        bool inSolver = true;
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

    // Attaches the player pawn: CreatePlayer and the PO_ pawn family become
    // real, and the game loop can walk.
    void AttachPlayer(PlayerPawn* pawn);
    int playerHandle() const { return playerHandle_; }

    // Attaches the keyboard and mouse: the INP.* family starts answering
    // truthfully, so CPlayer:Tick builds a real action mask and hands it to
    // PLAYER.ExecAction. Without this the scripts see no keys held, which is
    // a valid state (a standing player), not an error.
    void AttachInput(Input* input);

    // The frame delta PLAYER.ExecAction moves the pawn by. The original
    // takes the engine's own frame time inside PlayerAction rather than the
    // delta the script was called with; set this before the tick chain.
    void SetFrameDelta(float dt) { frameDelta_ = dt; }
    // ENTITY.PO_Enable on the player toggles between the walking pawn and
    // free flight - the scripts' own SwitchPlayerToPhysics semantics.
    bool pawnEnabled() const { return pawnEnabled_; }
    // Writes the pawn's position back into the player entity, so the scripts
    // read where the player actually is. Call after every pawn move.
    void SyncPlayerFromPawn();

    // Tests the player against every region volume and posts
    // REGION_ENTERED / REGION_LEFT into Game_GetMsg on the transitions.
    // Call once per frame after the ticks.
    void TickTriggers();

    // Counts down ENTITY.SetTimeToDie and reaps whatever has run out. Call
    // once per frame; transient debris and spent projectiles depend on it.
    void TickLifetimes(float dt);

    // Advances every entity's animation clock. Call once per frame BEFORE
    // the tick chain: CActor:Tick reads the time the same frame.
    void TickAnimations(float dt);

    // MOUSE.Lock(true), which the engine itself does at the play transition
    // (right after seating the camera from Lev.Pos/Lev.Ang). It is not a
    // mirror of the window's capture state: Game:Tick branches on the lock,
    // and CLevel:Synchronize uses it to decide which way the camera and the
    // level record synchronise. Unlocked, the level pushes its stored pose
    // into the camera - which is how a level seats the view at load. Locked,
    // the camera is authoritative and the level follows it.
    void SetMouseLocked(bool locked) { mouseLocked_ = locked; }

    // Takes the pose the scripts last pushed through CAM.SetPos/SetAng, if
    // any, so the game loop can adopt it. Returns false when they have not
    // moved the camera since the last call.
    bool TakeCameraPose(float pos[3], float& yaw, float& pitch);

    // The camera the CAM.* reads report (position, yaw and pitch in
    // radians). The game loop feeds it every frame; headless runs keep the
    // defaults.
    void SetCameraPose(const float pos[3], float yaw, float pitch) {
        for (int i = 0; i < 3; ++i) camPos_[i] = pos[i];
        camYaw_ = yaw;
        camPitch_ = pitch;
    }

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
    static int L_SetVelocity(lua_State* L);
    static int L_SetTimeToDie(lua_State* L);
    static int L_PO_Hit(lua_State* L);
    static int L_WORLD_HitPhysicObject(lua_State* L);
    void ReleaseEntity(int handle);
    static int L_PO_Create(lua_State* L);
    static int L_PO_Exist(lua_State* L);
    static int L_PO_GetMaxSphereRay(lua_State* L);
    static int L_PO_SetMass(lua_State* L);
    static int L_PO_SetFriction(lua_State* L);
    static int L_PO_SetRestitution(lua_State* L);
    static int L_PO_SetLinearDamping(lua_State* L);
    static int L_PO_SetAngularDamping(lua_State* L);
    static int L_CreatePlayer(lua_State* L);
    static int L_PO_SetPawnHeadPos(lua_State* L);
    static int L_PO_GetPawnHeadPos(lua_State* L);
    static int L_PO_GetPawnFloorPos(lua_State* L);
    static int L_GetDimensions(lua_State* L);
    static int L_IsDrawEnabled(lua_State* L);
    static int L_PLAYER_GetDistanceFromPoint(lua_State* L);
    static int L_REGION_BuildFromPoint(lua_State* L);
    static int TraceCommon(lua_State* L, bool staticOnly);
    bool TraceRay(const float from[3], const float to[3], PhysicsWorld::RayHit& hit,
                  bool staticOnly) const;
    int EntityForBody(int bodySlot) const;
    static int L_WORLD_LineTrace(lua_State* L);
    static int L_WORLD_LineTraceFixedGeom(lua_State* L);
    static int L_AddToIntersectionSolver(lua_State* L);
    static int L_RemoveFromIntersectionSolver(lua_State* L);
    static int L_IsFixedMesh(lua_State* L);
    static int L_SetPosAndRotRelativeToCamera(lua_State* L);
    static int L_GetType(lua_State* L);
    static int L_PARTICLE_SetEvolve(lua_State* L);
    static int L_MDL_SetAnim(lua_State* L);
    static int L_MDL_GetAnimLength(lua_State* L);
    static int L_MDL_GetAnimTime(lua_State* L);
    static int L_MDL_SetAnimTime(lua_State* L);
    static int L_MDL_GetAnimTimeScale(lua_State* L);
    static int L_MDL_SetAnimTimeScale(lua_State* L);
    static int L_MDL_ResetFrame(lua_State* L);
    static int L_MDL_LoadAnim(lua_State* L);
    static int L_MDL_GetAnimMovement(lua_State* L);
    static int L_MDL_TransformPointByJoint(lua_State* L);
    static int L_MDL_GetJointPos(lua_State* L);
    static const Entity::AnimSlot* AnimSlotArg(const Entity* e, lua_State* L, int arg);
    static int L_INP_Key(lua_State* L);
    static int L_INP_Action(lua_State* L);
    static int L_INP_UIAction(lua_State* L);
    static int L_INP_IsFireSwitched(lua_State* L);
    static int L_INP_LoadBindings(lua_State* L);
    static int L_INP_Reset(lua_State* L);
    static int L_PO_SetAction(lua_State* L);
    static int L_PO_AddAction(lua_State* L);
    static int L_PO_IsActionState(lua_State* L);
    static int L_PO_JumpedInLastAction(lua_State* L);
    static int L_PLAYER_ExecAction(lua_State* L);
    static int L_PLAYER_FloorCheck(lua_State* L);
    static int L_MOUSE_Lock(lua_State* L);
    static int L_MOUSE_IsLocked(lua_State* L);
    static int L_CAM_GetPos(lua_State* L);
    static int L_CAM_SetPos(lua_State* L);
    static int L_CAM_SetAng(lua_State* L);
    static int L_CAM_GetForwardVector(lua_State* L);
    static int L_CAM_GetAng(lua_State* L);
    static int L_CAM_GetAngRad(lua_State* L);
    static int L_CAM_GetRawRotation(lua_State* L);
    static int L_MOUSE_GetDelta(lua_State* L);
    static int L_MOUSE_SetSensitivity(lua_State* L);
    static int L_CAM_SetPositionDisplacement(lua_State* L);
    static int L_INP_GetActionStatus(lua_State* L);
    static int L_PLAYER_GetCameraFix(lua_State* L);
    static int L_PO_IsEnabled(lua_State* L);
    static int L_PO_Enable(lua_State* L);
    static int L_GetPlayerSpeed(lua_State* L);
    static int L_SetPlayerSpeed(lua_State* L);
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
    PlayerPawn* pawn_ = nullptr;
    Input* input_ = nullptr;
    float frameDelta_ = 1.f / 60.f;
    int playerHandle_ = 0;
    bool pawnEnabled_ = true;
    // What MOUSE.Lock/IsLocked report. The SCRIPTS own this - they lock on
    // entering play and unlock for menus - and nothing on the C++ side may
    // write it: Game:Tick branches on it, and an unlocked mouse runs the
    // EDITOR tick, where the player globals never update. Driving it from
    // the window's click-to-capture state is what once left the player
    // falling at the world origin. True at boot, as the engine enters play.
    // False until the play transition, because a level LOADS unlocked: that
    // is what lets CLevel:Synchronize push Lev.Pos/Lev.Ang into the camera
    // and seat the view where the level says. Starting locked inverts the
    // synchronise and overwrites Lev.Pos with wherever our camera happened
    // to be - which spawned the player at the world origin, since
    // CreatePlayerSP seats them at Lev.Pos.
    bool mouseLocked_ = false;
    bool camPoseDirty_ = false;
    float camPos_[3] = {0, 0, 0};
    float camYaw_ = 0.f, camPitch_ = 0.f;
    float camDisplacement_[3] = {0, 0, 0};
    float playerSpeedOverride_ = -1.f;
    float jumpStrengthOverride_ = -1.f;
    std::string dataRoot_;

    MapMesh map_;
    bool mapLoaded_ = false;
    std::unordered_map<int, int> bodyToEntity_;   // body slot -> entity handle
    // Body slots currently taken out of the intersection solver. Kept as a
    // list rather than rebuilt per trace: a shotgun fires a dozen traces in
    // one frame and the set is only ever a couple of entities deep.
    std::vector<int> excludedSlots_;
    std::vector<int> expired_;   // scratch for TickLifetimes
    AnimationCache animations_;
    std::vector<ScriptBodyPose> poseScratch_;
};

} // namespace painful
