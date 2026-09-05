#pragma once
#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include "../Assets/AnimationCache.h"
#include "../Assets/SkeletonCache.h"
#include "../Assets/Waypoints.h"
#include "Console.h"
#include "MenuSystem.h"
#include "../Assets/Mpk.h"
#include "../Assets/Rde.h"
#include "../Assets/Hke.h"
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
class AudioEngine;
class HudRenderer;

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
        // A world-mesh object promoted to a rigid body (name has "phys"):
        // the index into map_.objects it was built from, -1 otherwise.
        int activeMesh = -1;
        float activeOrigin[3] = {0, 0, 0};   // where it was built; PAINFUL_ACTIVE_TRACE
        int spriteSlot = -1;        // BillboardRenderer slot (Billboard type)
        // ParticleRenderer slots, indexed by the per-entity emitter index the
        // scripts hold (-1 entries when running headless).
        std::vector<int> emitterSlots;
        // What made each slot, kept so a save can remake it: AddEmitter's
        // file, SetupEmitter's transform, SetEvolve, Die. Parallel to
        // emitterSlots. Docs/Reference/LuaHost.md, "Saving".
        struct EmitterRec {
            std::string file;
            float scale = 1.f;
            float offset[3] = {0, 0, 0};
            float rotDeg[3] = {0, 0, 0};
            bool setup = false;
            bool evolveSet = false, evolve = false;
            bool stopped = false;
        };
        std::vector<EmitterRec> emitterRecs;
        // BILLBOARD.SetupCorona's arguments, for the same reason.
        bool hasCorona = false;
        float coronaArgs[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        std::string coronaTex;
        uint32_t coronaColor = 0;
        int coronaBlend = 1;
        bool coronaSpriteOnly = false;
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
        // ENTITY.RegisterChild: entities bound to this one, chiefly the looping
        // sounds BindSoundToEntity attaches. GetChildByName searches these by
        // SOUND name, which SND.Setup3D is what supplies.
        std::vector<int> children;

        // ENTITY.RegisterChild's fifth argument, which DEFAULTS TO TRUE: the
        // engine writes it to child+0x11a (0x1012fad0) and Entity::
        // KillAllChildren (0x1d2bc0) deletes a child only when it is set.
        //
        // Every pickup relies on it. IShotgunFZ:Client_OnCreateEntity makes a
        // SEPARATE billboard entity for its corona and registers it as a child
        // with four arguments - so the flag is on - and expects it to go when
        // the pickup does. Without honouring it the corona outlives the thing
        // it was drawn for and hangs in the air.
        bool dieWithParent = true;
        std::string soundName;
        // A Sound entity's own voice, set up by SND.Setup3D and started by
        // SND.Play. The name alone is what the child-by-name lookup needs; the
        // rest is what makes it audible and keeps it on the thing it follows.
        float soundDist1 = 10.f;         // Setup3D arg 3, default 10
        float soundDist2 = 20.f;         // Setup3D arg 4, default 20
        float soundInterval = -1.f;      // arg 5: >= 0 loops, -1 plays once
        float soundStartIn = -1.f;       // SND.Play's delay, counted down
        int soundVoice = 0;              // the AudioEngine voice, 0 when silent
        bool soundPlaying = false;       // SND.Play happened and SND.Stop has not
        // ENTITY.EnableCollisions(e, on, minTime=0.4, minStrength=0.6) - whether
        // this body reports contacts to the scripts, and how often. The cooldown
        // is what stops a prop resting against another from reporting forever.
        bool collisionsOn = false;
        float collisionMinTime = 0.4f;
        float collisionMinStrength = 0.6f;
        float collisionCooldown = 0.f;
        // MDL.SetMeshVisibility, kept so it survives the renderer instance
        // being rebuilt. name -> shown.
        std::map<std::string, bool> hiddenMeshes;
        // What this entity is bound to, and where on it.
        //
        // ENTITY.RegisterChild names the parent; PARTICLE.SetParentOffset gives
        // the offset and, usually, the JOINT it hangs off. CActor:BindFX does
        // both for every effect a monster carries. Without the pair being
        // acted on, a bound effect never moves: it stays wherever it was made,
        // which is the world origin - and DevilMonk decides whether to poison
        // the player by measuring the distance to its gas cloud, so a cloud
        // left at the origin poisons anyone who spawns there.
        int parent = 0;                  // 0 = not bound to anything
        float parentOffset[3] = {0, 0, 0};
        std::string parentJoint;         // empty = the parent's own transform
        int parentJointIndex = -2;       // -2 not resolved, -1 absent
        bool parentBound = false;        // SetParentOffset was called
        // SetParentOffset's optional 9th..11th arguments: an Euler rotation the
        // effect carries relative to its joint (ParticleEffect+0xc88/+0xc8c).
        // Without it a jointed effect takes the PARENT's rotation, not the
        // joint's. ParticleEffect::Tick, 0x101e59a0.
        bool parentRotBound = false;
        float parentRotWXYZ[4] = {1, 0, 0, 0};
        // A projectile: PO_Create was given ECollisionGroups.Noncolliding, so
        // this is moved along its velocity by TickProjectiles rather than
        // simulated. The scripts find their own hits with a line trace.
        // The ECollisionGroups value PO_Create was given, which the scripts
        // read back: PainHead skips a hit whose group is Noncolliding or
        // Particles, and sticks into one that is Fixed.
        int collisionGroup = 0;
        // ENTITY.PO_SetMovedByExplosions. A grenade turns this off for itself so
        // its own blast does not launch it; Slab barriers and lifts likewise.
        bool movedByExplosions = true;
        bool isProjectile = false;
        // ENTITY.PO_SetGrenade: flag 0x20 at PhysicsObject+0x74. A flagged body
        // gets PhysicsObject::FixGrenadeFlight after every step - the trace
        // from where the entity was to where the body is now, with the
        // reflect-and-slow on a hit. See TickGrenades.
        bool isGrenade = false;
        // ENTITY.PO_SetFriction / PO_SetRestitution. The engine keeps these on
        // its body wrapper (+0x38/+0x3c) and never hands them to Havok: the
        // contact material is fixed at creation, and the wrapper values feed
        // the engine's own flight code (FixGrenadeFlight reads friction).
        // Defaults are CreatePhysicsObject's (0x101999f0).
        float bodyFriction = 0.1f;
        float bodyRestitution = 0.001f;
        // PO_Create's shape arguments and the dressing that followed, so a
        // save can make the body again: mass, freedom of rotation, damping,
        // pinned. -1 means "never set". Docs/Reference/LuaHost.md, "Saving".
        int bodyType = 0;
        float bodyArgScale = -1.f;
        float bodyMass = -1.f;
        int bodyFreedomMode = -1;
        float bodyFreedomSoft = 1.f;
        float bodyLinDamp = -1.f, bodyAngDamp = -1.f;
        bool bodyPinned = false;
        bool bodyNonColliding = false;
        int bodyGravity = -1;            // PO_EnableGravity: 0/1, -1 never called
        // ENTITY.PO_EnableGravity. A projectile is not in the solver, so the
        // body's gravity factor is a value nothing reads - TickProjectiles has
        // to integrate this itself, or Stake:Tick's arc never happens.
        bool gravityOn = false;
        // ENTITY.PO_Enable / PO_IsEnabled. This is how a projectile STOPS:
        // Stake:Tick answers a hit with PO_Enable(false), moves itself half a
        // length back into the surface and returns, and every later tick exits
        // at its opening PO_IsEnabled check until TimeToLive removes it. That
        // is the whole of "the stake nails to the wall". Deactivating the Jolt
        // body is not enough to express it - the body still exists, so the
        // question came back true and the stake sailed on through the floor.
        bool poEnabled = true;
        // ENTITY.SetAngularVelocity: a world-space axis scaled by radians per
        // second (PhysicsObject::SetAngularVel, 0x10132260). The stake tumbles
        // nose-down with this once it starts to fall.
        float angVel[3] = {0, 0, 0};
        // ENTITY.SetPosAndRotRelativeToCamera: a viewmodel, held in CAMERA
        // space. The world pose is re-derived from the final camera each frame
        // rather than baked once during the tick - the shake moves the eye
        // after the scripts have run, and a weapon placed from the older eye
        // jitters against the view by exactly the shake.
        bool viewAttached = false;
        float viewOffset[3] = {0, 0, 0};
        float viewAngles[3] = {0, 0, 0};
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
            // The movement curve: SetAnim's 6th and 7th arguments, which are
            // how an animation carries the actor that plays it. The mask is
            // Definitions.lua's MovingCurve (ETransX 1, ETransY 2, ETransZ 4,
            // ERot 8) and zero means the animation does not move anything;
            // the bone defaults to "ROOOT", which is Engine.dll's own default
            // for that argument and the name of bone 0 in the shipped rigs.
            uint32_t curveMask = 0;
            std::string curveBone;
            int curveBoneIndex = -2;      // -2 = not looked up yet, -1 = absent
        };
        std::vector<AnimSlot> animSlots;
        int animIndex = -1;      // slot now playing, -1 = none
        float animTime = 0.f;    // seconds into it
        float animScale = 1.f;   // MDL.Get/SetAnimTimeScale; 0 pauses
        bool animLoop = false;

        // Where this entity's bones are, in MODEL space, for the animation
        // and time above. Built on demand and only when something asks: most
        // entities are never queried for a joint, and an actor is queried
        // several times in one tick, so it is cached against what produced it
        // rather than recomputed per call or pushed every frame.
        struct Pose {
            const Animation* anim = nullptr;   // what `tracks` was resolved against
            float time = -1.f;                 // what `boneWorld` was built at
            int rotVersion = -1;               // jointRotVersion it was built at
            float blendU = -1.f;               // cross-fade weight it was built at
            std::vector<const AnimTrack*> tracks;
            std::vector<Mat4> boneWorld;
        };
        Pose pose;

        // The animation being faded OUT of, frozen at the moment the new one
        // started. MDL.SetAnim's fifth argument is how long that fade lasts -
        // the templates carry one per animation, and CActor falls back to
        // 0.201 s - and without it an actor snaps from walking to attacking
        // inside a single frame.
        const Animation* blendFrom = nullptr;
        float blendFromTime = 0.f;
        std::vector<const AnimTrack*> blendFromTracks;
        float blendLeft = 0.f;      // seconds still to fade
        float blendTotal = 0.f;     // what it started at, for the weight

        // MDL.ApplyJointRotation, one entry per bone the scripts steer. They
        // pass an absolute angle every frame (a gun recomputes its barrel
        // pitch from scratch each tick), so a call SETS the bone's rotation
        // rather than adding to it. The version bumps on any change, which is
        // what tells the cached pose above to rebuild even though the
        // animation and its time have not moved.
        std::vector<JointOverride> jointRot;
        int jointRotVersion = 0;
        float regionMin[3] = {0, 0, 0};
        float regionMax[3] = {0, 0, 0};
        // The Actions bitmask ENTITY.PO_SetAction stores on the physics
        // object (PlayerAction reads it from this+0x78). The mover consumes
        // only Act::MoveMask; the rest is the scripts talking to themselves
        // through PO_IsActionState - which is how the weapon code learns
        // that fire was held this tick.
        uint32_t action = 0;
        bool jumpedLastAction = false;

        // --- monster locomotion -------------------------------------------
        // ENTITY.PO_SetMonsterType (0x101313C0 sets bit 2 at
        // PhysicsObject+0x74): the physics step re-commands this body's
        // velocity every tick - PhysicsWorld::StepCharacters. Until the flag
        // arrives the body is an ordinary dynamic prop.
        bool isMonster = false;
        // ENTITY.PO_Move's vector (PhysicsObject+0x34), a VELOCITY: CActor
        // passes `mv * (1/delta)`. Forwarded to the character body; kept here
        // for an actor that has none yet.
        float moveWish[3] = {0, 0, 0};
        // ENTITY.PO_SetMonsterMovementConst's two arguments (0x10130920:
        // GetFloat(2, 0.5) to +0x6c, GetBool(3, false) to +0x70): the share of
        // solver-added velocity kept per tick, and "do not check floors".
        float monsterMoveConst = 0.5f;
        bool monsterMoveFlag = false;
        bool monsterFlying = false;      // ENTITY.PO_SetFlying, +0x75 bit 3

        // ENTITY.PO_SetSightParams, at PhysicsObject+0x24..+0x30. Engine
        // defaults; the angles are half-angles in radians, converted from the
        // full spread in degrees the templates declare.
        float sightRange = 20.f;        // inside the cone
        float sightRange360 = 2.f;      // in every direction
        float sightHalfYaw = float(kPi);
        float sightHalfPitch = float(kPi);
        // ENTITY.Add/RemoveFromIntersectionSolver: whether line traces can
        // see this entity. The scripts bracket a trace with a Remove/Add pair
        // so a shot does not hit whatever fired it - that is what the
        // "intersection solver" is, a trace visibility set.
        bool inSolver = true;
        // The RAGDOLL's own line-trace collision, which is a SEPARATE switch
        // from the body's. The engine keeps the two objects at different
        // offsets on the entity - PhysicsObject at +0xac, Ragdoll at +0x7b8 -
        // and gives each its own EnableLineTraceCollision:
        // AddToIntersectionSolver (0x101349a0) sets both, while
        // AddRagdollToIntersectionSolver (0x10134830) sets only this one.
        // TraceLimbs honours this flag; the body exclusion honours inSolver.
        bool ragdollInSolver = true;
        // Joints MDL.EnableJoint has switched OFF, which takes that limb out
        // of the ragdoll entirely. The scripts use it on a weapon the monster
        // has already thrown - EvilMonkV2 does it to axeL/axeR inside
        // CustomOnGib - so the bone stays in the rig while its body stops
        // being part of the ragdoll.
        std::vector<int> disabledJoints;
        // The ragdoll this actor has been handed to, or -1. MDL.EnableRagdoll
        // creates it; while it exists the SOLVER owns the pose and the
        // animation does not, which is what PosedBones checks.
        int ragdollSlot = -1;
        // MDL.SetRagdollMovedByExplosions: bit 0x10 of the Ragdoll's flag byte
        // (Ragdoll::IsMovedByExplosions 0x1019CBB0). FUN_101B0DC0 tests it
        // before doing ANYTHING to a ragdoll - push or damage - so a corpse
        // with it off is invisible to blasts. CActor:CreateGib clears it on a
        // fresh gib for two ticks, so the rocket that made the gib does not
        // also launch it, then restores it and bursts the gib itself.
        bool ragdollMovedByExplosions = true;

    // The death spin, and the shot that caused it.
    //
    // A killing shot lands BEFORE the ragdoll exists: the shotgun fires its
    // pellets, calls PO_AccumulateRotation for each and PO_Hit for the lethal
    // one, and only afterwards does OnDamage reduce health to zero and create
    // the ragdoll. So the momentum has to be held on the entity and spent when
    // the ragdoll appears - which is exactly what the engine does, spending it
    // in Ragdoll::Activate via PhysicsObject::EffectRotateActor.
    float deathSpin = 0.f;              // PhysicsObject+0x40, yaw about Y
    float deathImpulse[3] = {0, 0, 0};  // the linear part, from PO_Hit
    float deathImpulseAt[3] = {0, 0, 0};
    bool hasDeathImpulse = false;
        // Model-space bone matrices read back from the solver. Full length -
        // the ragdoll only names a dozen or so bones and the rest have to
        // follow their nearest driven ancestor or the corpse loses its hands.
        std::vector<Mat4> ragdollPose;
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

    // The sound system. Without it every SOUND native is a silent no-op,
    // which is what a headless run wants.
    void AttachAudio(AudioEngine* audio) { audio_ = audio; }

    // The 2D layer. Without it every HUD.* and MATERIAL.* native draws
    // nothing, which is what the original does too: each of them opens with a
    // `if (GEngine->renderer != 0)` guard and returns having done nothing.
    void AttachHud(HudRenderer* hud, TextureCache* textures) {
        hud_ = hud;
        hudTextures_ = textures;
        menu_.Attach(hud, textures);
        menu_.SetPauseHandler([this](bool p) { SetGamePaused(p); });
    }

    // Headless: the texture index alone, with no renderer behind it. The
    // MATERIAL natives then answer real sizes off the image headers, so the
    // scripts' HUD layout arithmetic runs exactly as it does in the window.
    // Drawing is what is missing on a headless run, not sizing.
    void AttachHudTextures(TextureCache* textures) { hudTextures_ = textures; }

    // The menu owns its own widget tree; the game loop drives and draws it.
    MenuSystem& menu() { return menu_; }
    // The console: the game loop feeds it keys and draws it; the CONSOLE
    // natives and the scripts' command dispatch meet in it.
    Console& console() { return console_; }

    // Whether the simulation is frozen. The game loop skips the actor tick and
    // the physics step while this holds, and keeps drawing.
    bool gamePaused() const { return gamePaused_; }
    // Developer mode: the two switches the shipped scripts gate their own debug
    // tooling on. IsFinalBuild answers false while this is set, and the game
    // loop sets the global debugMarek alongside it.
    void SetDevMode(bool on) { devMode_ = on; }
    bool devMode() const { return devMode_; }
    // Pausing also silences what is audible, and unpausing brings back exactly
    // that set - the menu's own sounds keep playing. See ScriptEngine.cpp.
    void SetGamePaused(bool p);
    // The screen size the scripts read back from R3D.ScreenSize, and the size
    // the font scale is measured against.
    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }
    // The modes R3D.GetAvailableResolutions reports; the VideoOptions screen
    // builds its resolution row straight out of this and dies on a nil.
    void SetResolutions(std::vector<std::string> modes) {
        resolutions_ = std::move(modes);
    }
    // R3D.ApplyVideoSettings(resolution, fullscreen, ...): the app owns the
    // window, so the mode change is handed out. Cfg.Resolution is "WxH".
    void SetVideoModeHandler(std::function<void(int, int, bool)> handler) {
        setVideoMode_ = std::move(handler);
    }
    // The world renderer's per-object visibility, for a destructible's intact
    // twin: hidden when the pieces take over. Headless runs leave it unset.
    void SetWorldObjectVisibility(std::function<void(size_t, bool)> handler) {
        worldObjectVisible_ = std::move(handler);
    }
    // R3D.SetCameraFOV / GetCameraFOV: Cfg.FOV, the HORIZONTAL field of view
    // in degrees (Game:Init applies it; PainMenu:OpenMenu sets 90 for the
    // menu and restores it). The app derives the vertical angle for the
    // window's aspect each frame.
    float cameraFov() const { return cameraFov_; }
    static int L_CONSOLE_Activate(lua_State* L);
    static int L_CONSOLE_IsActive(lua_State* L);
    static int L_CONSOLE_AddMessage(lua_State* L);
    static int L_CONSOLE_Print(lua_State* L);
    static int L_CONSOLE_SetCurrentText(lua_State* L);
    static int L_CONSOLE_GetCurrentText(lua_State* L);
    static int L_CONSOLE_GetCursorPos(lua_State* L);
    static int L_CONSOLE_SetFont(lua_State* L);
    static int L_CONSOLE_SetMPMsgColor(lua_State* L);
    static int L_CONSOLE_SetMPMsgPosition(lua_State* L);
    static int L_CONSOLE_SetMPMsgFont(lua_State* L);
    static int L_R3D_SetCameraFOV(lua_State* L);
    static int L_R3D_GetCameraFOV(lua_State* L);
    static int L_R3D_ApplyVideoSettings(lua_State* L);
    int screenWidth() const { return screenW_; }
    int screenHeight() const { return screenH_; }

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

    // --- save / load (WORLD.SaveGame / LoadGame) ---------------------------
    // The engine-side half of a save: every entity with what remakes it, the
    // player's pawn, the camera. The scripts save their own tables beside it.
    // ScriptSave.cpp; Docs/Reference/LuaHost.md, "Saving".
    bool SaveWorld(const std::string& path);
    bool LoadWorld(const std::string& path);
    // The app polls this once per frame: WORLD.LoadMap ran since last asked,
    // so the level renderer has to be rebuilt. hasMap is false for the empty
    // "NoName" level; fromSave says LoadWorld followed, in which case the
    // world must not be settled or the player re-spawned.
    bool TakeLevelChange(std::string& levelName, bool& hasMap, bool& fromSave);

    // Tests the player against every region volume and posts
    // REGION_ENTERED / REGION_LEFT into Game_GetMsg on the transitions.
    // Call once per frame after the ticks.
    // Advances every projectile along its own velocity. Straight line, constant
    // speed, no solver - which is why every shot in the original lands the
    // same way.
    void TickProjectiles(float dt);
    // Re-places every view-attached entity from the camera as it will actually
    // be rendered. Called after the tick chain has settled the camera.
    void UpdateViewAttached();
    // Places every entity bound with RegisterChild + SetParentOffset. Run
    // after the parents have moved for the frame, or the effects lag them.
    void UpdateAttached();

    // The per-limb hitboxes of every model near a point, posed and in world
    // space, as wireframe. What a shot SHOULD be tested against, drawn so it
    // can be compared against what it currently is tested against.
    void CollectHitboxLines(const float around[3], float radius,
                            std::vector<DebugLine>& out);
    void PlaceViewAttached(Entity& entity);
    void TickTriggers();

    // Counts down ENTITY.SetTimeToDie and reaps whatever has run out. Call
    // once per frame; transient debris and spent projectiles depend on it.
    void TickLifetimes(float dt);
    // Contacts the physics step recorded, reported to the scripts as
    // COLLISION_WITH_OTHER_ENTITY.
    void TickCollisions(float dt);
    // PhysicsObject::FixGrenadeFlight (0x1018d990) for every PO_SetGrenade
    // body: run right after the physics step, before the bodies are read back.
    void TickGrenades();
    // Registers a world-object entity for every water surface in the loaded map.
    void BuildWaterSurfaces();
    // Where the segment first crosses a water surface, if it does.
    bool TraceWater(const float from[3], const float to[3], float& t, int& entity) const;
    // Bound 3D sounds: start the delayed ones, follow what they hang off.
    void StartBoundSound(Entity& e);
    void TickSounds(float dt);

    // Per-frame monster bookkeeping (limb shadowing, test hooks). The walking
    // itself is PhysicsWorld::StepCharacters.
    void TickMonsters(float dt);

    // The sizer's working scalar k and the root offset, from the ROOOT joint.
    bool MonsterBodyScale(Entity& e, float& k, float& rootOffsetY);

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
    // Reads every active ragdoll back out of the solver into the pose the
    // renderer draws, and moves the entity along with its own corpse. Once per
    // frame, AFTER the physics step.
    void TickRagdolls();

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

    // The entity's bones in model space at its current animation time,
    // rebuilding only when the animation or the time has moved. Null when the
    // entity has no model, the model has no skeleton, or nothing is playing -
    // all ordinary answers, and every caller has to handle them because the
    // scripts ask boneless props for joints all the time.
    const std::vector<Mat4>* PosedBones(Entity& e);
    // The travel one animation's curve bone has accumulated at a given time,
    // on the axes it declares. Both sides of a cross-fade need it.
    void CurveOffset(Entity& e, const SkeletonCache::Entry* skel, int slotIndex,
                     const std::vector<const AnimTrack*>& tracks, float time,
                     float out[3]);

    // Root motion for one animation slot over `delta` seconds, masked to the
    // axes its movement curve declares. Writes nothing when the slot has no
    // curve, which is the common case.
    void AnimMovement(Entity& e, int index, float delta, float out[3]);
    static int ResolveCurveBone(Entity::AnimSlot& slot, const SkeletonCache::Entry& skel);

    // Bone-local point to WORLD, through the entity's own transform. Used by
    // every joint query, so they cannot disagree about the entity's placement.
    bool JointToWorld(Entity& e, int joint, const float local[3], float out[3]);
    // The bone's orientation composed with the entity's own: a world rotation,
    // engine (w,x,y,z). False when the entity has no such bone.
    bool JointWorldRotation(Entity& e, int joint, float outWXYZ[4]);
    int JointIndexByName(Entity& e, const std::string& name);
    // The limb boxes of a model, derived once from its .rde and skin weights.
    // An empty vector is cached too - a model with no ragdoll is an answer.
    const std::vector<LimbBounds>* Hitboxes(const std::string& model);
    // Each ragdoll body relative to the bone that drives it, per model.
    const std::vector<Mat4>& RagdollOffsets(const std::string& model,
                                            const std::vector<std::string>& parts,
                                            const Hke& def,
                                            const SkeletonCache::Entry& skel);
    // The ragdoll definition of a model, parsed once and kept. Null when the
    // model has no .hke or has only the binary form.
    const Hke* RagdollDef(const std::string& model);
    // Is one joint connected to another through the ragdoll's constraint
    // graph, honouring whatever MDL.EnableJoint has switched off? This is what
    // separates a limb from a weapon: see Hke::Linked.
    bool JointsLinked(Entity& e, int a, int b);
    // The bone name a joint index refers to, or empty.
    std::string JointName(Entity& e, int joint);

    // What a line trace found on one posed limb box: which actor was hit and
    // which of its bones. The joint index is what MDL.GetJointFromHavokBody
    // answers and what every weak-point script branches on.
    struct LimbHit {
        int entity = 0;
        int joint = -1;
        float distance = 0.f;
        float point[3] = {0, 0, 0};
        float normal[3] = {0, 0, 0};
    };

    // THE SHOOTING SHAPE, as opposed to the walking one.
    //
    // A monster's movement body is three stacked spheres sized off its rig's
    // root - what you bump into and cannot stand inside. Testing a shot
    // against it makes a headshot and a shot at the ankle the same event.
    // These are the boxes the .rde names, one per limb, posed by the same
    // skinning matrices the draw already computed.
    //
    // maxDistance clamps the search to whatever the world trace already found,
    // so a shot that stops at a wall cannot reach the monster behind it. Pass
    // a NEGATIVE maxDistance for the whole segment.
    bool TraceLimbs(const float from[3], const float to[3], float maxDistance,
                    LimbHit& out);

    // An opaque body handle naming one limb of one actor - what the scripts
    // carry as `he` out of a trace and hand back to PHYSICS.GetHavokBodyInfo.
    // A registry rather than a bit-packing, so the entity handle stays
    // whatever the entity registry made it.
    int LimbHandle(int entity, int joint);
    bool LimbFromHandle(int handle, int& entity, int& joint) const;
    void PlaceAttached(Entity& e);
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
    static int L_PO_Remove(lua_State* L);
    static int L_PO_GetCollisionGroup(lua_State* L);
    static int L_PO_IsFixed(lua_State* L);
    static int L_R3D_DistToLine(lua_State* L);
    static int L_EnableDraw(lua_State* L);
    // ENTITY.EnableDraw's alsoChildren: a bound effect is a child ENTITY, so
    // hiding the parent alone leaves it burning.
    void SetDrawEnabled(Entity& e, bool on, bool alsoChildren, int depth);
    static int L_PARTICLE_AddEmitter(lua_State* L);
    static int L_PARTICLE_SetupEmitter(lua_State* L);
    static int L_PARTICLE_SetParentOffset(lua_State* L);
    static int L_NoOpNative(lua_State* L);
    static int L_BILLBOARD_SetupCorona(lua_State* L);
    static int L_GetVelocity(lua_State* L);
    static int L_SetVelocity(lua_State* L);
    static int L_SetAngularVelocity(lua_State* L);
    static int L_SetTimeToDie(lua_State* L);
    static int L_PO_Hit(lua_State* L);
    static int L_WORLD_HitPhysicObject(lua_State* L);
    static int L_WORLD_GetLastExplodedEntities(lua_State* L);
    static int L_ENTITY_ExplodeItem(lua_State* L);
    void ReleaseEntity(int handle);
    static int L_PO_Create(lua_State* L);
    static int L_PO_Exist(lua_State* L);
    static int L_PO_Move(lua_State* L);
    static int L_PO_SetMonsterType(lua_State* L);
    static int L_PO_SetFlying(lua_State* L);
    static int L_PO_IsFlying(lua_State* L);
    static int L_PO_SetMonsterMovementConst(lua_State* L);
    static int L_PO_IsOnFloor(lua_State* L);
    static int L_PO_SetSightParams(lua_State* L);
    static int L_SeesEntity(lua_State* L);
    static int L_SOUND_Play2D(lua_State* L);
    static int L_SOUND_Play3D(lua_State* L);
    static int L_SOUND_SetSoundProperties(lua_State* L);
    static int L_SND_Create2D(lua_State* L);
    static int L_SND_Create3D(lua_State* L);
    static int L_SND_Play(lua_State* L);
    static int L_SND_Stop(lua_State* L);
    static int L_SND_Pause(lua_State* L);
    static int L_SND_IsPlaying(lua_State* L);
    static int L_SND_SetVolume(lua_State* L);
    static int L_SND_SetLoopCount(lua_State* L);
    static int L_SND_SetPosition(lua_State* L);
    static int L_SND_SetHearingDistance(lua_State* L);
    static int L_SND_SetSoundSpeed(lua_State* L);
    static int L_SND_Delete(lua_State* L);
    static int L_SND_Forget(lua_State* L);
    static int L_SOUND_SetPlayerPos(lua_State* L);
    static int L_SOUND_SetPlayerOrientation(lua_State* L);
    void PushListener();
    static int L_WPT_Load(lua_State* L);
    static int L_WPT_GetClosest(lua_State* L);
    static int L_WPT_GetPosition(lua_State* L);
    static int L_PATH_Create(lua_State* L);
    static int L_PATH_Release(lua_State* L);
    static int L_PATH_GetShortest(lua_State* L);
    static int L_PATH_IsFinished(lua_State* L);
    static int L_PATH_GetNextPoint(lua_State* L);

    // Can `a` see `b`: range, then the sight cone, then an unobstructed line.
    bool Sees(int ha, Entity& a, int hb, Entity& b) const;
    // GetPawnHeadPos for anything: pawn eye, character head, or position.
    void EyePoint(const Entity& e, int handle, float out[3]) const;
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
    static int L_AddRagdollToIntersectionSolver(lua_State* L);
    static int L_RemoveRagdollFromIntersectionSolver(lua_State* L);
    // The body half of the intersection solver: keeps inSolver and the
    // excludedSlots_ list in step, so a Remove cannot be leaked or doubled.
    void SetSolverBody(Entity& e, bool on);
    static int L_IsFixedMesh(lua_State* L);
    // PHYSICS.GetHavokBodyInfo(he) -> type, entity, joint. The engine's own
    // shape (0x101291a0) returns a DIFFERENT NUMBER OF VALUES per kind: 1 for
    // an unknown body, 2 for a plain physics object, 3 for a ragdoll limb.
    // That is why every caller writes `if j then` - a body that is not a limb
    // leaves the joint nil rather than reporting -1.
    static int L_PHYSICS_GetHavokBodyInfo(lua_State* L);
    // MDL.GetJointFromHavokBody(e, he) -> joint, or -1. The engine checks the
    // body belongs to THAT entity's ragdoll (0x1012d320); so does this.
    static int L_MDL_GetJointFromHavokBody(lua_State* L);
    // MDL.JointsLinked(e, a, b) -> are these two joints connected through the
    // ragdoll? MDL.EnableJoint(e, joint, on) takes one out of it.
    static int L_MDL_JointsLinked(lua_State* L);
    static int L_MDL_ApplyPointImpulseToRagdoll(lua_State* L);
    static int L_PO_ScaleInertiaTensor(lua_State* L);
    static int L_PO_AccumulateRotation(lua_State* L);
    // MDL.EnableRagdoll(e, on, group) - hand the actor to the solver, or take
    // it back. This is what death is. `seedPose` is MODEL-space bone matrices
    // to start from instead of the entity's own pose: MakeGib seeds the gib
    // with the pose of the actor it replaces.
    bool EnableRagdoll(Entity& e, bool enable, const std::vector<Mat4>* seedPose = nullptr);
    // MDL.MakeGib(e, group, velocityJoint) -> the gib's handle, or 0 when the
    // model has no `<name>_gib` ragdoll. World::GibModel, 0x10060D90.
    int MakeGib(Entity& source, int group, const char* velocityJoint);
    // The ragdoll part a joint drives, or -1 (the .hke names a dozen bones).
    int RagdollPartOfJoint(Entity& e, int joint);
    static int L_MDL_MakeGib(lua_State* L);
    static int L_MDL_SetRagdollMovedByExplosions(lua_State* L);
    static int L_MDL_RagdollSelfExplosion(lua_State* L);
    static int L_MDL_ApplyVelocitiesToAllJoints(lua_State* L);
    static int L_MDL_EnableRagdoll(lua_State* L);
    static int L_MDL_IsRagdoll(lua_State* L);
    static int L_MDL_IsRagdollActive(lua_State* L);
    static int L_ENTITY_RemoveRagdoll(lua_State* L);
    static int L_MDL_SetRagdollLinearDamping(lua_State* L);
    static int L_MDL_SetRagdollAngularDamping(lua_State* L);
    static int L_MDL_SetRagdollFriction(lua_State* L);
    static int L_MDL_EnableJoint(lua_State* L);
    // PHYSICS.RemoveHavokBodyFromIS(he, on) - one BODY out of the traces.
    static int L_PHYSICS_RemoveHavokBodyFromIS(lua_State* L);
    static int L_PHYSICS_IsHavokBodyInWorld(lua_State* L);
    static int L_PHYSICS_GetHavokBodyVelocity(lua_State* L);
    static int L_ENTITY_EnableCollisions(lua_State* L);
    static int L_WORLD_Explosion2(lua_State* L);
    static int L_PO_SetMovedByExplosions(lua_State* L);
    static int L_PO_SetGrenade(lua_State* L);
    static int L_PO_SetFreedomOfRotation(lua_State* L);
    static int L_PO_SetPinned(lua_State* L);
    static int L_PO_IsPinned(lua_State* L);
    // The blast itself: collects what it reached, pushes it, and posts one
    // EXPLOSION per entity. Docs/Reference/Physics.md carries the falloff.
    void Explosion(const float centre[3], float strength, float range,
                   double killer, double attackType, float damage);
    static int L_ENTITY_IsWater(lua_State* L);
    static int L_INP_GetTimeMultiplier(lua_State* L);
    static int L_INP_SetTimeMultiplier(lua_State* L);
    static int L_SetPosAndRotRelativeToCamera(lua_State* L);
    static int L_GetType(lua_State* L);
    static int L_PARTICLE_SetEvolve(lua_State* L);
    static int L_PARTICLE_Die(lua_State* L);
    static int L_MDL_SetAnim(lua_State* L);
    static int L_MDL_SetMeshVisibility(lua_State* L);
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
    static int L_MDL_GetJointIndex(lua_State* L);
    static int L_MDL_GetJointName(lua_State* L);
    static int L_MDL_GetJointRotation(lua_State* L);
    static int L_MDL_ApplyJointRotation(lua_State* L);
    static int L_MDL_GetVelocitiesFromJoint(lua_State* L);
    static const Entity::AnimSlot* AnimSlotArg(const Entity* e, lua_State* L, int arg);
    static int L_INP_Key(lua_State* L);
    static int L_INP_Action(lua_State* L);
    static int L_INP_UIAction(lua_State* L);
    static int L_INP_RemoveUIAction(lua_State* L);
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
    static int L_IsFinalBuild(lua_State* L);
    static int L_EDITOR_OutputText(lua_State* L);
    static int L_PO_Enable(lua_State* L);
    static int L_GetPlayerSpeed(lua_State* L);
    static int L_SetPlayerSpeed(lua_State* L);
    static int L_WORLD_Init(lua_State* L);
    static int L_WORLD_AddEntity(lua_State* L);
    static int L_WORLD_FindEntityByName(lua_State* L);
    static int L_WORLD_LoadMap(lua_State* L);
    static int L_WORLD_SaveGame(lua_State* L);
    static int L_WORLD_LoadGame(lua_State* L);
    static int L_PHYSICS_ActiveMeshGroupActivate(lua_State* L);
    static int L_PHYSICS_ActiveMeshGroupEnable(lua_State* L);
    static int L_PHYSICS_ActiveMeshGroupStaticMeshEnable(lua_State* L);
    static int L_PHYSICS_ActiveMeshGroupSetActivationParams(lua_State* L);
    // Promotes every "phys" object of the loaded map into a body and an
    // entity; called from WORLD.LoadMap once the mesh is in. "physdest"
    // pieces are held out of the world and "statdest" twins get static bodies
    // of their own, paired by name (MapObject::piecePrefix).
    void CreateActiveMeshes();
    // The release, FUN_101B5010 for a statdest entry: the twin goes (hidden,
    // body removed), the pieces come in (visible, dynamic, at rest), and the
    // scripts get EXPLODEMESH. `blast` is the explosion centre or null.
    // Docs/Reference/Physics.md, "Destructibles".
    void ReleaseDestructible(size_t index, const float* blast);
    // Twin body slots that a blast or a group activation reported.
    void ReleaseTwins(const std::vector<int>& twinSlots, const float* blast);
    // Level_GetActiveMeshesData(name), the Lua global the ENGINE calls per
    // active mesh; 1 means "use WORLD.Init's ActiveMeshesMassScale".
    float ActiveMeshMassScale(const std::string& objectName);
    static int L_WORLD_SetupFog(lua_State* L);
    static int L_WORLD_SetFarClipDist(lua_State* L);
    static int L_WORLD_AmbientColor(lua_State* L);
    static int L_WORLD_LoadSky(lua_State* L);
    static int L_WORLD_LoadLowQualitySky(lua_State* L);
    static int L_WORLD_SetupSkyLayer(lua_State* L);
    static int L_MESH_SetDefaultDetailMaps(lua_State* L);

    // The 2D layer. Argument order, defaults and colour packing all follow
    // the shipped Engine.dll; see Docs/Reference/Hud.md for where each came from.
    static int L_MATERIAL_Create(lua_State* L);
    static int L_MATERIAL_Release(lua_State* L);
    static int L_MATERIAL_Size(lua_State* L);
    static int L_HUD_PrintXY(lua_State* L);
    static int L_HUD_DrawQuad(lua_State* L);
    static int L_HUD_DrawQuadRGBA(lua_State* L);
    static int L_HUD_DrawQuadRotated(lua_State* L);
    static int L_HUD_DrawRect(lua_State* L);
    static int L_HUD_DrawBorder(lua_State* L);
    static int L_HUD_SetFont(lua_State* L);
    static int L_HUD_GetTextWidth(lua_State* L);
    static int L_HUD_GetTextHeight(lua_State* L);
    static int L_HUD_SetTransparency(lua_State* L);
    static int L_HUD_PrepareString(lua_State* L);
    static int L_HUD_GetTransparency(lua_State* L);
    static int L_HUD_StripColorInfo(lua_State* L);
    static int L_HUD_ColorSubstr(lua_State* L);
    static int L_R3D_ScreenSize(lua_State* L);
    static int L_R3D_GetFPS(lua_State* L);

    // The menu. Stage 1: the screen lifecycle, static text and text buttons.
    static int L_PMENU_Activate(lua_State* L);
    static int L_PMENU_Active(lua_State* L);
    static int L_PMENU_Clear(lua_State* L);
    static int L_PMENU_ClearScreen(lua_State* L);
    static int L_PMENU_SetBackground(lua_State* L);
    static int L_PMENU_SetMenuWidth(lua_State* L);
    static int L_PMENU_SetTopPosition(lua_State* L);
    static int L_PMENU_ShowMouse(lua_State* L);
    static int L_PMENU_ShowMenu(lua_State* L);
    static int L_PMENU_ReturnToGame(lua_State* L);
    static int L_WORLD_SetGamePaused(lua_State* L);
    static int L_WORLD_IsGamePaused(lua_State* L);
    static int L_PMENU_AddCheckbox(lua_State* L);
    static int L_PMENU_AddSlider(lua_State* L);
    static int L_PMENU_AddNumRange(lua_State* L);
    static int L_PMENU_AddTextButtonEx(lua_State* L);
    static int L_PMENU_ChangeTextButtonExValue(lua_State* L);
    static int L_PMENU_AddTextEdit(lua_State* L);
    static int L_PMENU_GetSliderValue(lua_State* L);
    static int L_PMENU_IsSliderFloat(lua_State* L);
    static int L_PMENU_GetNumRangeValue(lua_State* L);
    static int L_PMENU_IsItemChecked(lua_State* L);
    static int L_PMENU_SetCheckboxValue(lua_State* L);
    static int L_PMENU_GetTextEditValue(lua_State* L);
    static int L_PMENU_AddBorder(lua_State* L);
    static int L_PMENU_AddTabGroup(lua_State* L);
    static int L_PMENU_SetBorderSize(lua_State* L);
    static int L_PMENU_SetBorderHeader(lua_State* L);
    static int L_PMENU_SetBorderColCount(lua_State* L);
    static int L_PMENU_SetBorderColumn(lua_State* L);
    static int L_R3D_GetAvailableResolutions(lua_State* L);
    static int L_R3D_DrawSprite(lua_State* L);
    static int L_R3D_DrawSprite1DOF(lua_State* L);
    static int L_R3D_RGB(lua_State* L);
    static int L_R3D_RGBA(lua_State* L);
    static int L_ENTITY_GetPtrByIndex(lua_State* L);
    static int L_ENTITY_RegisterChild(lua_State* L);
    static int L_ENTITY_GetChildByName(lua_State* L);
    static int L_ENTITY_KillAllChildrenByName(lua_State* L);
    static int L_ENTITY_KillAllChildren(lua_State* L);
    static int L_ENTITY_UnregisterAllChildren(lua_State* L);
    static int L_SND_Setup3D(lua_State* L);
    static int L_SND_EntityPlay(lua_State* L);
    static int L_SND_EntityStop(lua_State* L);
    static int L_SND_EntityIsPlaying(lua_State* L);
    static int L_SND_GetSound3DPtr(lua_State* L);
    static int L_PO_EnableGravity(lua_State* L);
    static int L_PMENU_AddStaticText(lua_State* L);
    static int L_PMENU_AddTextButton(lua_State* L);
    static int L_PMENU_SetItemText(lua_State* L);
    static int L_PMENU_SetItemDesc(lua_State* L);
    static int L_PMENU_SetItemAction(lua_State* L);
    static int L_PMENU_SetItemPosition(lua_State* L);
    static int L_PMENU_SetItemColors(lua_State* L);
    static int L_PMENU_SetItemFonts(lua_State* L);
    static int L_PMENU_SetItemFontsTex(lua_State* L);
    static int L_PMENU_SetItemVisibility(lua_State* L);
    static int L_PMENU_SetStaticTextRect(lua_State* L);
    // The campaign map: EngineGame::SwitchMapSelect and the Map* family.
    static int L_PMENU_SwitchToMap(lua_State* L);
    static int L_PMENU_AddLevelToMap(lua_State* L);
    static int L_PMENU_MapReset(lua_State* L);
    static int L_PMENU_MapSetCurrLevel(lua_State* L);
    static int L_PMENU_MapNextLevel(lua_State* L);
    static int L_PMENU_MapGetCurrLevel(lua_State* L);
    static int L_PMENU_MapGetCurrChapter(lua_State* L);
    static int L_PMENU_MapGetCurrLevelName(lua_State* L);
    static int L_PMENU_MapGetCurrLevelCardCondition(lua_State* L);
    static int L_PMENU_MapGetCurrLevelCardIndex(lua_State* L);
    // The tarot board.
    static int L_PMENU_SwitchToBoard(lua_State* L);
    static int L_MBOARD_SetupSlots(lua_State* L);
    static int L_MBOARD_SetSlotPosition(lua_State* L);
    static int L_MBOARD_AddCard(lua_State* L);
    static int L_MBOARD_IsCardInSlot(lua_State* L);
    // Bink movies: none here. Returns false, which every caller tolerates.
    static int L_PMENU_PlayMovie(lua_State* L);
    // The key table and its accessors (ControlsConfig).
    static int L_PMENU_AddKeyControl(lua_State* L);
    static int L_PMENU_AddSimpleKeyConf(lua_State* L);
    static int L_PMENU_SetKeyItemIndex(lua_State* L);
    static int L_PMENU_GetPrimaryKey(lua_State* L);
    static int L_PMENU_GetAlternateKey(lua_State* L);
    static int L_PMENU_GetSimpleKey(lua_State* L);
    static int L_PMENU_AddScroller(lua_State* L);
    static int L_PMENU_AddLoadSave(lua_State* L);
    static int L_PMENU_AddSaveGameToList(lua_State* L);
    static int L_PMENU_ClearList(lua_State* L);
    static int L_PMENU_GetSelectedSGSlot(lua_State* L);
    static int L_PMENU_SetAllowSave(lua_State* L);
    static int L_PMENU_SetListMaxHeight(lua_State* L);
    static int L_PMENU_SetScrollerForBorder(lua_State* L);
    static int L_PMENU_SetBorderScroller(lua_State* L);
    static int L_INP_GetKeyNameByEngName(lua_State* L);
    static int L_INP_GetShortNameByEngName(lua_State* L);
    static int L_MOUSE_SetInverse(lua_State* L);
    static int L_MOUSE_SetSmooth(lua_State* L);
    static int L_MOUSE_SetWheelSensitivity(lua_State* L);
    // Presentation knobs that are recorded and not yet honoured.
    static int L_PMENU_NoOp(lua_State* L);
    static int L_PMENU_LaunchURL(lua_State* L);
    // What a level switch has to drop that the scripts do not release
    // themselves: the engine-made active-mesh entities, the water, the
    // per-level caches. Run by WORLD.LoadMap when a map was already up.
    void ResetLevelState();
    static int L_PMENU_SetItemAlign(lua_State* L);
    static int L_PMENU_SetItemWidth(lua_State* L);
    static int L_PMENU_EnableItemBG(lua_State* L);
    static int L_PMENU_SetItemSounds(lua_State* L);
    static int L_PMENU_DisableItem(lua_State* L);
    static int L_PMENU_EnableItem(lua_State* L);
    static int L_MOUSE_GetPos(lua_State* L);
    static int L_SOUND_ApplySoundSettings(lua_State* L);
    static int L_SOUND_GetNumOfProviders(lua_State* L);
    static int L_SOUND_Get3DSoundProviderName(lua_State* L);
    static int L_SOUND_GetCurrent3DSoundProviderName(lua_State* L);
    static int L_SOUND_Set3DSoundProvider(lua_State* L);
    static int L_SOUND_SetMasterVolume(lua_State* L);

    LuaHost* host_ = nullptr;
    // One id per blast, so Game_GetMsg's _Exploded dedupe sees two entities in
    // the same explosion as one event and two explosions as two.
    uint32_t explosionCounter_ = 0;
    std::unordered_map<int, Entity> entities_;
    int nextHandle_ = 1;
    // Save/load: every entity goes, then each saved one is rebuilt at its
    // saved handle (the scripts hold them in EntityToObject).
    void ReleaseAllEntities();
    void RebuildEntity(int handle, Entity& e);
    int levelChangeSerial_ = 0, levelChangeSeen_ = 0;
    bool loadedFromSave_ = false;
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
    // The debris ExplodeItem made, per item that blew up. CItem:DestroyItemFX
    // asks for it by the ITEM's handle straight after exploding it, and walks
    // the answer to texture the parts and set them alight.
    std::unordered_map<int, std::vector<int>> lastExploded_;
    std::unordered_map<std::string, std::vector<LimbBounds>> hitboxes_;
    // Ragdoll definitions by model, parsed once. A model with no .hke gets an
    // entry too - a miss is an answer, not something to retry every frame.
    std::unordered_map<std::string, Hke> ragdolls_;
    std::unordered_map<std::string, std::vector<Mat4>> ragdollOffsets_;
    // Per model, which ragdoll parts the constraint graph does NOT hold - the
    // weapons. Parallel to the part order, so it shares RagdollOffsets' cache.
    std::unordered_map<std::string, std::vector<char>> ragdollFree_;
    // Limb body handles, dense and permanent for the life of the process. A
    // handle is kLimbHandleBase + index, which cannot be mistaken for a script
    // body slot (small and positive) or for the world (-1).
    static const int kLimbHandleBase = 0x40000000;
    std::vector<std::pair<int, int>> limbHandles_;         // index -> entity, joint
    std::unordered_map<long long, int> limbHandleIndex_;   // entity,joint -> index
    // The movement bodies that limb boxes have taken over from. An actor whose
    // .rde gives it limbs is shot at through those, so its walking shape must
    // stop answering traces - otherwise the fat three-sphere body shadows the
    // very boxes that are meant to replace it. Rebuilt each frame in
    // TickMonsters, which already walks exactly this set.
    std::vector<int> limbShadowed_;
    // Limb handles PHYSICS.RemoveHavokBodyFromIS has taken out of the traces.
    // Per BODY, where the solver flags are per entity: the stake removes the
    // ONE limb it just hit and traces again, to see what is behind it.
    std::vector<int> suppressedLimbs_;
    // Scratch for TraceRay: excludedSlots_ followed by limbShadowed_. A member
    // so a shotgun's dozen traces in one frame do not each allocate.
    mutable std::vector<int> traceExclude_;
    // Body slots currently taken out of the intersection solver. Kept as a
    // list rather than rebuilt per trace: a shotgun fires a dozen traces in
    // one frame and the set is only ever a couple of entities deep.
    std::vector<int> excludedSlots_;
    std::vector<int> expired_;   // scratch for TickLifetimes
    AnimationCache animations_;
    SkeletonCache skeletons_;

    // The navigation graph, loaded by WPT.Load from the .wps beside the map,
    // and the routes PATH.* hands the scripts over it. A path is a queue of
    // points an actor walks in order; the scripts hold an index into `paths_`
    // and pass it back to every PATH call.
    AudioEngine* audio_ = nullptr;

    // --- the 2D layer -----------------------------------------------------
    HudRenderer* hud_ = nullptr;
    TextureCache* hudTextures_ = nullptr;
    // Sizes for MATERIAL.Create/Size when no HudRenderer is attached. Indexed
    // 1-based to match HudRenderer::Material, so handle 0 stays "no texture".
    std::vector<std::pair<int, int>> headlessMaterials_;
    MenuSystem menu_;
    Console console_;
    // WORLD.IsGamePaused reads this; Engine.dll keeps the same byte on the
    // World object at +0x10. No shipped script ever calls SetGamePaused, so
    // the engine is what writes it - which is why the menu owns it here.
    bool gamePaused_ = false;
    int soundPauseToken_ = 0;   // the set SetGamePaused(true) silenced
    bool devMode_ = false;
    // 1024x768 is what the shipped interface was authored at and what
    // HUD::SetFont measures its scale against. Until a window says otherwise
    // this is also what R3D.ScreenSize answers.
    int screenW_ = 1024, screenH_ = 768;
    std::vector<std::string> resolutions_;
    std::function<void(int, int, bool)> setVideoMode_;
    std::function<void(size_t, bool)> worldObjectVisible_;
    // A destructible: the "statdest" object drawn by the world path with its
    // own static body, and the "physdest" pieces (entity handles) held out
    // of the world until a blast or a group activation swaps them in.
    struct Destructible {
        size_t object = 0;
        int twinBody = -1;
        int group = -1;
        std::vector<int> pieces;
        bool released = false;
    };
    std::vector<Destructible> destructibles_;
    float cameraFov_ = 90.f;
    // What HUD.SetFont selected. PrintXY overrides it per call, so this is
    // only what GetTextWidth / GetTextHeight measure against.
    std::string hudFont_ = "timesbd";
    int hudFontSize_ = 16;
    // HUD.SetTransparency, stored as 0-255 for the scripts to read back. It is
    // not applied to anything the engine draws - see L_HUD_SetTransparency.
    int hudAlpha_ = 255;

    // A script's font size in pixels. The original computes
    // round(size * (H/768 + W/1024) * 0.5) - the mean of the vertical and
    // horizontal scale against the reference resolution - so the interface
    // keeps its proportions at any window size, while the x/y coordinates the
    // scripts pass stay the raw pixels they scaled themselves.
    int HudFontPixels(int size) const;
    // The font a call draws with. A named font wins; an empty name is
    // PrintXY's `SetFont(0)`, which selects slot 0 - the default - and NOT
    // whatever HUD.SetFont last set.
    void HudResolveFont(const char* name, int size, std::string& outName,
                        int& outPixels) const;

    float listenerPos_[3] = {0, 0, 0};
    float listenerFwd_[3] = {0, 0, 1};

    WaypointSet waypoints_;
    struct Route {
        std::vector<float> points;   // xyz triples, front first
        size_t next = 0;             // how far along GetNextPoint has eaten
        bool live = false;
    };
    std::vector<Route> paths_;
    std::vector<int> routeScratch_;  // node indices, reused by GetShortest
    bool playerSpotDone_ = false;
    // Scratch for the per-frame pose push; kept so that posing forty actors
    // does not allocate forty times a frame.
    std::vector<Mat4> skinScratch_;
    std::vector<const AnimTrack*> curveTracks_;   // scratch for AnimMovement
    std::vector<ScriptBodyPose> poseScratch_;
    std::vector<ScriptContact> contactScratch_;
    // The level's water surfaces, one entity each, registered when the map
    // loads. WorldMesh::SetupFlags marks an object as water purely from its
    // NAME, so that is what identifies them here too - see Water.md.
    struct WaterSurface {
        int entity = 0;
        float y = 0.f;                 // world-space surface height (they are flat)
        float lo[2] = {0, 0};          // world-space XZ bounds
        float hi[2] = {0, 0};
    };
    std::vector<WaterSurface> water_;
    // INP.Get/SetTimeMultiplier - the game-speed scale, 1 at normal speed.
    // StdOnCollision multiplies the impact speed by it before comparing, so an
    // absent one would throw rather than merely read wrong.
    float timeMultiplier_ = 1.f;
    // The velocity each body had when this frame's contact was recorded.
    // PHYSICS.GetHavokBodyVelocity answers from here while the scripts are
    // handling those collisions - see TickCollisions.
    std::unordered_map<int, std::array<float, 3>> contactVelocity_;
};

} // namespace painful
