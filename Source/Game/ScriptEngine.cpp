#include "ScriptEngine.h"

#include "../Assets/Emitter.h"
#include "../Assets/Properties.h"
#include "../Assets/Skeleton.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "../Render/BillboardRenderer.h"
#include "../Render/EntityRenderer.h"
#include "../Render/HudRenderer.h"
#include "../Render/ParticleRenderer.h"
#include "../Render/TextureCache.h"
#include "../Audio/AudioEngine.h"
#include "PlayerPawn.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace painful {

namespace {

// Bone names come from the model file, the names to match come from a
// template's aiParams, and the two do not agree on case.
bool EqualsCI(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && !b[i];
}

// A posed bone can carry scale; EngineRot9ToQuat needs a pure rotation, and
// would otherwise fold the scale into the quaternion as a bogus twist.
void Normalize3x3Rows(float m[9]) {
    for (int r = 0; r < 3; ++r) {
        float* row = &m[r * 3];
        const float len = std::sqrt(row[0]*row[0] + row[1]*row[1] + row[2]*row[2]);
        if (len > 1e-8f) { row[0] /= len; row[1] /= len; row[2] /= len; }
        else             { row[0] = row[1] = row[2] = 0.f; row[r] = 1.f; }
    }
}

bool StartsWithCI(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = s[i], b = prefix[i];
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (std::tolower(static_cast<unsigned char>(a)) !=
            std::tolower(static_cast<unsigned char>(b)))
            return false;
    }
    return true;
}

// Entity handles are plain integers pushed as Lua numbers - scripts store
// them (self._Entity), pass them back as the first native argument, and use
// them as EntityToObject keys, all of which numbers satisfy. Zero is never
// handed out, so nil/absent arguments read as "no entity".
int HandleArg(lua_State* L, int idx) {
    return lua_isnumber(L, idx) ? int(lua_tonumber(L, idx)) : 0;
}

} // namespace

ScriptEngine* ScriptEngine::From(lua_State* L) {
    return static_cast<ScriptEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

ScriptEngine::Entity* ScriptEngine::Find(int handle) {
    auto it = entities_.find(handle);
    return it == entities_.end() ? nullptr : &it->second;
}

void ScriptEngine::AttachRenderer(EntityRenderer* entities, TextureCache* textures,
                                  const std::string& dataRoot) {
    renderer_ = entities;
    textures_ = textures;
    dataRoot_ = dataRoot;
    animations_.SetRoot(dataRoot + "/Models");
    skeletons_.SetRoot(dataRoot + "/Models");
}

void ScriptEngine::AttachPhysics(PhysicsWorld* physics, const std::string& dataRoot) {
    physics_ = physics;
    dataRoot_ = dataRoot;
    animations_.SetRoot(dataRoot + "/Models");
    skeletons_.SetRoot(dataRoot + "/Models");
}

void ScriptEngine::AttachParticles(ParticleRenderer* particles, EmitterLibrary* library) {
    particles_ = particles;
    emitterLib_ = library;
}

void ScriptEngine::AttachBillboards(BillboardRenderer* billboards) {
    billboards_ = billboards;
}

void ScriptEngine::AttachPlayer(PlayerPawn* pawn) {
    pawn_ = pawn;
}

void ScriptEngine::AttachInput(Input* input) {
    input_ = input;
}

void ScriptEngine::SyncPlayerFromPawn() {
    if (!pawn_ || !playerHandle_) return;
    if (Entity* e = Find(playerHandle_)) {
        const float* head = pawn_->headPos();
        for (int i = 0; i < 3; ++i) e->pos[i] = head[i];
    }
}

void ScriptEngine::UpdateAttachments(Entity& e) {
    if (particles_ && !e.emitterSlots.empty()) {
        float rot9[9];
        EngineQuatToRot9(e.rotWXYZ, rot9);
        for (int slot : e.emitterSlots) {
            if (slot < 0) continue;
            particles_->SetScriptEmitterOwner(slot, e.pos, rot9, e.scale,
                                              e.visible && e.inWorld);
        }
    }
    if (billboards_ && e.spriteSlot >= 0)
        billboards_->SetScriptSpritePos(e.spriteSlot, e.pos);
}

void ScriptEngine::SyncFromPhysics(bool activeOnly) {
    if (!physics_) return;
    physics_->CollectScriptPoses(poseScratch_, activeOnly);
    for (const ScriptBodyPose& pose : poseScratch_) {
        auto it = bodyToEntity_.find(pose.slot);
        if (it == bodyToEntity_.end()) continue;
        Entity* e = Find(it->second);
        if (!e) continue;
        // A monster is MOVED, not simulated: TickMonsters owns where it is,
        // and its body is carried along behind. Reading the body back here
        // would put the two in a tug of war - each frame the walk would be
        // half undone by whatever the kinematic body had drifted to.
        if (e->isMonster) continue;
        for (int c = 0; c < 3; ++c) e->pos[c] = pose.pos[c];
        for (int c = 0; c < 4; ++c) e->rotWXYZ[c] = pose.quatWXYZ[c];
        SyncPose(*e);
    }
}

// Splits an engine-style "../Data/Items/<pack>" back into the pack name the
// physics loader joins with its items root.
bool ScriptEngine::SplitPackSource(const std::string& source, std::string& packName) const {
    const std::string resolved = host_->ResolvePath(source);
    const std::string prefix = dataRoot_ + "/Items/";
    if (!StartsWithCI(resolved, prefix)) return false;
    packName = resolved.substr(prefix.size());
    return true;
}

void ScriptEngine::CreateRendererInstance(Entity& e) {
    if (!renderer_ || !textures_ || e.rendererInstance >= 0) return;
    if (e.type == kModel) {
        e.rendererInstance = renderer_->CreateScriptModel(
            e.source, e.scale, *textures_, dataRoot_ + "/Models");
    } else if (e.type == kMesh && !e.worldObject) {
        // The pack path arrives engine-style: "../Data/Items/<pack>".
        // GetPack joins itemsRoot + "/" + pack, so split the resolved path
        // back apart (which also keeps its cache keyed consistently).
        const std::string resolved = host_->ResolvePath(e.source);
        const std::string itemsRoot = dataRoot_ + "/Items";
        std::string pack = resolved;
        std::string root = ".";
        if (StartsWithCI(resolved, itemsRoot + "/")) {
            pack = resolved.substr(itemsRoot.size() + 1);
            root = itemsRoot;
        }
        e.rendererInstance =
            renderer_->CreateScriptPack(pack, e.mesh, e.scale, *textures_, root);
    }
    if (e.rendererInstance >= 0) SyncPose(e);
}

void ScriptEngine::SyncPose(Entity& e) {
    if (renderer_ && e.rendererInstance >= 0) {
        renderer_->SetScriptPose(e.rendererInstance, e.pos, e.rotWXYZ);
        renderer_->SetScriptVisible(e.rendererInstance, e.visible && e.inWorld);
    }
    UpdateAttachments(e);
}

void ScriptEngine::FlushToRenderer() {
    for (auto& kv : entities_) CreateRendererInstance(kv.second);
}

// ---------------------------------------------------------------- ENTITY

// ENTITY.Create(etype, source, nameTag, scale [, translateToZero]) -> handle
// The Model path passes a .pkmdl name with the scripts' own *0.1 already
// applied; the Mesh path passes "../Data/Items/<pack>" plus the object name.
int ScriptEngine::L_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity e;
    e.type = int(luaL_checknumber(L, 1));
    e.source = luaL_optstring(L, 2, "");
    e.mesh = luaL_optstring(L, 3, "");
    e.scale = float(luaL_optnumber(L, 4, 1.0));
    if (e.type == kModel) {
        // Argument 3 is the "Name:Tag" identity for models, not a mesh name.
        e.name = e.mesh;
        e.mesh.clear();
    }
    const int handle = self->nextHandle_++;
    auto it = self->entities_.emplace(handle, e).first;
    ++self->created_;
    self->CreateRendererInstance(it->second);
    lua_pushnumber(L, handle);
    return 1;
}

void ScriptEngine::ReleaseEntity(int handle) {
    auto it = entities_.find(handle);
    if (it == entities_.end()) return;   // nil and doubles are routine
    if (renderer_ && it->second.rendererInstance >= 0)
        renderer_->ReleaseScript(it->second.rendererInstance);
    if (physics_ && it->second.physicsBody >= 0) {
        physics_->RemoveScriptBody(it->second.physicsBody);
        bodyToEntity_.erase(it->second.physicsBody);
        // Anything released mid-trace-bracket would otherwise leave a dead
        // slot in the exclusion list for good. A projectile does exactly
        // that: it removes itself from the solver, traces, and dies.
        excludedSlots_.erase(
            std::remove(excludedSlots_.begin(), excludedSlots_.end(), it->second.physicsBody),
            excludedSlots_.end());
    }
    if (billboards_ && it->second.spriteSlot >= 0)
        billboards_->RemoveScriptSprite(it->second.spriteSlot);
    if (particles_)
        for (int slot : it->second.emitterSlots)
            if (slot >= 0) particles_->RemoveScriptEmitter(slot);
    entities_.erase(it);
    ++released_;
}

int ScriptEngine::L_Release(lua_State* L) {
    From(L)->ReleaseEntity(HandleArg(L, 1));
    return 0;
}

// ENTITY.PO_Hit(e, x,y,z, ix,iy,iz) and WORLD.HitPhysicObject(body, ...) -
// the shove a hit delivers, as an impulse at the point it landed. The two
// differ only in what they are handed: PO_Hit takes an entity, and
// HitPhysicObject takes the body handle a trace reported, which is the same
// script body slot. A weapon calls both - the first for what it damaged, the
// second for anything physical it touched.
static void ApplyHitImpulse(lua_State* L, PhysicsWorld* physics, int slot) {
    if (!physics || slot < 0) return;
    const float at[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                         float(luaL_optnumber(L, 4, 0))};
    const float impulse[3] = {float(luaL_optnumber(L, 5, 0)),
                              float(luaL_optnumber(L, 6, 0)),
                              float(luaL_optnumber(L, 7, 0))};
    physics->AddScriptBodyImpulse(slot, at, impulse);
}

int ScriptEngine::L_PO_Hit(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (e) ApplyHitImpulse(L, self->physics_, e->physicsBody);
    return 0;
}

int ScriptEngine::L_WORLD_HitPhysicObject(lua_State* L) {
    ScriptEngine* self = From(L);
    // -1 is what a trace reports for the world itself, which cannot be moved.
    ApplyHitImpulse(L, self->physics_, int(luaL_optnumber(L, 1, -1)));
    return 0;
}

// ENTITY.SetTimeToDie(e, seconds) - the engine reaps the entity itself once
// the time is up. Everything transient uses it: shell casings, the stone
// chips a shotgun knocks off a wall, a spent projectile. Left unimplemented
// those never go away, and since they are ITEMS with real bodies they pile
// up as collision the player walks into - which reads as the impact effect
// itself being solid.
int ScriptEngine::L_SetTimeToDie(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->timeToDie = float(luaL_optnumber(L, 2, 0));
    return 0;
}

// How wide a monster is, in world units.
//
// NOT the body radius CreateScriptBody computed: that is the largest of the
// three half-extents, which is right for a barrel and badly wrong for a
// character - evilmonkv2's widest axis is its outstretched ARMS, 14.4 model
// units against a body only 2.9 deep. A monster given that radius is a sphere
// wider than it is tall, and it cannot get near a wall.
//
// The horizontal half-extents are the honest measure of a character's width,
// and the SMALLER of the two is the one that is not arms. The engine's own
// rule for BodyTypes.Fatter lives inside Entity::CreatePhysicsObject and has
// not been recovered, so this is a shape argument rather than the original's
// constant - flagged in Docs/Gameplay_Roadmap.md.
// Also reports how far the sphere's CENTRE sits above the entity's position,
// which is not zero and is not the radius: a .pkmdl's origin is the middle of
// the model, not the ground under it. evilmonkv2's bounds run y[-12.80..10.11],
// so its feet are 12.8 model units BELOW the position the scripts set. Place
// the sphere about the origin and the monster wades through the floor; lift it
// by a radius, as a foot-origin rig would want, and it climbs out of the world
// a radius at a time.
float ScriptEngine::MonsterRadius(Entity& e, float* centreAboveOrigin) {
    if (centreAboveOrigin) *centreAboveOrigin = 0.f;
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel) return 0.f;
    const float halfX = (skel->hi[0] - skel->lo[0]) * 0.5f;
    const float halfZ = (skel->hi[2] - skel->lo[2]) * 0.5f;
    const float half = std::min(halfX, halfZ);
    if (half <= 0.f) return 0.f;

    const float radius = half * e.scale;
    // Bottom of the sphere on the soles: centre = feet + radius, and the feet
    // are lo[1] (negative) below the origin.
    if (centreAboveOrigin) *centreAboveOrigin = skel->lo[1] * e.scale + radius;
    return radius;
}

void ScriptEngine::TickMonsters(float dt) {
    if (dt <= 0.f || !physics_) return;

    // PAINFUL_PLAYER_AT lands HERE rather than at spawn: the scripts place the
    // player themselves during level load, so an override applied any earlier
    // is simply overwritten before the first frame.
    if (!playerSpotDone_ && pawn_ && playerHandle_) {
        playerSpotDone_ = true;
        float at[3];
        const char* spot = getenv("PAINFUL_PLAYER_AT");
        if (spot && std::sscanf(spot, "%f,%f,%f", &at[0], &at[1], &at[2]) == 3) {
            pawn_->Spawn(at);
            LogInfo("player moved to %.1f %.1f %.1f (PAINFUL_PLAYER_AT)", at[0], at[1], at[2]);
        }
    }


    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (!e.isMonster || e.physicsBody < 0) continue;

        // Comfortably above SlideSphere's own 0.02 skin.
        static constexpr float kSweepSkin = 0.05f;
        float lift = 0.f;
        const float radius = MonsterRadius(e, &lift);
        if (radius <= 0.f) continue;

        // Gravity is ours. The AI passes a direction to walk in, not a fall -
        // CActor's vector comes out of VectorRotate on its facing angle - so
        // something has to hold the actor down, and in the original that is
        // the physics step this stands in for.
        e.fallSpeed = e.onFloor ? 0.f : e.fallSpeed + physics_->settings().gravity * dt;

        // Carry the step forward until it is worth sweeping.
        //
        // SlideSphere keeps a 0.02 skin off every surface and advances by
        // `length - skin`, so a step SHORTER than the skin advances by nothing
        // at all. The player never meets that - it moves 0.13 units a frame -
        // but an actor damps its speed down as it closes on its target, and at
        // 0.46 units a second a 60 Hz step is 0.008 units. Swept one frame at a
        // time it is frozen forever, however long it walks.
        //
        // So the step accumulates and is spent in one go once it clears the
        // skin. Nothing is lost: the distance is the same, it just arrives
        // every third frame instead of every frame.
        for (int c = 0; c < 3; ++c) {
            e.moveResidual[c] += e.moveWish[c] * dt;
            if (c == 1) e.moveResidual[c] -= e.fallSpeed * dt;
        }
        const float carried = std::sqrt(e.moveResidual[0] * e.moveResidual[0] +
                                        e.moveResidual[1] * e.moveResidual[1] +
                                        e.moveResidual[2] * e.moveResidual[2]);
        if (carried < kSweepSkin) continue;

        const float step[3] = {e.moveResidual[0], e.moveResidual[1], e.moveResidual[2]};
        for (int c = 0; c < 3; ++c) e.moveResidual[c] = 0.f;

        // The SAME swept sphere the player moves with, so a monster is stopped
        // by exactly the geometry the player is stopped by. solidProps=true
        // because a monster should no more walk through a coffin than the
        // player should.
        //
        // The sphere is carried to where the model's soles are and put back on
        // the origin afterwards; MonsterRadius works that offset out from the
        // model's own bounds.
        float pos[3] = {e.pos[0], e.pos[1] + lift, e.pos[2]};
        physics_->SlideSphere(pos, step, radius, true, e.physicsBody);

        // Floor state, for PO_IsOnFloor. A short probe straight down: far
        // enough to survive the gap a slide leaves, short enough not to claim
        // ground the actor is falling towards.
        const float below[3] = {0.f, -(radius * 0.25f), 0.f};
        float probe[3] = {pos[0], pos[1], pos[2]};
        physics_->SlideSphere(probe, below, radius, true, e.physicsBody);
        e.onFloor = (probe[1] - pos[1]) > below[1] * 0.5f;
        if (e.onFloor) e.fallSpeed = 0.f;
        pos[1] -= lift;
        // The normal is not measured yet; upright is the answer that keeps
        // CAiBrain's arithmetic honest until a real contact normal is read.
        e.floorNormal[0] = 0.f;
        e.floorNormal[1] = 1.f;
        e.floorNormal[2] = 0.f;

        for (int c = 0; c < 3; ++c) e.pos[c] = pos[c];
        // The body is kinematic, so it does not move itself - it is carried,
        // and it exists so that everything sweeping against the world finds a
        // monster in the way.
        //
        // It goes where the COLLISION SPHERE is, not where the entity is. The
        // entity's position is the model's centre and the sphere sits about a
        // unit lower, on the soles; a body left at the entity position floats
        // over the player's head, and the player walks straight through the
        // monster because the two never overlap.
        const float bodyPos[3] = {e.pos[0], e.pos[1] + lift, e.pos[2]};
        physics_->SetScriptBodyPose(e.physicsBody, bodyPos, e.rotWXYZ);
        if (renderer_ && e.rendererInstance >= 0)
            renderer_->SetScriptPose(e.rendererInstance, e.pos, e.rotWXYZ);
    }
}

void ScriptEngine::TickLifetimes(float dt) {
    if (dt <= 0.f) return;
    expired_.clear();
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (e.timeToDie >= 0.f) {
            e.timeToDie -= dt;
            if (e.timeToDie <= 0.f) {
                expired_.push_back(kv.first);
                continue;
            }
        }
        // A spent one-shot effect. AddPFX creates an entity per impact and
        // never takes it back, so the engine has to: once every emitter has
        // burnt its budget and its last particle has gone, the effect is
        // over. A level-placed effect never reaches this, because its
        // emitters are forced to keep evolving.
        if (e.type == kParticleFX && particles_ && !e.emitterSlots.empty()) {
            bool done = true;
            for (int slot : e.emitterSlots)
                if (slot >= 0 && !particles_->ScriptEmitterFinished(slot)) done = false;
            if (done) expired_.push_back(kv.first);
        }
    }
    for (int handle : expired_) ReleaseEntity(handle);
}

int ScriptEngine::L_SetPosition(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    if (Entity* e = self->Find(handle)) {
        const float p[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                            float(luaL_optnumber(L, 4, 0))};
        // A script can compute a NaN - divide a velocity by its own length
        // when that length is zero and every coordinate downstream is one -
        // and handing that to the simulation takes the process down. The
        // engine survives a script's bad arithmetic; so must this.
        for (int c = 0; c < 3; ++c) {
            if (!(p[c] == p[c]) || p[c] > 1e18f || p[c] < -1e18f) {
                LogWarn("ENTITY.SetPosition(%d): ignoring a non-finite position", handle);
                return 0;
            }
        }
        for (int c = 0; c < 3; ++c) e->pos[c] = p[c];
        self->SyncPose(*e);
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyPose(e->physicsBody, e->pos, e->rotWXYZ);
        if (self->pawn_ && handle == self->playerHandle_)
            self->pawn_->SetHeadPos(e->pos);   // teleports (spawn, checkpoints)
    }
    return 0;
}

int ScriptEngine::L_GetPosition(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->pos[0] : 0);
    lua_pushnumber(L, e ? e->pos[1] : 0);
    lua_pushnumber(L, e ? e->pos[2] : 0);
    return 3;
}

int ScriptEngine::L_SetRotationQ(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        for (int i = 0; i < 4; ++i)
            e->rotWXYZ[i] = float(luaL_optnumber(L, 2 + i, i == 0 ? 1 : 0));
        self->SyncPose(*e);
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyPose(e->physicsBody, e->pos, e->rotWXYZ);
    }
    return 0;
}

int ScriptEngine::L_GetRotationQ(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->rotWXYZ[0] : 1);
    lua_pushnumber(L, e ? e->rotWXYZ[1] : 0);
    lua_pushnumber(L, e ? e->rotWXYZ[2] : 0);
    lua_pushnumber(L, e ? e->rotWXYZ[3] : 0);
    return 4;
}

// SetOrientation/GetOrientation: yaw about Y, radians.
//
// The sign is NEGATED, and the shipped scripts say so twice. `BindPoint`
// (Utils.lua) rotates an offset by `-GetOrientation(e)`, and
// `CActor:MoveWithAnimation` rotates the animation's own motion by
// `cos(-angle)/sin(-angle)`. Both come out as the same transform:
//
//     world = ( cos A * mx + sin A * mz,  my,  -sin A * mx + cos A * mz )
//
// which sends the model's forward (+Z, the axis the walk animations travel
// along) to (sin A, 0, cos A). Built the other way round the actor faces the
// mirror image of where it is going - right at 0 and 180 degrees, backwards
// at 90, which is what "close, but facing the wrong way" looks like.
int ScriptEngine::L_SetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        const float a = float(luaL_optnumber(L, 2, 0)) * 0.5f;
        e->rotWXYZ[0] = std::cos(a);
        e->rotWXYZ[1] = 0;
        e->rotWXYZ[2] = -std::sin(a);
        e->rotWXYZ[3] = 0;
        self->SyncPose(*e);
    }
    return 0;
}

int ScriptEngine::L_GetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    // The yaw back out of the stored quaternion, negated to match the above -
    // an actor reads its own angle back every tick through Synchronize, so a
    // round trip that does not land on the same number makes it drift.
    const float yaw =
        e ? -2.f * std::atan2(e->rotWXYZ[2], e->rotWXYZ[0]) : 0.f;
    lua_pushnumber(L, yaw);
    return 1;
}

// ENTITY.SetScale(e, s): particle effects apply their entity scale this way.
// A model's renderer instance keeps its creation scale for now - the shipped
// load path never rescales one after Create.
int ScriptEngine::L_SetScale(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        e->scale = float(luaL_optnumber(L, 2, 1.0));
        self->UpdateAttachments(*e);
    }
    return 0;
}

// PARTICLE.AddEmitter(e, file) -> the per-entity emitter index the scripts
// hand back to SetupEmitter. The effect resolution happened script-side
// (LoadParticleFX over ParticleFXArray); only the .ini name arrives.
int ScriptEngine::L_PARTICLE_AddEmitter(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    int slot = -1;
    if (self->particles_ && self->emitterLib_ && self->textures_)
        slot = self->particles_->AddScriptEmitter(luaL_optstring(L, 2, ""),
                                                  *self->emitterLib_, *self->textures_,
                                                  self->world_.levelName);
    e->emitterSlots.push_back(slot);
    lua_pushnumber(L, double(e->emitterSlots.size() - 1));
    return 1;
}

// PARTICLE.SetupEmitter(e, i, scale, px, py, pz, rx, ry, rz) - the .pfx
// entry's own transform; rotation in degrees, exactly what the entry stores.
int ScriptEngine::L_PARTICLE_SetupEmitter(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->particles_) return 0;
    const size_t idx = size_t(luaL_optnumber(L, 2, -1));
    if (idx >= e->emitterSlots.size() || e->emitterSlots[idx] < 0) return 0;

    const float offset[3] = {float(luaL_optnumber(L, 4, 0)), float(luaL_optnumber(L, 5, 0)),
                             float(luaL_optnumber(L, 6, 0))};
    const float rotDeg[3] = {float(luaL_optnumber(L, 7, 0)), float(luaL_optnumber(L, 8, 0)),
                             float(luaL_optnumber(L, 9, 0))};
    self->particles_->SetupScriptEmitter(e->emitterSlots[idx],
                                         float(luaL_optnumber(L, 3, 1.0)), offset, rotDeg);
    self->UpdateAttachments(*e);
    return 0;
}

// PARTICLE.SetEvolve / SetFixedTransform: evolve is always on in this port
// (the flag exists to freeze effects while the editor scrubs), and fixed
// transforms only matter once effects are bound to moving entities.
int ScriptEngine::L_NoOpNative(lua_State*) { return 0; }

// BILLBOARD.SetupCorona(e, alpha, fadeIn, fadeOut, minSize, minDistance,
// size, maxDistance, offDistance, traceMargin, tex, packedColor, blendMode,
// spriteOnly) - CBillboard:Apply's one native, field for field.
int ScriptEngine::L_BILLBOARD_SetupCorona(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->billboards_ || !self->textures_) return 0;

    float args[9];
    for (int i = 0; i < 9; ++i) args[i] = float(luaL_optnumber(L, 2 + i, 0));
    e->spriteSlot = self->billboards_->SetupScriptCorona(
        e->spriteSlot, args, luaL_optstring(L, 11, ""),
        uint32_t(int64_t(luaL_optnumber(L, 12, 0))), int(luaL_optnumber(L, 13, 1)),
        lua_toboolean(L, 14) != 0, *self->textures_, self->world_.levelName);
    self->billboards_->SetScriptSpritePos(e->spriteSlot, e->pos);
    return 0;
}

int ScriptEngine::L_EnableDraw(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        e->visible = lua_toboolean(L, 2) != 0;
        self->SyncPose(*e);
    }
    return 0;
}

// No physics yet, so entities are at rest.
// ENTITY.GetVelocity(e) -> vx, vy, vz, speed. The fourth value matters: the
// projectiles divide by it to get their heading, so answering a flat zero
// hands them a NaN that then travels through every position they compute.
int ScriptEngine::L_GetVelocity(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    float v[3] = {0, 0, 0};
    // The player has no script body - it is the pawn - so its velocity comes
    // from there. Reading the entity store instead returns whatever last wrote
    // to it, which is nothing until something knocks the player back and then
    // that knockback forever: CPlayer decides it is walking from this, so the
    // head bob stayed dead until the first hit and ran permanently after.
    if (self->pawn_ && handle == self->playerHandle_ && self->playerHandle_) {
        self->pawn_->Velocity(v);
    } else if (const Entity* e = self->Find(handle)) {
        if (!(self->physics_ && e->physicsBody >= 0 &&
              self->physics_->GetScriptBodyVelocity(e->physicsBody, v)))
            for (int c = 0; c < 3; ++c) v[c] = e->velocity[c];
    }
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, v[c]);
    lua_pushnumber(L, std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    return 4;
}

// ENTITY.SetVelocity(e, x, y, z) - how every projectile is launched, and how
// the rocket jump lifts the player.
int ScriptEngine::L_SetVelocity(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    for (int c = 0; c < 3; ++c)
        e->velocity[c] = float(luaL_optnumber(L, c + 2, 0));
    if (self->physics_ && e->physicsBody >= 0)
        self->physics_->SetScriptBodyVelocity(e->physicsBody, e->velocity);
    return 0;
}

// ENTITY.PO_Create(e, bodytype, scale, collisionGroup): CObject:PO_Create
// calls this right after SetPosition/SetRotationQ, then sets mass, friction
// and the rest through the PO_Set* family - so the body starts bare and the
// scripts dress it, exactly as the original divides the work. A scale of -1
// (the scripts' "not given") means the entity's own scale, which already
// carries the model *0.1 rule.
int ScriptEngine::L_PO_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->physics_ || e->physicsBody >= 0) return 0;

    const int bodyType = int(luaL_optnumber(L, 2, 0));
    float scale = float(luaL_optnumber(L, 3, -1.0));
    if (scale <= 0.f) scale = e->scale;

    std::string model, pack;
    if (e->type == kModel) {
        model = e->source;
    } else if (e->type == kMesh && !e->worldObject) {
        if (!self->SplitPackSource(e->source, pack)) return 0;
    } else {
        return 0;
    }

    const int slot = self->physics_->CreateScriptBody(
        bodyType, model, pack, e->mesh, scale, e->pos, e->rotWXYZ, self->dataRoot_);
    if (slot >= 0) {
        e->physicsBody = slot;
        self->bodyToEntity_[slot] = HandleArg(L, 1);
    }
    return 0;
}

// ENTITY.PO_Move(e, x, y, z) - where this actor WANTS to go, as a velocity.
//
// A pure setter, exactly as in the original: 0x10130D50 writes the three
// floats to PhysicsObject+0x34 and returns. Nothing moves here; the physics
// step spends it (TickMonsters). CActor calls this with `mv * (1/delta)`,
// which is why the units are per second.
int ScriptEngine::L_PO_Move(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    for (int c = 0; c < 3; ++c) e->moveWish[c] = float(luaL_optnumber(L, c + 2, 0));
    return 0;
}

// ENTITY.PO_SetMonsterType(e) - this body is walked, not simulated.
//
// The engine sets one flag bit and changes nothing else (0x101313C0). The flag
// arrives AFTER PO_Create, so the body is born an ordinary dynamic prop and is
// converted here - which is also the only moment we know it is a monster.
int ScriptEngine::L_PO_SetMonsterType(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->isMonster = true;
    if (self->physics_ && e->physicsBody >= 0)
        self->physics_->SetScriptBodyKinematic(e->physicsBody, self->MonsterRadius(*e));
    return 0;
}

// ------------------------------------------------------------------ sound
//
// Three families, and the split is by how the scripts hold them rather than by
// what they sound like:
//
//   SOUND.Play2D / Play3D   fire and forget; the handle comes back but most
//                           callers drop it
//   SOUND2D.*               a handle kept and driven - a bullet-time loop
//   SOUND3D.*               the same, at a world position - a flamethrower,
//                           an elevator
//
// Volume arrives as 0..100 (CObject:GetSndInfo defaults to 100), and the
// hearing distances default to 15 and 40 there, so a script that names only a
// sample still gets sensible falloff.

// The sample name the scripts build is a path under Sounds without the
// extension, and CObject:GetSndInfo joins it as `path.."/"..name`, which
// leaves a leading slash when the definition has no path. Trim it rather than
// failing to find the file.
static std::string SoundName(lua_State* L, int index) {
    const char* raw = lua_isstring(L, index) ? lua_tostring(L, index) : nullptr;
    if (!raw) return std::string();
    std::string name = raw;
    while (!name.empty() && (name.front() == '/' || name.front() == '\\'))
        name.erase(name.begin());
    return name;
}

static float SoundVolume(lua_State* L, int index) {
    // 0..100 from the scripts; anything absent means full.
    const double v = luaL_optnumber(L, index, 100.0);
    return float(v) * 0.01f;
}

int ScriptEngine::L_SOUND_Play2D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int v = self->audio_->Play2D(SoundName(L, 1), SoundVolume(L, 2),
                                       lua_toboolean(L, 3) != 0,
                                       lua_toboolean(L, 4) != 0);
    lua_pushnumber(L, v);
    return 1;
}

int ScriptEngine::L_SOUND_Play3D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const float pos[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                          float(luaL_optnumber(L, 4, 0))};
    const int v = self->audio_->Play3D(SoundName(L, 1), pos,
                                       float(luaL_optnumber(L, 5, 15.0)),
                                       float(luaL_optnumber(L, 6, 40.0)),
                                       lua_toboolean(L, 7) != 0);
    lua_pushnumber(L, v);
    return 1;
}

// SOUND2D.Create(name, loop) / SOUND3D.Create(name) -> a handle the script
// keeps. Created stopped: the scripts call Play when they want it.
int ScriptEngine::L_SND_Create2D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int v = self->audio_->Create(SoundName(L, 1), false);
    if (v && lua_toboolean(L, 2)) self->audio_->SetLoopCount(v, -1);
    lua_pushnumber(L, v);
    return 1;
}

int ScriptEngine::L_SND_Create3D(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const int v = self->audio_->Create(SoundName(L, 1), true);
    if (v && lua_toboolean(L, 2)) self->audio_->SetLoopCount(v, -1);
    lua_pushnumber(L, v);
    return 1;
}

int ScriptEngine::L_SND_Play(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Start(int(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_SND_Stop(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Stop(int(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_SND_Pause(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Pause(int(luaL_optnumber(L, 1, 0)), true);
    return 0;
}

int ScriptEngine::L_SND_IsPlaying(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushboolean(L, self->audio_ &&
                           self->audio_->IsPlaying(int(luaL_optnumber(L, 1, 0))));
    return 1;
}

int ScriptEngine::L_SND_SetVolume(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetVolume(int(luaL_optnumber(L, 1, 0)), SoundVolume(L, 2));
    return 0;
}

int ScriptEngine::L_SND_SetLoopCount(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetLoopCount(int(luaL_optnumber(L, 1, 0)),
                                   int(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_SND_SetPosition(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->audio_) return 0;
    const float pos[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                          float(luaL_optnumber(L, 4, 0))};
    self->audio_->SetPosition(int(luaL_optnumber(L, 1, 0)), pos);
    return 0;
}

int ScriptEngine::L_SND_SetHearingDistance(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetHearingDistance(int(luaL_optnumber(L, 1, 0)),
                                         float(luaL_optnumber(L, 2, 15.0)),
                                         float(luaL_optnumber(L, 3, 40.0)));
    return 0;
}

int ScriptEngine::L_SND_SetSoundSpeed(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_)
        self->audio_->SetSpeed(int(luaL_optnumber(L, 1, 0)),
                               float(luaL_optnumber(L, 2, 1.0)));
    return 0;
}

// Delete stops it; Forget lets it finish and stops caring. Both hand the slot
// back, which is what keeps a level's worth of one-shots from filling the pool.
int ScriptEngine::L_SND_Delete(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Release(int(luaL_optnumber(L, 1, 0)), false);
    return 0;
}

int ScriptEngine::L_SND_Forget(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->audio_) self->audio_->Release(int(luaL_optnumber(L, 1, 0)), true);
    return 0;
}

// SOUND.SetPlayerPos / SetPlayerOrientation - the listener, pushed every frame
// by CPlayer. Without the orientation everything would still attenuate with
// distance but nothing would come from a side.
int ScriptEngine::L_SOUND_SetPlayerPos(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int c = 0; c < 3; ++c)
        self->listenerPos_[c] = float(luaL_optnumber(L, c + 1, 0));
    self->PushListener();
    return 0;
}

int ScriptEngine::L_SOUND_SetPlayerOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int c = 0; c < 3; ++c)
        self->listenerFwd_[c] = float(luaL_optnumber(L, c + 1, 0));
    self->PushListener();
    return 0;
}

void ScriptEngine::PushListener() {
    if (!audio_) return;
    // Right = forward x up. The scripts only hand over a forward vector, and
    // panning needs a side.
    const float* f = listenerFwd_;
    float right[3] = {f[1] * 0.f - f[2] * 1.f, f[2] * 0.f - f[0] * 0.f,
                      f[0] * 1.f - f[1] * 0.f};
    const float l = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (l > 1e-5f) {
        for (int c = 0; c < 3; ++c) right[c] /= l;
    } else {
        right[0] = 1.f; right[1] = 0.f; right[2] = 0.f;
    }
    audio_->SetListener(listenerPos_, listenerFwd_, right);
}

// WPT.Load(dir, mergeFlag) - the navigation graph.
//
// The scripts pass only a DIRECTORY ("../Data/Maps/"); the engine appends the
// map's own name, which is why WORLD.LoadMap has to have run first. Engine.dll
// (0x10128A90) clears both pathfinders, then LoadContents the .wps and
// LoadFloors a companion file that does not ship - so the floors section
// inside the .wps is all there is, and routing does not need it.
int ScriptEngine::L_WPT_Load(lua_State* L) {
    ScriptEngine* self = From(L);
    self->waypoints_ = WaypointSet{};
    self->paths_.clear();

    // "../Data/Maps/1x01_Chaos.mpk" -> the .wps beside it.
    std::string path = self->world_.mapPath;
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return 0;                         // a level with no map has no graph
    path = path.substr(0, dot) + ".wps";

    const std::string resolved = self->host_ ? self->host_->ResolvePath(path) : path;
    if (!WaypointSet::Load(resolved, self->waypoints_)) {
        // Only 29 of the 85 shipped maps carry a .wps at all, so a missing one
        // is an ordinary answer rather than a fault - those levels' actors
        // walk straight at their target, which is what they did before any of
        // this existed. A malformed one IS worth shouting about.
        if (FileSystem::Get().Exists(resolved))
            LogWarn("waypoints: %s", self->waypoints_.error.c_str());
        else
            LogInfo("waypoints: none for this map, actors will walk straight");
        return 0;
    }
    LogInfo("waypoints: %zu points, %zu links from %s",
            self->waypoints_.nodes.size(), self->waypoints_.links.size(),
            path.c_str());
    return 0;
}

// PATH.Create() -> a handle the scripts keep in CActor._Path.
//
// It has to be non-nil: CActor tests `if not self._Path` before creating
// another, and returning nothing made every actor build a fresh path every
// single tick. Slots are reused, and the handle is index+1 so that 0 is never
// a valid one.
int ScriptEngine::L_PATH_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    for (size_t i = 0; i < self->paths_.size(); ++i) {
        if (self->paths_[i].live) continue;
        self->paths_[i] = Route{};
        self->paths_[i].live = true;
        lua_pushnumber(L, double(i + 1));
        return 1;
    }
    self->paths_.push_back(Route{});
    self->paths_.back().live = true;
    lua_pushnumber(L, double(self->paths_.size()));
    return 1;
}

int ScriptEngine::L_PATH_Release(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    if (h > 0 && size_t(h) <= self->paths_.size()) self->paths_[size_t(h) - 1] = Route{};
    return 0;
}

// PATH.GetShortest(path, x,y,z, destX,destY,destZ, minDist, maxDist) - route
// through the waypoint graph.
//
// minDist/maxDist come from the actor's template as WPminDist / WPmaxDist and
// bound how far it is willing to reach for a waypoint. An actor standing
// somewhere the level designer never marked gets NO path, which is not a
// failure: CActor reads an empty path as "finished" and walks straight at the
// destination instead.
//
// The first waypoint is dropped when the actor is already close to it, so it
// does not walk backwards to a point it has effectively reached.
int ScriptEngine::L_PATH_GetShortest(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    if (h <= 0 || size_t(h) > self->paths_.size()) return 0;
    Route& route = self->paths_[size_t(h) - 1];
    route.points.clear();
    route.next = 0;
    if (self->waypoints_.nodes.empty()) return 0;

    const float from[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                           float(luaL_optnumber(L, 4, 0))};
    const float to[3] = {float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
                         float(luaL_optnumber(L, 7, 0))};
    const float maxDist = float(luaL_optnumber(L, 9, 0));

    const int a = self->waypoints_.Closest(from, maxDist);
    const int b = self->waypoints_.Closest(to, maxDist);
    if (a < 0 || b < 0) return 0;
    if (!self->waypoints_.FindPath(a, b, self->routeScratch_)) return 0;

    const float minDist = float(luaL_optnumber(L, 8, 0));
    for (size_t i = 0; i < self->routeScratch_.size(); ++i) {
        const WaypointSet::Node& n =
            self->waypoints_.nodes[size_t(self->routeScratch_[i])];
        if (i == 0 && minDist > 0.f) {
            float d = 0.f;
            for (int c = 0; c < 3; ++c) {
                const float e = n.pos[c] - from[c];
                d += e * e;
            }
            if (d < minDist * minDist) continue;
        }
        for (int c = 0; c < 3; ++c) route.points.push_back(n.pos[c]);
    }
    return 0;
}

// PATH.IsFinished(path) -> 1 when no waypoint is left.
//
// Engine.dll (0x1013AA20) starts at "finished" and only clears it when PeekPos
// finds a point, so a path that does not exist is finished - and in CActor
// that is the branch which walks straight at the destination. Everything here
// therefore degrades to the old straight-line behaviour rather than stopping.
int ScriptEngine::L_PATH_IsFinished(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    bool finished = true;
    if (h > 0 && size_t(h) <= self->paths_.size()) {
        const Route& route = self->paths_[size_t(h) - 1];
        finished = route.next * 3 >= route.points.size();
    }
    lua_pushnumber(L, finished ? 1 : 0);
    return 1;
}

// PATH.GetNextPoint(path) -> x,y,z, and CONSUMES it: CActor calls this and
// then asks IsFinished again to learn whether that was the last one.
int ScriptEngine::L_PATH_GetNextPoint(lua_State* L) {
    ScriptEngine* self = From(L);
    const int h = int(luaL_optnumber(L, 1, 0));
    float p[3] = {0, 0, 0};
    if (h > 0 && size_t(h) <= self->paths_.size()) {
        Route& route = self->paths_[size_t(h) - 1];
        if (route.next * 3 + 2 < route.points.size()) {
            for (int c = 0; c < 3; ++c) p[c] = route.points[route.next * 3 + size_t(c)];
            ++route.next;
        }
    }
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, p[c]);
    return 3;
}

// ENTITY.PO_SetSightParams(e, viewDistance, viewDistance360, viewAngle,
// viewAnglePitch) - 0x10131210, writing the four floats at PhysicsObject
// +0x24..+0x30. The defaults are the engine's own: 20, 2, 180, 180.
//
// The names come from the templates, and they say what the model is:
// `viewDistance360` is how far the actor sees in EVERY direction, and
// `viewDistance` how far it sees inside its cone. Shipped monsters carry
// things like `viewAngle = 170, viewDistance360 = 6`: aware of anything within
// six units, and beyond that only what is in front.
//
// The angles arrive in DEGREES as a full spread (360 means all round) and are
// stored as a half-angle in radians, which is what makes the engine's own
// default of 180 come out as pi/2 - the value PO_Create seeds.
int ScriptEngine::L_PO_SetSightParams(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->sightRange = float(luaL_optnumber(L, 2, 20.0));
    e->sightRange360 = float(luaL_optnumber(L, 3, 2.0));
    e->sightHalfYaw = float(luaL_optnumber(L, 4, 180.0)) * float(kPi / 360.0);
    e->sightHalfPitch = float(luaL_optnumber(L, 5, 180.0)) * float(kPi / 360.0);
    return 0;
}

// ENTITY.SeesEntity(a, b) -> can a see b.
//
// Engine.dll 0x101335E0 hands this to PhysicsWorld::CalculatePawnToEntityVisibility
// when the looker has a physics object, and otherwise falls back to a plain
// line trace between the two entity POSITIONS (+0x620) - which is the shape
// reproduced here: the range and cone from PO_SetSightParams, then an
// unobstructed line.
//
// Note what the engine brackets the trace with: it turns the looker's own
// ragdoll off for the duration and back on afterwards, because a monster's own
// body sits on the line and would blind it. The same applies to us - both
// bodies are excluded below.
int ScriptEngine::L_SeesEntity(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* a = self->Find(HandleArg(L, 1));
    Entity* b = self->Find(HandleArg(L, 2));
    lua_pushboolean(L, a && b && self->Sees(*a, *b));
    return 1;
}

bool ScriptEngine::Sees(Entity& a, Entity& b) const {
    if (!physics_) return false;

    float to[3];
    for (int c = 0; c < 3; ++c) to[c] = b.pos[c] - a.pos[c];
    const float dist = std::sqrt(to[0]*to[0] + to[1]*to[1] + to[2]*to[2]);
    if (dist > a.sightRange) return false;
    if (dist < 1e-4f) return true;

    // Inside the all-round radius the cone does not apply; outside it, the
    // target has to be in front. A viewAngle of 360 makes the half-angle pi,
    // which no angle can exceed - so a monster declared to see all round
    // never fails this, without needing a special case.
    if (dist > a.sightRange360 && a.sightHalfYaw < float(kPi)) {
        const float fwd[3] = {0, 0, 1};      // model forward, the axis the
        float facing[3];                     // walk animations travel along
        EngineQuatRotate(a.rotWXYZ, fwd, facing);
        const float fl = std::sqrt(facing[0]*facing[0] + facing[2]*facing[2]);
        const float tl = std::sqrt(to[0]*to[0] + to[2]*to[2]);
        if (fl > 1e-6f && tl > 1e-6f) {
            const float cosYaw = (facing[0]*to[0] + facing[2]*to[2]) / (fl * tl);
            if (cosYaw < std::cos(a.sightHalfYaw)) return false;
        }
    }

    // Line of sight. Both bodies are excluded: the looker's own body is on the
    // line by construction, and the target's would stop the trace one step
    // Against the WORLD only, not against other bodies.
    //
    // Engine.dll's CalculatePawnToEntityVisibility (0x10198D30) takes both
    // pawns' head positions, checks the range at PhysicsObject+0x24 and the
    // pitch cone at +0x30, and then resolves the rest through
    // World::FindZone - the zone graph. Visibility there is a question about
    // level geometry, not about what happens to be standing in the way.
    //
    // Measured, and the difference is the whole behaviour of a crowd: 16 monks
    // spawned four deep in front of the player, tracing against bodies, leaves
    // 9 of 16 ever seeing him - only the front rank, because each rank blinds
    // the one behind it. Against the world alone all 16 see, walk and arrive.
    PhysicsWorld::RayHit hit;
    if (!physics_->RayCast(a.pos, b.pos, hit, true))
        return true;                          // nothing in the way at all
    return hit.distance >= dist - 1e-3f;      // whatever it hit is past the target
}

// ENTITY.PO_SetMonsterMovementConst(e, value, flag) - 0x10130920, defaults
// 0.5 and false. Recorded; what the engine's mover does with them is not
// established, so nothing here reads them yet.
int ScriptEngine::L_PO_SetMonsterMovementConst(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    e->monsterMoveConst = float(luaL_optnumber(L, 2, 0.5));
    e->monsterMoveFlag = lua_toboolean(L, 3) != 0;
    return 0;
}

// ENTITY.PO_IsOnFloor(e) -> onFloor, nx, ny, nz. Four values (0x101341C0
// returns 4), and CAiBrain unpacks all four into the floor normal.
int ScriptEngine::L_PO_IsOnFloor(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->onFloor);
    for (int c = 0; c < 3; ++c)
        lua_pushnumber(L, e ? e->floorNormal[c] : (c == 1 ? 1.0 : 0.0));
    return 4;
}

int ScriptEngine::L_PO_Exist(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && self->physics_ && e->physicsBody >= 0 &&
                           self->physics_->ScriptBodyExists(e->physicsBody));
    return 1;
}

// CActor divides and multiplies by this; 0.8 is the scripts' own fallback
// for actors without a physics body.
int ScriptEngine::L_PO_GetMaxSphereRay(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const float r = (e && self->physics_ && e->physicsBody >= 0)
                        ? self->physics_->ScriptBodyRadius(e->physicsBody)
                        : 0.f;
    lua_pushnumber(L, r > 0.f ? r : 0.8);
    return 1;
}

int ScriptEngine::L_PO_SetMass(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyMass(e->physicsBody,
                                              float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetFriction(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyFriction(e->physicsBody,
                                                  float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetRestitution(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyRestitution(e->physicsBody,
                                                     float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetLinearDamping(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyLinearDamping(e->physicsBody,
                                                       float(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PO_SetAngularDamping(lua_State* L) {
    ScriptEngine* self = From(L);
    if (const Entity* e = self->Find(HandleArg(L, 1)))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyAngularDamping(e->physicsBody,
                                                        float(luaL_optnumber(L, 2, 0)));
    return 0;
}

// ---------------------------------------------------------------- player

// CreatePlayer(model, bool) -> the player's entity handle. The pawn itself
// is engine-side: the original's player locomotion is native code driven by
// the PlayerMove tweaks, and the scripts wrap the handle in CPlayer for
// health, weapons and pickups. The model ("player_box") is never drawn in
// first person - Game:AddPlayer sets Visible = false immediately.
int ScriptEngine::L_CreatePlayer(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity e;
    e.type = kModel;
    e.source = luaL_optstring(L, 1, "");
    e.name = "Player";
    const int handle = self->nextHandle_++;
    self->entities_.emplace(handle, e);
    ++self->created_;
    self->playerHandle_ = handle;
    self->pawnEnabled_ = true;
    if (self->pawn_) {
        const float at[3] = {0, 0, 0};
        self->pawn_->Spawn(at);
    }
    lua_pushnumber(L, handle);
    return 1;
}

int ScriptEngine::L_PO_SetPawnHeadPos(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const float p[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                        float(luaL_optnumber(L, 4, 0))};
    for (int i = 0; i < 3; ++i) e->pos[i] = p[i];
    if (self->pawn_ && HandleArg(L, 1) == self->playerHandle_) self->pawn_->SetHeadPos(p);
    return 0;
}

int ScriptEngine::L_PO_GetPawnHeadPos(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const float* p = (self->pawn_ && HandleArg(L, 1) == self->playerHandle_)
                         ? self->pawn_->headPos()
                         : (e ? e->pos : nullptr);
    lua_pushnumber(L, p ? p[0] : 0);
    lua_pushnumber(L, p ? p[1] : 0);
    lua_pushnumber(L, p ? p[2] : 0);
    return 3;
}

// PO_Enable / PO_IsEnabled: on the player this is the walk/fly switch
// (SwitchPlayerToPhysics); on a prop it wakes or sleeps the body.
int ScriptEngine::L_PO_IsEnabled(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    if (handle == self->playerHandle_ && self->playerHandle_) {
        lua_pushboolean(L, self->pawnEnabled_);
        return 1;
    }
    const Entity* e = self->Find(handle);
    lua_pushboolean(L, e && self->physics_ && e->physicsBody >= 0 &&
                           self->physics_->ScriptBodyExists(e->physicsBody));
    return 1;
}

int ScriptEngine::L_PO_Enable(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    const bool enable = lua_toboolean(L, 2) != 0;
    if (handle == self->playerHandle_ && self->playerHandle_) {
        self->pawnEnabled_ = enable;
        return 0;
    }
    if (const Entity* e = self->Find(handle))
        if (self->physics_ && e->physicsBody >= 0)
            self->physics_->SetScriptBodyEnabled(e->physicsBody, enable);
    return 0;
}

// ENTITY.PO_GetPawnFloorPos(e) -> the feet; the scripts' _groundx/y/z track
// this every player tick and the proximity helpers measure from it.
int ScriptEngine::L_PO_GetPawnFloorPos(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->pawn_ && HandleArg(L, 1) == self->playerHandle_) {
        float feet[3];
        self->pawn_->FloorPos(feet);
        lua_pushnumber(L, feet[0]);
        lua_pushnumber(L, feet[1]);
        lua_pushnumber(L, feet[2]);
        return 3;
    }
    return L_GetPosition(L);
}

// ENTITY.GetDimensions(e) -> w,h,d, world-space - Slab plates sink by their
// own height when they open, so this must be real for the ambush barriers
// to hide.
int ScriptEngine::L_GetDimensions(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    float dims[3] = {0, 0, 0};
    if (e && self->renderer_ && e->rendererInstance >= 0)
        self->renderer_->GetScriptDimensions(e->rendererInstance, dims);
    lua_pushnumber(L, dims[0]);
    lua_pushnumber(L, dims[1]);
    lua_pushnumber(L, dims[2]);
    return 3;
}

// PLAYER.GetDistanceFromPoint(e, x, y, z) - the pickup poll: every CItem
// measures the player's distance against its takeDistance each tick, and
// OnTake fires inside that radius.
int ScriptEngine::L_PLAYER_GetDistanceFromPoint(lua_State* L) {
    ScriptEngine* self = From(L);
    float from[3] = {0, 0, 0};
    if (self->pawn_ && HandleArg(L, 1) == self->playerHandle_) {
        // The pawn's body centre - between the head and the feet, which is
        // what the item's own Pos.Y-1 adjustment expects to measure against.
        self->pawn_->FloorPos(from);
        from[1] += 0.9f;
    } else if (const Entity* e = self->Find(HandleArg(L, 1))) {
        for (int i = 0; i < 3; ++i) from[i] = e->pos[i];
    }
    const float dx = from[0] - float(luaL_optnumber(L, 2, 0));
    const float dy = from[1] - float(luaL_optnumber(L, 3, 0));
    const float dz = from[2] - float(luaL_optnumber(L, 4, 0));
    lua_pushnumber(L, std::sqrt(dx * dx + dy * dy + dz * dz));
    return 1;
}

int ScriptEngine::L_IsDrawEnabled(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->visible && e->inWorld);
    return 1;
}

// GetPlayerSpeed() -> speed, jumpStrength; SetPlayerSpeed(speed [, jump]) -
// the natives at 0x1011df50/0x1011dea0 read and write the LIVE tweak fields
// (+0xc and +0x14 of the physics engine's tweak block), which is how demon
// mode and powerups retune movement.
int ScriptEngine::L_GetPlayerSpeed(lua_State* L) {
    ScriptEngine* self = From(L);
    const Tweaks* tweaks = self->physics_ ? &self->physics_->tweaks() : nullptr;
    double speed = self->playerSpeedOverride_;
    if (speed < 0)
        speed = tweaks ? tweaks->Number("PlayerMove.PlayerSpeed", 8.0) : 8.0;
    double jump = self->jumpStrengthOverride_;
    if (jump < 0)
        jump = tweaks ? tweaks->Number("PlayerMove.JumpStrength", 1.0) : 1.0;
    lua_pushnumber(L, speed);
    lua_pushnumber(L, jump);
    return 2;
}

int ScriptEngine::L_SetPlayerSpeed(lua_State* L) {
    ScriptEngine* self = From(L);
    self->playerSpeedOverride_ = float(luaL_optnumber(L, 1, -1.0));
    if (lua_isnumber(L, 2))
        self->jumpStrengthOverride_ = float(lua_tonumber(L, 2));
    return 0;
}

// ---------------------------------------------------------------- regions

// REGION.BuildFromPoint(e, points): a trigger volume from an array of
// vectors ({X=..., Y=..., Z=...} tables). Stored as the points' AABB - the
// shipped regions are boxes and box-shaped prisms; a genuine polygon prism
// test can replace this if a level ever needs one.
int ScriptEngine::L_REGION_BuildFromPoint(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !lua_istable(L, 2)) return 0;

    bool any = false;
    for (int i = 1;; ++i) {
        lua_rawgeti(L, 2, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        float p[3];
        const char* axes[3] = {"X", "Y", "Z"};
        bool ok = true;
        for (int a = 0; a < 3; ++a) {
            lua_pushstring(L, axes[a]);
            lua_gettable(L, -2);
            ok = ok && lua_isnumber(L, -1);
            p[a] = float(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (!ok) continue;
        for (int a = 0; a < 3; ++a) {
            if (!any || p[a] < e->regionMin[a]) e->regionMin[a] = p[a];
            if (!any || p[a] > e->regionMax[a]) e->regionMax[a] = p[a];
        }
        any = true;
    }
    e->isRegion = any;
    e->playerInside = false;
    return 0;
}

void ScriptEngine::TickTriggers() {
    if (!host_ || !playerHandle_) return;
    const Entity* player = Find(playerHandle_);
    if (!player) return;

    // The test point follows the scripts' own convention: a metre above the
    // feet, the same PY+1 every IsInside caller uses.
    float at[3];
    if (pawn_) {
        pawn_->FloorPos(at);
    } else {
        for (int i = 0; i < 3; ++i) at[i] = player->pos[i];
    }
    at[1] += 1.f;

    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (!e.isRegion) continue;
        bool inside = true;
        for (int a = 0; a < 3 && inside; ++a)
            inside = at[a] >= e.pos[a] + e.regionMin[a] &&
                     at[a] <= e.pos[a] + e.regionMax[a];
        if (inside == e.playerInside) continue;
        e.playerInside = inside;
        const double args[2] = {double(kv.first), double(playerHandle_)};
        host_->PostMsg(inside ? "REGION_ENTERED" : "REGION_LEFT", args, 2);
    }
}

// The CAM reads, from the pose the game loop feeds each frame. GetAng is in
// degrees (CActor converts with -x * 3.14/180), GetAngRad in radians
// (CPlayer wraps it straight into 0..2pi).
int ScriptEngine::L_CAM_GetPos(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushnumber(L, self->camPos_[0]);
    lua_pushnumber(L, self->camPos_[1]);
    lua_pushnumber(L, self->camPos_[2]);
    return 3;
}

int ScriptEngine::L_CAM_GetForwardVector(lua_State* L) {
    ScriptEngine* self = From(L);
    const float cp = std::cos(self->camPitch_);
    lua_pushnumber(L, std::cos(self->camYaw_) * cp);
    lua_pushnumber(L, std::sin(self->camPitch_));
    lua_pushnumber(L, std::sin(self->camYaw_) * cp);
    return 3;
}

// Our camera yaw is measured from +X turning toward +Z. The engine's turn
// angle is not: reading the scripts' own maths back out shows their basis at
// turn a is right = (cos a, 0, -sin a), forward = (-sin a, 0, -cos a) - it
// starts down -Z and runs the opposite way round. Matching that against our
// right = (-sin yaw, 0, cos yaw) gives turn = -(yaw + pi/2), and the
// elevation passes through unchanged (both put sin(pitch) in Y).
//
// This is load-bearing, not cosmetic: CPlayer:SetupAction rebuilds the
// player's whole movement basis from CAM.GetAngRad in pure Lua, so getting
// it wrong walks the player at ninety degrees to where the camera looks.
// The conversion is its own inverse, which is what the handover in Tick2
// needs. Verified against CAM.GetForwardVector, which computes the same
// basis on the C++ side and must agree.

static float EngineTurn(float camYaw) {
    return camYaw + kPi * 0.5f;
}

static float CamYawFromTurn(float turn) {
    return turn - kPi * 0.5f;
}

// The engine's elevation runs the OTHER WAY from our pitch: positive is
// looking DOWN. That is not arbitrary - the scripts feed the elevation into
// the X slot of the engine Euler (CPlayer:SetupAction builds
// FromEuler(elevation, turn, 0)), and a positive rotation about X in a
// Y-up, Z-forward frame tilts forward toward -Y. Report our pitch without
// this and the horizontal aim is perfect while every shot goes as far wrong
// vertically as the player was looking. Its own inverse, like the turn.
static float EngineElevation(float camPitch) {
    return -camPitch;
}

// CAM.SetPos / CAM.SetAng - the scripts steering the view. A level seats the
// camera this way at load (CLevel:Synchronize while the mouse is unlocked
// pushes Lev.Pos and Lev.Ang out), and Game:Tick2 steers it this way during
// play. Angles arrive in degrees, in the engine's turn convention, so they
// come back through the same conversion - which is its own inverse.
int ScriptEngine::L_CAM_SetPos(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int i = 0; i < 3; ++i)
        self->camPos_[i] = float(luaL_optnumber(L, i + 1, self->camPos_[i]));
    self->camPoseDirty_ = true;
    return 0;
}

int ScriptEngine::L_CAM_SetAng(lua_State* L) {
    ScriptEngine* self = From(L);
    const float k = kPi / 180.f;
    self->camYaw_ = CamYawFromTurn(float(luaL_optnumber(L, 1, 0)) * k);
    self->camPitch_ = EngineElevation(float(luaL_optnumber(L, 2, 0)) * k);
    self->camPoseDirty_ = true;
    return 0;
}

bool ScriptEngine::TakeCameraPose(float pos[3], float& yaw, float& pitch) {
    if (!camPoseDirty_) return false;
    camPoseDirty_ = false;
    for (int i = 0; i < 3; ++i) pos[i] = camPos_[i] + camDisplacement_[i];
    yaw = camYaw_;
    pitch = camPitch_;
    return true;
}

int ScriptEngine::L_CAM_GetAng(lua_State* L) {
    ScriptEngine* self = From(L);
    const float k = 180.f / kPi;
    lua_pushnumber(L, EngineTurn(self->camYaw_) * k);
    lua_pushnumber(L, EngineElevation(self->camPitch_) * k);
    lua_pushnumber(L, 0);
    return 3;
}

int ScriptEngine::L_CAM_GetAngRad(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushnumber(L, EngineTurn(self->camYaw_));
    lua_pushnumber(L, EngineElevation(self->camPitch_));
    lua_pushnumber(L, 0);
    return 3;
}

// The head-to-camera offset the original applies when the pawn drives the
// view; zero until the crouch/land bob that feeds it exists.
int ScriptEngine::L_PLAYER_GetCameraFix(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// CAM.GetRawRotation() -> the accumulated look angles in DEGREES - Game's
// camera tick wraps them with math.mod(...,360). While the C++ loop drives
// the camera, these mirror its state and MOUSE.GetDelta reports no motion,
// so the script-side accumulation is a faithful no-op; handing the camera
// to the scripts entirely means feeding real deltas here instead.
int ScriptEngine::L_CAM_GetRawRotation(lua_State* L) {
    ScriptEngine* self = From(L);
    const float k = 180.f / kPi;
    lua_pushnumber(L, EngineTurn(self->camYaw_) * k);
    lua_pushnumber(L, EngineElevation(self->camPitch_) * k);
    return 2;
}

// MOUSE.GetDelta() -> look movement in degrees since the last call. This is
// the whole of the look input: Game:UpdateViewFromPlayer adds it onto
// CAM.GetRawRotation and writes the result back through CAM.SetAng, so the
// scripts own the view and the C++ camera follows them.
int ScriptEngine::L_MOUSE_GetDelta(lua_State* L) {
    ScriptEngine* self = From(L);
    float dx = 0.f, dy = 0.f;
    if (self->input_) self->input_->TakeLookDegrees(dx, dy);
    lua_pushnumber(L, dx);
    lua_pushnumber(L, dy);
    return 2;
}

int ScriptEngine::L_MOUSE_SetSensitivity(lua_State* L) {
    if (Input* in = From(L)->input_) in->SetSensitivity(float(luaL_optnumber(L, 1, 40)));
    return 0;
}

// CAM.SetPositionDisplacement(x, y, z) - an offset added to the camera
// position after it is set, which is how the engine shakes the view without
// disturbing where the player actually is. Held apart from camPos_ so the
// CAM.GetPos the scripts read stays the true eye position.
int ScriptEngine::L_CAM_SetPositionDisplacement(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int i = 0; i < 3; ++i)
        self->camDisplacement_[i] = float(luaL_optnumber(L, i + 1, 0));
    self->camPoseDirty_ = true;
    return 0;
}

// ----------------------------------------------------------------- input
//
// The player's controls are a SCRIPT path in this engine, and these natives
// are its two ends. CPlayer:Tick reads INP.GetActionStatus into an Actions
// bitmask, overrides bits of it (weapon select, switched fire, rocket jump),
// stores it with ENTITY.PO_SetAction, and then calls PLAYER.ExecAction - and
// ExecAction is the entry to PhysicsObject::PlayerAction, the mover. Nothing
// here decides how the player moves; the mask decides, and PlayerPawn runs it.

// INP.GetActionStatus(e) -> the pressed-actions bitmask. With no input
// attached this is zero, which reads as a standing player rather than an
// error.
int ScriptEngine::L_INP_GetActionStatus(lua_State* L) {
    const ScriptEngine* self = From(L);
    lua_pushnumber(L, self->input_ ? double(self->input_->ActionMask()) : 0.0);
    return 1;
}

// INP.Action(mask) / INP.UIAction(mask) -> is that action pressed. The
// scripts pass one Actions.* constant at a time.
int ScriptEngine::L_INP_Action(lua_State* L) {
    const ScriptEngine* self = From(L);
    const uint32_t mask = uint32_t(luaL_optnumber(L, 1, 0));
    lua_pushboolean(L, self->input_ && self->input_->Action(mask));
    return 1;
}

int ScriptEngine::L_INP_UIAction(lua_State* L) {
    const ScriptEngine* self = From(L);
    const uint32_t mask = uint32_t(luaL_optnumber(L, 1, 0));
    lua_pushboolean(L, self->input_ && self->input_->UIAction(mask));
    return 1;
}

// INP.Key(vk) -> 0 up, 1 pressed this frame, 2 held. Tri-state, not boolean:
// Game.lua pairs `==1` for a toggle with `==2` for a modifier held alongside
// it, so collapsing this to a bool breaks both halves.
int ScriptEngine::L_INP_Key(lua_State* L) {
    const ScriptEngine* self = From(L);
    const int vk = int(luaL_optnumber(L, 1, 0));
    lua_pushnumber(L, self->input_ ? self->input_->KeyState(vk) : 0);
    return 1;
}

int ScriptEngine::L_INP_IsFireSwitched(lua_State* L) {
    const ScriptEngine* self = From(L);
    lua_pushboolean(L, self->input_ && self->input_->fireSwitched());
    return 1;
}

// INP.LoadBindings() - the bindings live in the scripts' own Cfg table
// (Cfg.KeyPrimary<Action> / Cfg.KeyAlternative<Action>, holding engine key
// names), which Cfg.lua fills from its defaults and then config.ini. So this
// reads them straight back out of the Lua state; the options menu calls it
// again after a rebind.
int ScriptEngine::L_INP_LoadBindings(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->input_) return 0;
    self->input_->LoadBindings(
        [](void* ctx, const char* field) -> std::string {
            lua_State* S = static_cast<lua_State*>(ctx);
            lua_getglobal(S, "Cfg");
            if (!lua_istable(S, -1)) {
                lua_pop(S, 1);
                return {};
            }
            lua_pushstring(S, field);
            lua_gettable(S, -2);
            std::string value;
            if (lua_isstring(S, -1)) value = lua_tostring(S, -1);
            lua_pop(S, 2);
            return value;
        },
        L);
    return 0;
}

int ScriptEngine::L_INP_Reset(lua_State* L) {
    if (Input* in = From(L)->input_) in->Reset();
    return 0;
}

// ENTITY.PO_SetAction(e, mask) / PO_AddAction(e, mask) - the action bitmask
// on the physics object (PlayerAction reads it from this+0x78). SetAction
// replaces, AddAction ORs.
int ScriptEngine::L_PO_SetAction(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->action = uint32_t(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PO_AddAction(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->action |= uint32_t(luaL_optnumber(L, 2, 0));
    return 0;
}

// ENTITY.PO_IsActionState(e, mask) - is that bit set in the stored action.
// The weapon code reads its own fire bits back out this way, so it answers
// over the whole mask, not just the five bits the mover consumes.
int ScriptEngine::L_PO_IsActionState(lua_State* L) {
    const Entity* e = From(L)->Find(HandleArg(L, 1));
    const uint32_t mask = uint32_t(luaL_optnumber(L, 2, 0));
    lua_pushboolean(L, e && (e->action & mask) != 0);
    return 1;
}

// ENTITY.PO_JumpedInLastAction(e) - whether the last mover step left the
// ground, which the scripts use to gate landing behaviour.
int ScriptEngine::L_PO_JumpedInLastAction(lua_State* L) {
    const Entity* e = From(L)->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->jumpedLastAction);
    return 1;
}

// PLAYER.ExecAction(e, 0, fx,fy,fz, rx,ry,rz) - run the mover for one frame.
// The two vectors are the camera basis; PlayerAction takes them as Vector&
// param_1 and param_2 and builds the ground direction from the RIGHT one
// alone. Only the player has a pawn, so this is a no-op for anything else.
int ScriptEngine::L_PLAYER_ExecAction(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    Entity* e = self->Find(handle);
    if (!e || !self->pawn_ || handle != self->playerHandle_ || !self->pawnEnabled_)
        return 0;
    if (!self->physics_) return 0;

    const float right[3] = {float(luaL_optnumber(L, 6, 0)),
                            float(luaL_optnumber(L, 7, 0)),
                            float(luaL_optnumber(L, 8, 0))};
    const bool wasOnGround = self->pawn_->onGround();
    self->pawn_->Move(*self->physics_, self->physics_->tweaks(), e->action, right,
                      self->frameDelta_);
    e->jumpedLastAction = wasOnGround && !self->pawn_->onGround();
    self->SyncPlayerFromPawn();
    return 0;
}

// PLAYER.FloorCheck(e) - is the player standing on something. CPlayer gates
// the rocket jump on it, and the camera code on whether to bob.
int ScriptEngine::L_PLAYER_FloorCheck(lua_State* L) {
    const ScriptEngine* self = From(L);
    lua_pushboolean(L, self->pawn_ && HandleArg(L, 1) == self->playerHandle_ &&
                           self->pawn_->onGround());
    return 1;
}

int ScriptEngine::L_MOUSE_Lock(lua_State* L) {
    From(L)->mouseLocked_ = lua_toboolean(L, 1) != 0;
    return 0;
}

int ScriptEngine::L_MOUSE_IsLocked(lua_State* L) {
    lua_pushboolean(L, From(L)->mouseLocked_);
    return 1;
}

// ---------------------------------------------------------------- traces
//
// WORLD.LineTrace(x1,y1,z1, x2,y2,z2) -> hit, distance, hitX,hitY,hitZ,
// normalX,normalY,normalZ, bodyHandle, entityHandle. That ten-value shape is
// what every caller unpacks, and the last two are how a hit is identified:
// the body handle goes to the PHYSICS.GetHavokBody* family, and the entity
// handle to EntityToObject. A world hit reports entity 0, which is what makes
// ENTITY.IsFixedMesh answer true for it.
int ScriptEngine::TraceCommon(lua_State* L, bool staticOnly) {
    ScriptEngine* self = From(L);
    const float from[3] = {float(luaL_optnumber(L, 1, 0)), float(luaL_optnumber(L, 2, 0)),
                           float(luaL_optnumber(L, 3, 0))};
    const float to[3] = {float(luaL_optnumber(L, 4, 0)), float(luaL_optnumber(L, 5, 0)),
                         float(luaL_optnumber(L, 6, 0))};

    PhysicsWorld::RayHit hit;
    const bool got = self->TraceRay(from, to, hit, staticOnly);

    lua_pushboolean(L, got);
    lua_pushnumber(L, got ? hit.distance : 0.0);
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, got ? hit.point[c] : to[c]);
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, got ? hit.normal[c] : 0.0);
    lua_pushnumber(L, got ? hit.bodySlot : -1);
    lua_pushnumber(L, got ? self->EntityForBody(hit.bodySlot) : 0);
    return 10;
}

int ScriptEngine::L_WORLD_LineTrace(lua_State* L) {
    return TraceCommon(L, false);
}

// LineTraceFixedGeom asks about the world mesh alone. The actors use it for
// their ground and step probes, where hitting each other would be noise.
int ScriptEngine::L_WORLD_LineTraceFixedGeom(lua_State* L) {
    return TraceCommon(L, true);
}

bool ScriptEngine::TraceRay(const float from[3], const float to[3],
                            PhysicsWorld::RayHit& hit, bool staticOnly) const {
    if (!physics_) return false;
    return physics_->RayCast(from, to, hit, staticOnly,
                             excludedSlots_.empty() ? nullptr : excludedSlots_.data(),
                             excludedSlots_.size());
}

int ScriptEngine::EntityForBody(int bodySlot) const {
    if (bodySlot < 0) return 0;                  // the world
    auto it = bodyToEntity_.find(bodySlot);
    return it == bodyToEntity_.end() ? 0 : it->second;
}

// ENTITY.RemoveFromIntersectionSolver(e) / AddToIntersectionSolver(e) - take
// an entity out of the traces and put it back. Always bracketed, so this has
// to be exact: leaking a Remove would leave something permanently unhittable.
// The ragdoll variants say the same thing about an actor's ragdoll, which is
// the same body here.
int ScriptEngine::L_RemoveFromIntersectionSolver(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !e->inSolver) return 0;
    e->inSolver = false;
    if (e->physicsBody >= 0) self->excludedSlots_.push_back(e->physicsBody);
    return 0;
}

int ScriptEngine::L_AddToIntersectionSolver(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || e->inSolver) return 0;
    e->inSolver = true;
    if (e->physicsBody >= 0) {
        auto& v = self->excludedSlots_;
        v.erase(std::remove(v.begin(), v.end(), e->physicsBody), v.end());
    }
    return 0;
}

// ENTITY.IsFixedMesh(e) - is this the immovable world rather than something
// that can be moved or hurt. A trace into the world reports entity 0, and an
// entity with no simulated body is fixed in the same sense.
int ScriptEngine::L_IsFixedMesh(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    if (handle == 0) {
        lua_pushboolean(L, 1);
        return 1;
    }
    const Entity* e = self->Find(handle);
    lua_pushboolean(L, e != nullptr && e->physicsBody < 0);
    return 1;
}

// ENTITY.SetPosAndRotRelativeToCamera(e, x,y,z, ax,ay,az) - the view model.
// CWeapon:ClientTick2 parks the held weapon this way every frame, at a fixed
// offset in CAMERA space (the shipped one is 0.39 right, 0.49 down, 1.2
// forward) with Euler angles on top.
//
// Camera space is the scripts' own: -Z is forward, which is what their
// forward vector reduces to at a turn of zero. The camera's own orientation
// is the engine Euler (elevation, turn, 0) - the same pair CAM.GetAngRad
// reports - so the offset rotates by that and the model's rotation composes
// after it.
int ScriptEngine::L_SetPosAndRotRelativeToCamera(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;

    const float lx = float(luaL_optnumber(L, 2, 0));
    const float ly = float(luaL_optnumber(L, 3, 0));
    const float lz = float(luaL_optnumber(L, 4, 0));

    // Camera space is +X right, +Y up, -Z forward. Placing the offset from
    // the camera's own basis keeps this independent of how a rotation is
    // spelled as a quaternion, and that basis is the one already driving both
    // the view matrix and the player's movement.
    const float cp = std::cos(self->camPitch_), sp = std::sin(self->camPitch_);
    const float cy = std::cos(self->camYaw_), sy = std::sin(self->camYaw_);
    const float fwd[3] = {cy * cp, sp, sy * cp};
    const float right[3] = {-sy, 0.f, cy};
    // up = right x forward, which tilts with the pitch as the view does.
    const float up[3] = {right[1] * fwd[2] - right[2] * fwd[1],
                         right[2] * fwd[0] - right[0] * fwd[2],
                         right[0] * fwd[1] - right[1] * fwd[0]};
    for (int c = 0; c < 3; ++c)
        e->pos[c] = self->camPos_[c] + right[c] * lx + up[c] * ly - fwd[c] * lz;

    // The orientation, built the same way. EngineQuatToRot9 is applied to ROW
    // vectors, so its rows are where the local axes land - which means the
    // camera's rotation is just its basis written out as rows, and there is
    // no quaternion convention left to get wrong.
    const float camRot[9] = {right[0], right[1], right[2],
                             up[0],    up[1],    up[2],
                             -fwd[0],  -fwd[1],  -fwd[2]};
    // The TURN is negated, the same way ENTITY.SetOrientation negates it when
    // it builds its yaw quaternion. Passing it raw here left every viewmodel
    // rotated to show its far side: the stakegun's own template asks for a
    // yaw of -1.57, and with the wrong sign the gun sits in exactly the right
    // place while presenting its back, so the gaps between its parts read as
    // holes punched through a solid model. Nothing was missing - all sixteen
    // meshes draw, all 3137 triangles - it was simply turned around.
    float localQuat[4], localRot[9], worldRot[9];
    EngineEulerToQuat(float(luaL_optnumber(L, 5, 0)), -float(luaL_optnumber(L, 6, 0)),
                      float(luaL_optnumber(L, 7, 0)), localQuat);
    EngineQuatToRot9(localQuat, localRot);
    // Row-vector order: the weapon's own rotation first, then the camera's.
    EngineRot9Mul(localRot, camRot, worldRot);
    EngineRot9ToQuat(worldRot, e->rotWXYZ);

    self->SyncPose(*e);
    return 0;
}

// PARTICLE.SetEvolve(e, on) - force continuous emission on every emitter of
// an effect, overriding a one-shot .ini. CParticleFX:LoadData calls it right
// after loading and Apply calls it again, which is how a level-placed torch
// keeps burning while the same emitter data used for an impact fires once.
int ScriptEngine::L_PARTICLE_SetEvolve(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->particles_) return 0;
    const bool on = lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0 : true;
    for (int slot : e->emitterSlots)
        if (slot >= 0) self->particles_->SetScriptEmitterEvolve(slot, on);
    return 0;
}

// ENTITY.GetType(e) -> the ETypes value it was created with.
int ScriptEngine::L_GetType(lua_State* L) {
    const Entity* e = From(L)->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->type : 0);
    return 1;
}

// ------------------------------------------------------------ the anim clock
//
// This is the half of animation the GAME waits on, as opposed to the half the
// eye does. CActor:Tick opens its whole animation-event loop with
//
//     local animSpeed = MDL.GetAnimTimeScale(self._Entity, self._CurAnimIndex)
//     if animSpeed > 0 then ... while self._AnimationEvents[i] do ...
//
// and the events are declared in the actor's own template as
// {timeInSeconds, method, arg}. So melee damage, footsteps, attack sounds and
// the sequencing of every actor state hang off nothing more than a per-entity
// timer and a duration. None of it needs a single triangle drawn.

// MDL.SetAnim(e, anim, loop, speed, blend, mcurve, hasMovingCurveRot) -> the
// animation's index on this entity, or -1 when the model has no such track -
// which the scripts handle as "carry on without it", so it must stay a
// negative number rather than an error.
//
// blend, mcurve and hasMovingCurveRot are accepted and ignored: blending and
// root motion are their own problems, and guessing at them is how conventions
// get broken here. See Docs/Animation.md.
int ScriptEngine::L_MDL_SetAnim(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
    if (!e || !name || !*name) {
        lua_pushnumber(L, -1);
        return 1;
    }

    const Animation* anim = self->animations_.Get(e->source, name);
    if (!anim) {
        lua_pushnumber(L, -1);
        return 1;
    }

    // Find the slot this animation already has on the entity, or append one.
    int index = -1;
    for (size_t i = 0; i < e->animSlots.size(); ++i) {
        if (e->animSlots[i].name == name) {
            index = int(i);
            break;
        }
    }
    if (index < 0) {
        e->animSlots.push_back({name, anim->duration(), anim});
        index = int(e->animSlots.size()) - 1;
    }

    // Start the cross-fade out of whatever was playing, before the index moves.
    // Only when the animation actually changes: CActor re-sets the same
    // animation constantly, and restarting the fade every tick would leave an
    // actor permanently half-way between a pose and itself.
    const Animation* previous =
        (e->animIndex >= 0 && size_t(e->animIndex) < e->animSlots.size())
            ? e->animSlots[size_t(e->animIndex)].anim
            : nullptr;
    if (previous && previous != anim) {
        e->blendFrom = previous;
        e->blendFromTime = e->animTime;
        e->blendFromTracks.clear();      // resolved lazily against the skeleton
        e->blendTotal = float(luaL_optnumber(L, 5, 0.201));
        e->blendLeft = e->blendTotal;
    }

    e->animIndex = index;
    e->animTime = 0.f;
    // Every default here is Engine.dll's own (SetAnim, 0x1013BFC0): looping is
    // GetBool(3, TRUE), not false - a plain SetAnim(e, "idle") is a looping
    // idle, and several shipped call sites rely on that by omitting the
    // argument entirely.
    e->animLoop = lua_isnil(L, 3) || lua_isnone(L, 3) ? true : lua_toboolean(L, 3) != 0;
    // The template's declared speed. A speed of zero would stall the event
    // loop the moment it started, so an unspecified or zero speed plays at 1.
    const float speed = float(luaL_optnumber(L, 4, 1.0));
    e->animScale = speed > 0.f ? speed : 1.f;

    // The movement curve. Only set when the mask is positive, exactly as the
    // engine does - it calls SetAnimationMovementCurve only for mcurve > 0 and
    // otherwise leaves whatever the animation already had.
    Entity::AnimSlot& slot = e->animSlots[size_t(index)];
    const uint32_t mask = uint32_t(luaL_optnumber(L, 6, 0));
    if (mask > 0) {
        const char* bone = lua_isstring(L, 7) ? lua_tostring(L, 7) : "ROOOT";
        if (slot.curveMask != mask || slot.curveBone != bone) {
            slot.curveMask = mask;
            slot.curveBone = bone;
            slot.curveBoneIndex = -2;      // resolve against the skeleton lazily
        }
    }

    lua_pushnumber(L, index);
    return 1;
}

// The slot an MDL call is asking about. Defaults to the one playing, since
// that is what the scripts pass in every case that matters.
const ScriptEngine::Entity::AnimSlot* ScriptEngine::AnimSlotArg(const Entity* e,
                                                                lua_State* L, int arg) {
    if (!e) return nullptr;
    const int index = lua_isnumber(L, arg) ? int(lua_tonumber(L, arg)) : e->animIndex;
    if (index < 0 || size_t(index) >= e->animSlots.size()) return nullptr;
    return &e->animSlots[index];
}

// MDL.GetAnimLength(e, index) -> the track's duration in seconds. CActor
// stores it as _CurAnimLength and sequences against it.
int ScriptEngine::L_MDL_GetAnimLength(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const Entity::AnimSlot* slot = AnimSlotArg(e, L, 2);
    lua_pushnumber(L, slot ? slot->length : 0.0);
    return 1;
}

// MDL.GetAnimTime(e, index) -> how far into it we are. Only the playing
// animation has a clock; anything else reads as not started.
int ScriptEngine::L_MDL_GetAnimTime(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const int index = lua_isnumber(L, 2) ? int(lua_tonumber(L, 2)) : (e ? e->animIndex : -1);
    lua_pushnumber(L, (e && index == e->animIndex) ? e->animTime : 0.0);
    return 1;
}

int ScriptEngine::L_MDL_SetAnimTime(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (e) e->animTime = float(luaL_optnumber(L, 3, 0));
    return 0;
}

// MDL.GetAnimTimeScale / SetAnimTimeScale - the playback speed, and the gate
// the event loop tests. CActor pauses an animation by storing the scale,
// setting it to 0 and restoring it later, which is what says this is a speed
// rather than a flag.
int ScriptEngine::L_MDL_GetAnimTimeScale(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    const bool playing = e && e->animIndex >= 0;
    lua_pushnumber(L, playing ? e->animScale : 0.0);
    return 1;
}

int ScriptEngine::L_MDL_SetAnimTimeScale(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (e) e->animScale = float(luaL_optnumber(L, 3, 1.0));
    return 0;
}

// MDL.ResetFrame(e) - back to the first frame without changing which
// animation is playing.
int ScriptEngine::L_MDL_ResetFrame(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1))) e->animTime = 0.f;
    return 0;
}

// MDL.LoadAnim(e, anim) - preload, so the first play does not read a file
// mid-frame. Answers the same index SetAnim would.
int ScriptEngine::L_MDL_LoadAnim(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
    if (!e || !name || !*name || !self->animations_.Get(e->source, name)) {
        lua_pushnumber(L, -1);
        return 1;
    }
    for (size_t i = 0; i < e->animSlots.size(); ++i)
        if (e->animSlots[i].name == name) {
            lua_pushnumber(L, double(i));
            return 1;
        }
    const Animation* loaded = self->animations_.Get(e->source, name);
    e->animSlots.push_back({name, loaded->duration(), loaded});
    lua_pushnumber(L, double(e->animSlots.size() - 1));
    return 1;
}

// MDL.GetAnimMovement(e, index, delta) -> how far the animation itself moves
// the actor over `delta`: root motion, the thing that carries a monster
// forward during an attack.
//
// Engine.dll answers this by sampling the animation's movement curve twice and
// subtracting (0x1012C210 -> Model::GetAnimationMovement 0x101DE890 ->
// FUN_1001BB60):
//
//     movement = curve(t + delta * speed) - curve(t)
//
// The curve is a NAMED BONE, set by SetAnim's 6th and 7th arguments and
// defaulting to "ROOOT" in the engine's own argument default - which is the
// name of bone 0 in the shipped rigs. The mask says which components count;
// a turn animation asks for ETransX + ETransZ + ERot, deliberately leaving out
// the vertical so an animation's bob cannot lift the actor off the floor.
//
// It must return three numbers whatever happens: CActor multiplies them the
// moment it has a moving curve, and returning nothing throws an arithmetic
// error that aborts the whole tick - which is how this need first announced
// itself.
int ScriptEngine::L_MDL_GetAnimMovement(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int index = int(luaL_optnumber(L, 2, -1));
    const float delta = float(luaL_optnumber(L, 3, 0));

    float move[3] = {0, 0, 0};
    if (e && index >= 0 && size_t(index) < e->animSlots.size())
        self->AnimMovement(*e, index, delta, move);

    for (int c = 0; c < 3; ++c) lua_pushnumber(L, move[c]);
    return 3;
}

// The bone the movement curve names, looked up once per slot. -1 when the
// model has no such bone, which is an ordinary answer for a model whose
// template names a curve bone it does not carry.
int ScriptEngine::ResolveCurveBone(Entity::AnimSlot& slot,
                                   const SkeletonCache::Entry& skel) {
    if (slot.curveBoneIndex == -2) {
        slot.curveBoneIndex = -1;
        for (size_t i = 0; i < skel.bones.size(); ++i)
            if (EqualsCI(skel.bones[i].name, slot.curveBone.c_str())) {
                slot.curveBoneIndex = int(i);
                break;
            }
    }
    return slot.curveBoneIndex;
}

void ScriptEngine::AnimMovement(Entity& e, int index, float delta, float out[3]) {
    Entity::AnimSlot& slot = e.animSlots[size_t(index)];
    if (slot.curveMask == 0 || !slot.anim) return;

    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel) return;

    if (ResolveCurveBone(slot, *skel) < 0) return;

    // The curve is read off the animation named by this slot, which is not
    // necessarily the one playing - the scripts pass an explicit index.
    ResolveAnimTracks(skel->bones, *slot.anim, curveTracks_);

    const float t0 = e.animTime;
    // Held at the last key rather than wrapped. A looping animation crossing
    // its own end would otherwise report the whole loop's travel as one
    // backwards lurch; holding makes that step contribute nothing, which loses
    // a fraction of a frame of travel instead of teleporting the actor.
    const float t1 = std::min(t0 + delta * e.animScale, slot.length);

    float a[3], b[3];
    if (!ComputeBonePositionAtTime(skel->bones, curveTracks_, slot.curveBoneIndex, t0, a) ||
        !ComputeBonePositionAtTime(skel->bones, curveTracks_, slot.curveBoneIndex, t1, b))
        return;

    // MovingCurve, Definitions.lua: ETransX 1, ETransY 2, ETransZ 4. ERot (8)
    // is a rotation channel and does not belong in a translation.
    static const uint32_t kAxisBit[3] = {1, 2, 4};
    for (int c = 0; c < 3; ++c)
        if (slot.curveMask & kAxisBit[c]) out[c] = (b[c] - a[c]) * e.scale;
}

const std::vector<Mat4>* ScriptEngine::PosedBones(Entity& e) {
    if (e.type != kModel || e.source.empty()) return nullptr;
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel || skel->bones.empty()) return nullptr;

    const Animation* anim = (e.animIndex >= 0 && size_t(e.animIndex) < e.animSlots.size())
                                ? e.animSlots[size_t(e.animIndex)].anim
                                : nullptr;

    // Matching bone names to tracks is a string lookup per bone, so it happens
    // only when the animation itself changes - not on every query, and not on
    // every frame.
    if (e.pose.anim != anim || e.pose.tracks.size() != skel->bones.size()) {
        e.pose.anim = anim;
        if (anim) ResolveAnimTracks(skel->bones, *anim, e.pose.tracks);
        else      e.pose.tracks.assign(skel->bones.size(), nullptr);
        e.pose.time = -1.f;                      // force a rebuild below
    }

    // The weight is 0 at the moment of the switch and 1 when the fade is done.
    const float blendU = (e.blendFrom && e.blendTotal > 1e-6f)
                             ? 1.f - e.blendLeft / e.blendTotal
                             : 1.f;

    if (e.pose.time != e.animTime || e.pose.rotVersion != e.jointRotVersion ||
        e.pose.blendU != blendU || e.pose.boneWorld.size() != skel->bones.size()) {
        if (e.blendFrom && blendU < 1.f) {
            if (e.blendFromTracks.size() != skel->bones.size())
                ResolveAnimTracks(skel->bones, *e.blendFrom, e.blendFromTracks);
            ComputeBoneWorldBlended(skel->bones, e.blendFromTracks, e.blendFromTime,
                                    e.pose.tracks, e.animTime, blendU, e.pose.boneWorld,
                                    e.jointRot.data(), e.jointRot.size());
        } else {
            ComputeBoneWorldAtTime(skel->bones, e.pose.tracks, e.animTime, e.pose.boneWorld,
                                   e.jointRot.data(), e.jointRot.size());
        }
        e.pose.time = e.animTime;
        e.pose.rotVersion = e.jointRotVersion;
        e.pose.blendU = blendU;

        // Take the root motion back out of the POSE.
        //
        // An animation with a movement curve carries its own travel: the walk
        // cycle slides ROOOT 25.9 model units down +Z, and every bone hangs off
        // it. That travel is extracted by GetAnimMovement and spent on the
        // ENTITY, so leaving it in the pose as well moves the actor twice -
        // the mesh strides ahead of where the monster actually is and snaps
        // back to it every time the loop wraps.
        //
        // Only the axes the curve declares are removed. ETransZ takes the
        // forward travel out and deliberately leaves the vertical, so the
        // actor still bobs as it walks.
        if (anim && e.animIndex >= 0) {
            Entity::AnimSlot& slot = e.animSlots[size_t(e.animIndex)];
            if (slot.curveMask != 0 && ResolveCurveBone(slot, *skel) >= 0) {
                float at[3];
                if (ComputeBonePositionAtTime(skel->bones, e.pose.tracks,
                                              slot.curveBoneIndex, e.animTime, at)) {
                    static const uint32_t kAxisBit[3] = {1, 2, 4};
                    for (int c = 0; c < 3; ++c)
                        if (!(slot.curveMask & kAxisBit[c])) at[c] = 0.f;
                    for (Mat4& m : e.pose.boneWorld)
                        for (int c = 0; c < 3; ++c) m.m[12 + c] -= at[c];
                }
            }
        }
    }
    return &e.pose.boneWorld;
}

bool ScriptEngine::JointToWorld(Entity& e, int joint, const float local[3],
                                float out[3]) {
    const std::vector<Mat4>* bones = PosedBones(e);
    if (!bones || joint < 0 || size_t(joint) >= bones->size()) return false;

    // Bone-local -> model space by the posed bone, then model -> world by the
    // entity's own transform, built exactly as the renderer builds it
    // (Properties.cpp ReadRotation's matrix form, scaled by the entity scale
    // the scripts' *0.1 rule already produced). If these two ever disagree, a
    // muzzle flash drifts off the barrel it is drawn on.
    float model[3];
    (*bones)[size_t(joint)].TransformPoint(local[0], local[1], local[2], model);

    float rot[9];
    EngineQuatToRot9(e.rotWXYZ, rot);
    for (int c = 0; c < 3; ++c)
        out[c] = e.pos[c] + e.scale * (model[0] * rot[0 * 3 + c] +
                                       model[1] * rot[1 * 3 + c] +
                                       model[2] * rot[2 * 3 + c]);
    return true;
}

// MDL.GetJointIndex(e, name) -> the bone's index, or -1.
//
// The scripts hold onto what this returns (aiParams.weaponBindPos names the
// bone a weapon rides) and pass it back to every other joint call, so the
// index has to be the bone's own position in the model's bone list.
int ScriptEngine::L_MDL_GetJointIndex(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const char* name = lua_tostring(L, 2);
    lua_pushnumber(L, -1);
    if (!e || !name || e->type != kModel) return 1;

    const SkeletonCache::Entry* skel = self->skeletons_.Get(e->source);
    if (!skel) return 1;
    for (size_t i = 0; i < skel->bones.size(); ++i)
        if (EqualsCI(skel->bones[i].name, name)) {
            lua_pop(L, 1);
            lua_pushnumber(L, double(i));
            return 1;
        }
    return 1;   // -1: a model without that bone is an answer the scripts test
}

// MDL.GetJointName(e, joint) -> the bone's name, or nothing.
int ScriptEngine::L_MDL_GetJointName(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int joint = int(lua_tonumber(L, 2));
    const SkeletonCache::Entry* skel =
        (e && e->type == kModel) ? self->skeletons_.Get(e->source) : nullptr;
    if (!skel || joint < 0 || size_t(joint) >= skel->bones.size()) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, skel->bones[size_t(joint)].name.c_str());
    return 1;
}

// MDL.TransformPointByJoint(e, joint, x,y,z) -> a point carried by a bone,
// plus that bone's rotation: x,y,z,rw,rx,ry,rz. It is how a muzzle flash sits
// at the barrel and how anything else rides a skeleton.
//
// The point arrives in the BONE's own space, which is why the scripts treat
// TransformPointByJoint(e, j, 0,0,0) and GetJointPos(e, j) as the same
// question - and they say so, in a comment at CActor's own call site.
//
// It has to return all seven values whatever happens. Returning nothing is
// what the stub did, and once the animation clock let weapons reach this at
// all, the nils flowed into Vector:New and threw an error that aborted
// Game_Tick entirely - every frame a weapon fired.
int ScriptEngine::L_MDL_TransformPointByJoint(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int joint = int(lua_tonumber(L, 2));
    const float local[3] = {float(lua_tonumber(L, 3)), float(lua_tonumber(L, 4)),
                            float(lua_tonumber(L, 5))};

    float world[3];
    if (!e || !self->JointToWorld(*e, joint, local, world))
        for (int c = 0; c < 3; ++c) world[c] = e ? e->pos[c] : 0.f;
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, world[c]);

    // The bone's orientation, composed with the entity's own so the result is
    // a world rotation - which is what the callers hand to ENTITY.SetRotation.
    float quat[4] = {1, 0, 0, 0};
    const std::vector<Mat4>* bones = e ? self->PosedBones(*e) : nullptr;
    if (bones && joint >= 0 && size_t(joint) < bones->size()) {
        const Mat4& m = (*bones)[size_t(joint)];
        float rot[9];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) rot[r * 3 + c] = m.m[r * 4 + c];
        Normalize3x3Rows(rot);
        float boneQuat[4];
        EngineRot9ToQuat(rot, boneQuat);
        EngineQuatMul(e->rotWXYZ, boneQuat, quat);
    }
    for (int c = 0; c < 4; ++c) lua_pushnumber(L, quat[c]);
    return 7;
}

// MDL.GetJointPos(e, joint) -> where a bone is, in world space. The bone's
// own origin, which is TransformPointByJoint with a zero point.
int ScriptEngine::L_MDL_GetJointPos(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int joint = int(lua_tonumber(L, 2));
    const float origin[3] = {0, 0, 0};

    float world[3];
    if (!e || !self->JointToWorld(*e, joint, origin, world))
        for (int c = 0; c < 3; ++c) world[c] = e ? e->pos[c] : 0.f;
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, world[c]);
    return 3;
}

// MDL.ApplyJointRotation(e, joint, ax, ay, az) - turns one bone on top of the
// animation. Radians: the shipped gun clamps its barrel to math.pi/3 before
// passing it here.
//
// This SETS the bone's rotation rather than accumulating, because every
// shipped caller recomputes an absolute angle each tick and passes it again -
// a turret's _barrelPitch, an actor's head angle toward the player. Made
// additive, a turret would wind up and spin.
int ScriptEngine::L_MDL_ApplyJointRotation(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int joint = int(lua_tonumber(L, 2));
    if (!e || joint < 0) return 0;

    const float euler[3] = {float(lua_tonumber(L, 3)), float(lua_tonumber(L, 4)),
                            float(lua_tonumber(L, 5))};
    for (JointOverride& o : e->jointRot) {
        if (o.bone != joint) continue;
        if (o.euler[0] == euler[0] && o.euler[1] == euler[1] && o.euler[2] == euler[2])
            return 0;                     // unchanged: leave the cached pose alone
        for (int c = 0; c < 3; ++c) o.euler[c] = euler[c];
        ++e->jointRotVersion;
        return 0;
    }

    JointOverride add;
    add.bone = joint;
    for (int c = 0; c < 3; ++c) add.euler[c] = euler[c];
    e->jointRot.push_back(add);
    ++e->jointRotVersion;
    return 0;
}

// MDL.GetJointRotation(e, joint) -> the bone's world orientation, engine
// quaternion order (w,x,y,z).
// MDL.GetVelocitiesFromJoint(e, joint) -> vx,vy,vz,vl, ax,ay,az,al: a ragdoll
// joint's linear and angular velocity, each with its magnitude.
//
// Ragdoll is its own system and has not been built (Docs/Animation.md stage
// 4), so a joint that no ragdoll is driving has no velocity - and zero is the
// true answer for that, not a placeholder standing in for one. The scripts
// read it to decide whether a hanging chain should creak; nothing is swinging,
// so nothing creaks.
//
// It has to return all eight numbers regardless. Returning nothing is what the
// stub did, and CItem compares the fourth against a threshold the moment an
// object declares a RagdollCreakSound - so `nil > number` aborted Game_Tick on
// every pass, 388 times in a 400-frame run of the Prison, and in twelve other
// levels besides. The guard above it only prints when the joint is missing; it
// does not stop the timer.
int ScriptEngine::L_MDL_GetVelocitiesFromJoint(lua_State* L) {
    for (int i = 0; i < 8; ++i) lua_pushnumber(L, 0);
    return 8;
}

int ScriptEngine::L_MDL_GetJointRotation(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int joint = int(lua_tonumber(L, 2));

    float quat[4] = {1, 0, 0, 0};
    const std::vector<Mat4>* bones = e ? self->PosedBones(*e) : nullptr;
    if (bones && joint >= 0 && size_t(joint) < bones->size()) {
        const Mat4& m = (*bones)[size_t(joint)];
        float rot[9];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) rot[r * 3 + c] = m.m[r * 4 + c];
        Normalize3x3Rows(rot);
        float boneQuat[4];
        EngineRot9ToQuat(rot, boneQuat);
        EngineQuatMul(e->rotWXYZ, boneQuat, quat);
    }
    for (int c = 0; c < 4; ++c) lua_pushnumber(L, quat[c]);
    return 4;
}

void ScriptEngine::TickAnimations(float dt) {
    if (dt <= 0.f) return;
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (e.animIndex < 0 || e.animScale <= 0.f) continue;   // none, or paused
        const float length = e.animSlots[size_t(e.animIndex)].length;
        e.animTime += dt * e.animScale;
        if (length <= 0.f) {
            e.animTime = 0.f;
        } else if (e.animLoop) {
            while (e.animTime >= length) e.animTime -= length;
        } else if (e.animTime > length) {
            // A one-shot holds on its last frame. It must not wrap: the event
            // loop walks forward through the declared times and would fire
            // the whole list again on every pass.
            e.animTime = length;
        }

        // Run the cross-fade down. It is spent in real seconds, not animation
        // time, so a blend lasts as long as the template says whatever speed
        // the outgoing animation was playing at.
        if (e.blendLeft > 0.f) {
            e.blendLeft -= dt;
            if (e.blendLeft <= 0.f) {
                e.blendLeft = 0.f;
                e.blendFrom = nullptr;
                e.blendFromTracks.clear();
            }
        }

        // Hand the pose to the renderer. Headless runs have none attached,
        // which is why the clock is useful on its own.
        //
        // The bones are posed HERE rather than in the renderer so that a joint
        // query and the drawn mesh cannot disagree. It costs one pass over the
        // skeleton per animated entity - about forty of them in a level, sixty
        // bones each - while the expensive half, deforming the vertices, stays
        // behind the renderer's frustum test where an actor across the map
        // still costs nothing.
        if (renderer_ && e.rendererInstance >= 0) {
            const std::vector<Mat4>* boneWorld = PosedBones(e);
            const SkeletonCache::Entry* skel =
                boneWorld ? skeletons_.Get(e.source) : nullptr;
            if (skel) {
                BoneWorldToSkinning(skel->inverseBind, *boneWorld, skinScratch_);
                renderer_->SetScriptSkinning(e.rendererInstance, skinScratch_.data(),
                                             skinScratch_.size());
            }
        }
    }
}

// ---------------------------------------------------------------- WORLD

// WORLD.AddEntity(handle, hidden) - enters the entity into the drawn world;
// CActor passes `not self.Visible` as the second argument.
int ScriptEngine::L_WORLD_AddEntity(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        e->inWorld = true;
        if (lua_isboolean(L, 2)) e->visible = !lua_toboolean(L, 2);
        self->SyncPose(*e);
    }
    return 0;
}

// WORLD.FindEntityByName(name) - resolves a world-mesh object (MapEntities
// bind EMesh scripts to named .mpk objects). Handed out as a pseudo-entity;
// the mesh-level natives that act on it are still stubs.
int ScriptEngine::L_WORLD_FindEntityByName(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, "");
    for (const auto& kv : self->entities_) {
        if (kv.second.worldObject && kv.second.name == name) {
            lua_pushnumber(L, kv.first);
            return 1;
        }
    }
    Entity e;
    e.type = kMesh;
    e.name = name;
    e.worldObject = true;
    e.inWorld = true;
    const int handle = self->nextHandle_++;
    self->entities_.emplace(handle, e);
    lua_pushnumber(L, handle);
    return 1;
}

// WORLD.LoadMap(mapPath, levelName, scale, overbright, rtCubeMap,
// shadowMapSize, shadowMapCount) - recorded; the game loop owns the actual
// renderer upload.
int ScriptEngine::L_WORLD_LoadMap(lua_State* L) {
    ScriptEngine* self = From(L);
    self->world_.mapPath = luaL_optstring(L, 1, "");
    self->world_.levelName = luaL_optstring(L, 2, "");
    self->world_.scale = float(luaL_optnumber(L, 3, 1.0));
    self->world_.overbright = lua_toboolean(L, 4) != 0;
    // The empty "NoName" level passes "../Data/Maps/" with no file - a level
    // without a world, not an error.
    self->world_.loadRequested =
        !self->world_.mapPath.empty() && self->world_.mapPath.back() != '/';

    // With physics attached the static world is built HERE, synchronously:
    // the entity bodies follow through PO_Create later in this same level
    // load, and they need something to rest on.
    self->mapLoaded_ = false;
    if (self->physics_ && self->world_.loadRequested) {
        const std::string path = self->host_->ResolvePath(self->world_.mapPath);
        // Load reports success as "no error recorded", so the reused mesh
        // must start clean or a previous failure poisons this one.
        self->map_ = MapMesh();
        if (MapMesh::Load(path, self->map_)) {
            self->mapLoaded_ = true;
            self->physics_->LoadWorldMesh(self->map_, self->world_.scale,
                                          self->dataRoot_);
        } else {
            LogWarn("WORLD.LoadMap: %s failed: %s", path.c_str(),
                    self->map_.error.c_str());
        }
    }
    return 0;
}

// WORLD.Init(activeMeshesMassScale, defaultMeshFriction,
// defaultMeshRestitution, deactivatorDelay, deactivatorMaxPosDiff) - CLevel
// calls it right after LoadMap. The deactivator pair maps onto Jolt's own
// sleep thresholds, which are close enough to leave alone for now.
int ScriptEngine::L_WORLD_Init(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->physics_)
        self->physics_->SetWorldSurface(float(luaL_optnumber(L, 1, 1.0)),
                                        float(luaL_optnumber(L, 2, 0.5)),
                                        float(luaL_optnumber(L, 3, 0.5)));
    return 0;
}

// WORLD.SetupFog(mode [, start, end, density, packedColor]). CLevel scales
// start/end by the user's clip-plane setting before the call, so the values
// arrive ready to use.
int ScriptEngine::L_WORLD_SetupFog(lua_State* L) {
    ScriptEngine* self = From(L);
    WorldState& w = self->world_;
    w.fogMode = int(luaL_optnumber(L, 1, 0));
    w.fogStart = float(luaL_optnumber(L, 2, 0));
    w.fogEnd = float(luaL_optnumber(L, 3, 90));
    w.fogDensity = float(luaL_optnumber(L, 4, 0));
    const uint32_t c = uint32_t(int64_t(luaL_optnumber(L, 5, 0)));
    w.fogColor[0] = float((c >> 16) & 0xFF);
    w.fogColor[1] = float((c >> 8) & 0xFF);
    w.fogColor[2] = float(c & 0xFF);
    return 0;
}

int ScriptEngine::L_WORLD_SetFarClipDist(lua_State* L) {
    From(L)->world_.farClip = float(luaL_optnumber(L, 1, 1024));
    return 0;
}

// WORLD.AmbientColor(r, g, b, gunAmbientMultiplier), components 0-255.
int ScriptEngine::L_WORLD_AmbientColor(lua_State* L) {
    ScriptEngine* self = From(L);
    for (int i = 0; i < 3; ++i)
        self->world_.ambient[i] = float(luaL_optnumber(L, 1 + i, 128));
    return 0;
}

// WORLD.LoadSky("../Data/Maps/<dome>") -> layer count, read out of the dome
// mesh itself: objects name their layer ("layer01shape",
// "_trans_layer03shape"), and the count is how many carry one. An empty path
// (Cfg.RenderSky < 2) or an unreadable mesh returns 0, which sends
// CLevel:ReloadSky down the low-quality path - the same fallback the
// original uses for DX7-class hardware.
int ScriptEngine::L_WORLD_LoadSky(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string mapPath = luaL_optstring(L, 1, "");
    self->world_.skyDomeMap.clear();
    self->world_.skyLayerCount = 0;

    if (!mapPath.empty() && mapPath.back() != '/') {
        MapMesh dome;
        if (MapMesh::Load(self->host_->ResolvePath(mapPath), dome)) {
            int count = 0;
            for (const MapObject& o : dome.objects) {
                std::string low = o.name;
                for (char& c : low)
                    c = char(std::tolower(static_cast<unsigned char>(c)));
                if (low.find("layer") != std::string::npos) ++count;
            }
            if (count > 4) count = 4;
            if (count > 0) {
                const size_t slash = mapPath.find_last_of("/\\");
                self->world_.skyDomeMap =
                    slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
                self->world_.skyLayerCount = count;
            }
        }
    }
    lua_pushnumber(L, self->world_.skyLayerCount);
    return 1;
}

// WORLD.LoadLowQualitySky("../Data/Maps/<dome>", height, angle) -> layer
// count (one: the single-texture dome).
int ScriptEngine::L_WORLD_LoadLowQualitySky(lua_State* L) {
    ScriptEngine* self = From(L);
    const std::string mapPath = luaL_optstring(L, 1, "");
    self->world_.skyMap.clear();
    if (mapPath.empty() || mapPath.back() == '/' ||
        !FileSystem::Get().Exists(self->host_->ResolvePath(mapPath))) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const size_t slash = mapPath.find_last_of("/\\");
    self->world_.skyMap = slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
    self->world_.skyAngle = float(luaL_optnumber(L, 3, 0));
    lua_pushnumber(L, 1);
    return 1;
}

// WORLD.SetupSkyLayer(i, texMask, texLMap,
//     tex1, rot, panU, panV, tileU, tileV,
//     tex2, rot, panU, panV, tileU, tileV) - argument order straight from
// CLevel:ReloadSky. On the low-quality path (no layered dome) the only
// meaningful argument is tex1: the dome's single texture.
int ScriptEngine::L_WORLD_SetupSkyLayer(lua_State* L) {
    ScriptEngine* self = From(L);
    const int i = int(luaL_optnumber(L, 1, 0));
    if (self->world_.skyLayerCount == 0) {
        self->world_.skyTexture = luaL_optstring(L, 4, "");
        return 0;
    }
    if (i < 0 || i >= 4) return 0;
    SkyLayer& layer = self->world_.skyLayers[i];
    layer.mask = luaL_optstring(L, 2, "");
    layer.lightmap = luaL_optstring(L, 3, "");
    SkyTexture* tex[2] = {&layer.tex1, &layer.tex2};
    for (int t = 0; t < 2; ++t) {
        const int base = 4 + t * 6;
        tex[t]->name = luaL_optstring(L, base, "");
        tex[t]->rotSpeed = float(luaL_optnumber(L, base + 1, 0));
        tex[t]->panU = float(luaL_optnumber(L, base + 2, 0));
        tex[t]->panV = float(luaL_optnumber(L, base + 3, 0));
        tex[t]->tileU = float(luaL_optnumber(L, base + 4, 1));
        tex[t]->tileV = float(luaL_optnumber(L, base + 5, 1));
    }
    return 0;
}

int ScriptEngine::L_MESH_SetDefaultDetailMaps(lua_State* L) {
    ScriptEngine* self = From(L);
    self->world_.detailTex = luaL_optstring(L, 1, "");
    self->world_.detailTileU = float(luaL_optnumber(L, 2, 8.2));
    self->world_.detailTileV = float(luaL_optnumber(L, 3, 7.1));
    return 0;
}


// ------------------------------------------------------------- the 2D layer
//
// Everything the shipped game draws over the world - health, ammo, the tarot
// board, the loading screens, the menus - is drawn from Lua through these.
// Argument order, defaults and colour packing below are read out of
// Engine.dll rather than guessed; Docs/Hud.md records where each came from.

namespace {

// PainEngine packs colours as D3D ARGB. HUD.PrintXY builds one out of three
// script arguments with `((r | 0xffffff00) << 8 | g) << 8 | b`, which is
// 0xFF_RR_GG_BB; DrawQuadRGBA builds `((a << 8 | r) << 8 | g) << 8 | b`, the
// same layout with a real alpha. R3D.RGB / R3D.RGBA agree, so one unpack
// serves the lot.
//
// The alpha passed here is final. HUD.SetTransparency is NOT folded in: the
// original stores that byte and nothing in the draw path reads it. The
// scripts apply it themselves - Hud:QuadTrans reads it back with
// HUD.GetTransparency and passes it as the RGBA alpha - so multiplying it in
// again would square the fade and leave the whole interface nearly
// invisible.
uint32_t ArgbToAbgr(uint32_t argb) {
    const uint32_t a = (argb >> 24) & 0xFF;
    const uint32_t r = (argb >> 16) & 0xFF;
    const uint32_t g = (argb >> 8) & 0xFF;
    const uint32_t b = argb & 0xFF;
    // bgfx vertex colours are little-endian ABGR.
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// The sixteen colours `#0`..`#f` select, read straight out of the table at
// 0x103e6220: four four-step ramps - parchment, grey, blood, leather - which
// is the whole of Painkiller's interface palette.
const uint32_t kColorCodes[16] = {
    0xffffba7a, 0xffe6a161, 0xffcd8848, 0xff9b5616,
    0xffd1d1d1, 0xffb8b8b8, 0xff9f9f9f, 0xff6d6d6d,
    0xffd60017, 0xffbd0000, 0xffa40000, 0xff720000,
    0xff6c483a, 0xff532f21, 0xff3a1608, 0xff080000,
};

bool ColorCodeDigit(char c, int& out) {
    if (c >= '0' && c <= '9') { out = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { out = 10 + (c - 'a'); return true; }
    return false;
}

// `#` plus one hex digit is a colour marker, and HUD::GetTextWidth steps over
// both without measuring them. Anything else after a `#` is literal text.
std::string StripColorCodes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        int idx = 0;
        if (text[i] == '#' && i + 1 < text.size() && ColorCodeDigit(text[i + 1], idx)) {
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

// A material handle travels through Lua as light userdata, because that is
// what MATERIAL.Create returns in the original - a Texture* the scripts hold
// opaquely and hand back. Scripts also pass a literal 0 to mean "no texture",
// which arrives as a number and reads back as handle 0.
void PushMaterial(lua_State* L, int handle) {
    if (handle <= 0) {
        lua_pushnil(L);
        return;
    }
    lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<intptr_t>(handle)));
}

int ToMaterial(lua_State* L, int index) {
    if (lua_islightuserdata(L, index))
        return int(reinterpret_cast<intptr_t>(lua_touserdata(L, index)));
    return 0;
}

} // namespace

int ScriptEngine::HudFontPixels(int size) const {
    if (size <= 0) size = hudFontSize_;
    // round(size * (H/768 + W/1024) * 0.5), which is 1:1 at the 1024x768 the
    // interface was authored at.
    const float scale =
        (float(screenH_) / 768.f + float(screenW_) / 1024.f) * 0.5f;
    const int px = int(std::lround(double(size) * double(scale)));
    return px > 0 ? px : 1;
}

void ScriptEngine::HudResolveFont(const char* name, int size, std::string& outName,
                                  int& outPixels) const {
    // PrintXY calls SetFont(0) when the script names no font - slot 0, the
    // default, not whatever HUD.SetFont last selected. timesbd is the game's
    // own default: it is what all but one of the shipped SetFont calls ask
    // for, and the only face the HUD scripts print with.
    if (name && *name) {
        outName = name;
        outPixels = HudFontPixels(size);
    } else {
        outName = "timesbd";
        outPixels = HudFontPixels(size);
    }
}

// MATERIAL.Create(name, flags) -> a texture handle. The flags are the
// TextureFlags bitfield (NoLOD, NoMipMaps and friends); our cache decides
// sampling from the image itself, so they are read and ignored.
int ScriptEngine::L_MATERIAL_Create(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!self->hud_ || !self->hudTextures_ || !name || !*name) {
        lua_pushnil(L);
        return 1;
    }
    PushMaterial(L, self->hud_->CreateMaterial(name, *self->hudTextures_, ""));
    return 1;
}

int ScriptEngine::L_MATERIAL_Release(lua_State* L) {
    ScriptEngine* self = From(L);
    if (self->hud_) self->hud_->ReleaseMaterial(ToMaterial(L, 1));
    return 0;
}

// MATERIAL.Size(mat) -> width, height. Every HUD script lays itself out by
// asking an image how big it is, so this has to be the real size. The
// original answers -1, -1 for a null material rather than a plausible
// guess - a script that divides by it then produces something visibly wrong
// instead of something subtly wrong.
int ScriptEngine::L_MATERIAL_Size(lua_State* L) {
    ScriptEngine* self = From(L);
    int w = -1, h = -1;
    if (self->hud_) self->hud_->MaterialSize(ToMaterial(L, 1), w, h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

// HUD.PrintXY(x, y, text, font, r, g, b, size)
//
// The colour defaults are (0, 255, 0): text with no colour given is green,
// which is the shipped console's colour. A negative x centres the string
// horizontally and a negative y centres it vertically, both against the real
// screen size - that is how every banner in the game is positioned.
int ScriptEngine::L_HUD_PrintXY(lua_State* L) {
    ScriptEngine* self = From(L);
    const int rawX = int(luaL_optnumber(L, 1, 0));
    const int rawY = int(luaL_optnumber(L, 2, 0));
    const char* text = luaL_optstring(L, 3, nullptr);
    const char* font = luaL_optstring(L, 4, nullptr);
    const uint32_t r = uint32_t(int(luaL_optnumber(L, 5, 0))) & 0xFF;
    const uint32_t g = uint32_t(int(luaL_optnumber(L, 6, 255))) & 0xFF;
    const uint32_t b = uint32_t(int(luaL_optnumber(L, 7, 0))) & 0xFF;
    const int size = int(luaL_optnumber(L, 8, 0));
    if (!self->hud_ || !text) return 0;

    std::string fontName;
    int pixels = 0;
    self->HudResolveFont(font, size, fontName, pixels);

    float x = float(rawX), y = float(rawY);
    if (rawX < 0)
        x = std::floor((float(self->screenW_) -
                        self->hud_->TextWidth(fontName, pixels, StripColorCodes(text))) *
                       0.5f);
    if (rawY < 0)
        y = std::floor((float(self->screenH_) - self->hud_->TextHeight(fontName, pixels)) * 0.5f);

    // `#<hex digit>` switches colour mid-string and is not itself drawn, so a
    // run is emitted per colour and the pen carries across.
    const std::string s = text;
    uint32_t argb = 0xFF000000u | (r << 16) | (g << 8) | b;
    std::string run;
    for (size_t i = 0; i <= s.size(); ++i) {
        int idx = 0;
        const bool marker = i + 1 < s.size() && s[i] == '#' && ColorCodeDigit(s[i + 1], idx);
        if (i == s.size() || marker) {
            if (!run.empty()) {
                x += self->hud_->Text(fontName, pixels, x, y, run,
                                      ArgbToAbgr(argb));
                run.clear();
            }
            if (marker) {
                // The original only honours a marker when the running colour
                // has any RGB at all, so text explicitly drawn black stays
                // black through one.
                if ((argb & 0xFFFFFFu) != 0) argb = kColorCodes[idx];
                ++i;
            }
            continue;
        }
        run.push_back(s[i]);
    }
    return 0;
}

// HUD.DrawQuad(mat, x, y, w, h, color, u1, v1, u2, v2)
// The colour defaults to -1, which is 0xFFFFFFFF: opaque white, drawing the
// texture as it is. The UVs default to the whole image.
int ScriptEngine::L_HUD_DrawQuad(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const int mat = ToMaterial(L, 1);
    const float x = float(luaL_optnumber(L, 2, 0));
    const float y = float(luaL_optnumber(L, 3, 0));
    const float w = float(luaL_optnumber(L, 4, 0));
    const float h = float(luaL_optnumber(L, 5, 0));
    const uint32_t argb = uint32_t(int64_t(luaL_optnumber(L, 6, -1)));
    const float u1 = float(luaL_optnumber(L, 7, 0.0));
    const float v1 = float(luaL_optnumber(L, 8, 0.0));
    const float u2 = float(luaL_optnumber(L, 9, 1.0));
    const float v2 = float(luaL_optnumber(L, 10, 1.0));
    self->hud_->Quad(mat, x, y, w, h, ArgbToAbgr(argb), u1, v1, u2, v2);
    return 0;
}

// HUD.DrawQuadRGBA(mat, x, y, w, h, r, g, b, a, u1, v1, u2, v2)
// The UV defaults are 0.01 and 0.99, not 0 and 1: an inset that keeps the
// filter off the edge texels of an icon packed against its neighbours.
int ScriptEngine::L_HUD_DrawQuadRGBA(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const int mat = ToMaterial(L, 1);
    const float x = float(luaL_optnumber(L, 2, 0));
    const float y = float(luaL_optnumber(L, 3, 0));
    const float w = float(luaL_optnumber(L, 4, 0));
    const float h = float(luaL_optnumber(L, 5, 0));
    const uint32_t r = uint32_t(int(luaL_optnumber(L, 6, 255))) & 0xFF;
    const uint32_t g = uint32_t(int(luaL_optnumber(L, 7, 255))) & 0xFF;
    const uint32_t b = uint32_t(int(luaL_optnumber(L, 8, 255))) & 0xFF;
    const uint32_t a = uint32_t(int(luaL_optnumber(L, 9, 255))) & 0xFF;
    const float u1 = float(luaL_optnumber(L, 10, 0.01));
    const float v1 = float(luaL_optnumber(L, 11, 0.01));
    const float u2 = float(luaL_optnumber(L, 12, 0.99));
    const float v2 = float(luaL_optnumber(L, 13, 0.99));
    const uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
    self->hud_->Quad(mat, x, y, w, h, ArgbToAbgr(argb), u1, v1, u2, v2);
    return 0;
}

// HUD.DrawQuadRotated(mat, x, y, w, h, angle, pivotX, pivotY, r, g, b, a)
//
// The compass needle. The pivot is an absolute screen point, not an offset
// and not the quad's centre: Hud:QuadRot draws the arrow at one place and
// turns it about the dial's hub a few pixels away. The original rounds the
// pivot to whole pixels before using it.
int ScriptEngine::L_HUD_DrawQuadRotated(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const int mat = ToMaterial(L, 1);
    const float x = float(luaL_optnumber(L, 2, 0));
    const float y = float(luaL_optnumber(L, 3, 0));
    const float w = float(luaL_optnumber(L, 4, 0));
    const float h = float(luaL_optnumber(L, 5, 0));
    const float angle = float(luaL_optnumber(L, 6, 0.0));
    const float px = float(std::lround(luaL_optnumber(L, 7, 0.0)));
    const float py = float(std::lround(luaL_optnumber(L, 8, 0.0)));
    const uint32_t r = uint32_t(int(luaL_optnumber(L, 9, 255))) & 0xFF;
    const uint32_t g = uint32_t(int(luaL_optnumber(L, 10, 255))) & 0xFF;
    const uint32_t b = uint32_t(int(luaL_optnumber(L, 11, 255))) & 0xFF;
    const uint32_t a = uint32_t(int(luaL_optnumber(L, 12, 255))) & 0xFF;
    const uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
    self->hud_->QuadRotated(mat, x, y, w, h, angle, px, py, ArgbToAbgr(argb));
    return 0;
}

// HUD.DrawRect(x, y, w, h, color): an untextured filled rectangle.
int ScriptEngine::L_HUD_DrawRect(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    const float x = float(luaL_optnumber(L, 1, 0));
    const float y = float(luaL_optnumber(L, 2, 0));
    const float w = float(luaL_optnumber(L, 3, 0));
    const float h = float(luaL_optnumber(L, 4, 0));
    const uint32_t argb = uint32_t(int64_t(luaL_optnumber(L, 5, -1)));
    self->hud_->Quad(0, x, y, w, h, ArgbToAbgr(argb));
    return 0;
}

// HUD.DrawBorder(x, y, w, h), defaulting to the whole 1024x768 reference
// screen.
//
// This is not a line rectangle: HUD::DrawBorder at 0x1008b510 builds a
// MenuItemBorder - the carved stone frame - and renders it, which is why it
// takes no colour. Now that the menu owns that widget, the HUD borrows it, so
// a script drawing a frame gets the shipped art either way.
int ScriptEngine::L_HUD_DrawBorder(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->hud_) return 0;
    self->menu_.DrawFrame(float(luaL_optnumber(L, 1, 0)), float(luaL_optnumber(L, 2, 0)),
                          float(luaL_optnumber(L, 3, 1024)),
                          float(luaL_optnumber(L, 4, 768)));
    return 0;
}

int ScriptEngine::L_HUD_SetFont(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, "");
    const int size = int(luaL_optnumber(L, 2, 0));
    if (*name) self->hudFont_ = name;
    if (size > 0) self->hudFontSize_ = size;
    return 0;
}

// HUD.GetTextWidth(text) -> pixels, measured in the font HUD.SetFont chose.
// Colour markers are stepped over rather than measured, and a multi-line
// string measures as its widest line.
int ScriptEngine::L_HUD_GetTextWidth(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* text = luaL_optstring(L, 1, "");
    if (!self->hud_) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const int pixels = self->HudFontPixels(0);
    const std::string clean = StripColorCodes(text);
    float widest = 0.f;
    size_t start = 0;
    while (start <= clean.size()) {
        const size_t end = clean.find('\n', start);
        const std::string line =
            clean.substr(start, end == std::string::npos ? std::string::npos : end - start);
        widest = std::max(widest, self->hud_->TextWidth(self->hudFont_, pixels, line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    lua_pushnumber(L, double(int(widest)));
    return 1;
}

// HUD.GetTextHeight(text) -> (newlines + 1) * the font's line height.
int ScriptEngine::L_HUD_GetTextHeight(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* text = luaL_optstring(L, 1, "");
    if (!self->hud_) {
        lua_pushnumber(L, 0);
        return 1;
    }
    int lines = 1;
    for (const char* p = text; *p; ++p)
        if (*p == '\n') ++lines;
    const float line = self->hud_->TextHeight(self->hudFont_, self->HudFontPixels(0));
    lua_pushnumber(L, double(int(line) * lines));
    return 1;
}

// HUD.SetTransparency(percent) / GetTransparency() -> 0-255.
//
// The argument is a PERCENTAGE - it comes from the HUD Transparency slider in
// the options menu - and the original stores round(percent * 2.55) in a byte,
// defaulting to 100. Nothing in the draw path reads that byte; the scripts
// read it back themselves and pass it as an RGBA alpha, so the conversion is
// the whole of what this native does.
int ScriptEngine::L_HUD_SetTransparency(lua_State* L) {
    ScriptEngine* self = From(L);
    const long v = std::lround(luaL_optnumber(L, 1, 100) * 2.55);
    self->hudAlpha_ = int(v < 0 ? 0 : (v > 255 ? 255 : v));
    return 0;
}

int ScriptEngine::L_HUD_GetTransparency(lua_State* L) {
    lua_pushnumber(L, From(L)->hudAlpha_);
    return 1;
}

int ScriptEngine::L_HUD_StripColorInfo(lua_State* L) {
    const std::string out = StripColorCodes(luaL_optstring(L, 1, ""));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// HUD.ColorSubstr(text, n) -> the first n VISIBLE characters, carrying the
// colour markers along so the trimmed string still draws in its own colours.
// The typing effect on the loading screens is this called with a rising n.
int ScriptEngine::L_HUD_ColorSubstr(lua_State* L) {
    const std::string s = luaL_optstring(L, 1, "");
    const int want = int(luaL_optnumber(L, 2, 0));
    std::string out;
    int visible = 0;
    for (size_t i = 0; i < s.size() && visible < want; ++i) {
        int idx = 0;
        if (s[i] == '#' && i + 1 < s.size() && ColorCodeDigit(s[i + 1], idx)) {
            out.push_back(s[i]);
            out.push_back(s[i + 1]);
            ++i;
            continue;
        }
        out.push_back(s[i]);
        ++visible;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// R3D.ScreenSize() -> the real window, which every HUD script scales its
// layout from.
int ScriptEngine::L_R3D_ScreenSize(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_pushnumber(L, self->screenW_);
    lua_pushnumber(L, self->screenH_);
    return 2;
}

// R3D.GetFPS() -> frames per second. The HUD formats it with string.format
// '%d', so returning nothing is a script error rather than a missing number.
int ScriptEngine::L_R3D_GetFPS(lua_State* L) {
    ScriptEngine* self = From(L);
    const float dt = self->frameDelta_;
    lua_pushnumber(L, dt > 0.f ? double(int(1.f / dt + 0.5f)) : 0.0);
    return 1;
}

// --------------------------------------------------------------- the menu
//
// The scripts declare a screen and the engine owns it from there: layout,
// hit-testing, keyboard navigation and drawing are all on this side. Items are
// addressed by NAME, which is what Engine.dll's MenuScreen::FindItem does and
// why every setter below takes a name string first. See Docs/Menu.md.
//
// Stage 1: static text, text buttons, and the screen lifecycle. Everything
// else is still an instrumented stub, so the call report keeps counting what
// the shipped menus actually reach for.

namespace {

// Every SetItem* native is "find by name, write one field". A miss is not an
// error - the scripts configure items they have not added yet on screens that
// were never activated - so it returns quietly.
MenuSystem::Item* MenuItemArg(ScriptEngine* self, lua_State* L, MenuSystem** outMenu);

} // namespace

int ScriptEngine::L_PMENU_Activate(lua_State* L) {
    ScriptEngine* self = From(L);
    // The argument is "activate", and PainMenu passes false to LEAVE the menu.
    // lua_isnoneornil, not lua_isnil: an ABSENT argument is LUA_TNONE, and
    // lua_isnil only catches an explicit nil. PMENU.ShowMouse() is called with
    // no argument at all, and reading that as "false" is what left the menu
    // with no cursor and the mouse still steering the player.
    const bool on = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    self->menu_.Activate(on);
    return 0;
}

int ScriptEngine::L_PMENU_Active(lua_State* L) {
    lua_pushboolean(L, From(L)->menu_.active() ? 1 : 0);
    return 1;
}

int ScriptEngine::L_PMENU_Clear(lua_State* L) {
    From(L)->menu_.Clear();
    return 0;
}

int ScriptEngine::L_PMENU_ClearScreen(lua_State* L) {
    From(L)->menu_.ClearScreen();
    return 0;
}

// PMENU.SetBackground(material, type). The type selects how the artwork is
// fitted; we stretch to the window either way, because a menu background is
// artwork rather than a layout element.
int ScriptEngine::L_PMENU_SetBackground(lua_State* L) {
    From(L)->menu_.SetBackground(luaL_optstring(L, 1, ""), int(luaL_optnumber(L, 2, 0)));
    return 0;
}

int ScriptEngine::L_PMENU_SetMenuWidth(lua_State* L) {
    From(L)->menu_.SetMenuWidth(float(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_PMENU_SetTopPosition(lua_State* L) {
    From(L)->menu_.SetTopPosition(float(luaL_optnumber(L, 1, 0)));
    return 0;
}

int ScriptEngine::L_PMENU_ShowMouse(lua_State* L) {
    // ShowMouse() with no argument means SHOW - see L_PMENU_Activate.
    From(L)->menu_.ShowMouse(lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0));
    return 0;
}

// PMENU.ShowMenu() / PMENU.ReturnToGame() - the same transition Escape makes,
// exposed because the scripts drive it too: a dropped multiplayer connection
// or a bad CD key forces the menu up from Lua.
int ScriptEngine::L_PMENU_ShowMenu(lua_State* L) {
    From(L)->menu_.Open();
    return 0;
}

int ScriptEngine::L_PMENU_ReturnToGame(lua_State* L) {
    From(L)->menu_.Close();
    return 0;
}

// WORLD.SetGamePaused(bool) / IsGamePaused(). Engine.dll keeps this as a byte
// on the World object; no shipped script ever SETS it, which is what says the
// engine owns the pause - the scripts only ask (PainKiller.lua guards its
// tick on it). The menu sets it on the way in and clears it on the way out.
int ScriptEngine::L_WORLD_SetGamePaused(lua_State* L) {
    From(L)->gamePaused_ = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    return 0;
}

int ScriptEngine::L_WORLD_IsGamePaused(lua_State* L) {
    lua_pushboolean(L, From(L)->gamePaused_ ? 1 : 0);
    return 1;
}

// PMENU.AddStaticText(name, text) and AddTextButton(name, text, desc).
//
// The third argument of AddTextButton is the DESCRIPTION, not the action -
// PainMenu:SetupScreen passes o.desc there and sets the action separately with
// SetItemAction. (Engine.dll's own AddTextButton takes three strings; which of
// them is which is settled by the call site, not by the decompile.)
int ScriptEngine::L_PMENU_AddStaticText(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::StaticText);
    item.text = luaL_optstring(L, 2, "");
    return 0;
}

int ScriptEngine::L_PMENU_AddTextButton(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TextButton);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    return 0;
}

int ScriptEngine::L_PMENU_SetItemText(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->text = luaL_optstring(L, 2, "");
    return 0;
}

int ScriptEngine::L_PMENU_SetItemDesc(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->desc = luaL_optstring(L, 2, "");
    return 0;
}

// The action is a string of LUA SOURCE, run when the item is chosen:
//   action = "PainMenu:ActivateScreen(GameMenu)"
int ScriptEngine::L_PMENU_SetItemAction(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->action = luaL_optstring(L, 2, "");
    return 0;
}

int ScriptEngine::L_PMENU_SetItemPosition(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->x = float(luaL_optnumber(L, 2, -1));
        item->y = float(luaL_optnumber(L, 3, 0));
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemColors(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->textColor      = uint32_t(int64_t(luaL_optnumber(L, 2, 0xFF646464u)));
        item->disabledColor  = uint32_t(int64_t(luaL_optnumber(L, 3, 0xFF9B9B9Bu)));
        item->underMouseColor= uint32_t(int64_t(luaL_optnumber(L, 4, 0xFFFFFFFFu)));
        item->descColor      = uint32_t(int64_t(luaL_optnumber(L, 5, 0xFFFFFFFFu)));
    }
    return 0;
}

// PMENU.SetItemFontsTex(name, bigTex, smallTex) - the texture the glyphs are
// filled with, not another font. See MenuSystem::Item::fontBigTex.
int ScriptEngine::L_PMENU_SetItemFontsTex(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        const std::string big = luaL_optstring(L, 2, "");
        const std::string small = luaL_optstring(L, 3, "");
        if (big != item->fontBigTex) { item->fontBigTex = big; item->fontBigTexMat = -1; }
        if (small != item->fontSmallTex) { item->fontSmallTex = small; item->fontSmallTexMat = -1; }
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemFonts(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->fontBig      = luaL_optstring(L, 2, "timesbd");
        item->fontBigSize  = int(luaL_optnumber(L, 3, 26));
        item->fontSmall    = luaL_optstring(L, 4, "timesbd");
        item->fontSmallSize= int(luaL_optnumber(L, 5, 22));
        if (item->fontBig.empty())   item->fontBig = "timesbd";
        if (item->fontSmall.empty()) item->fontSmall = "timesbd";
        if (item->fontBigSize   <= 0) item->fontBigSize = 26;
        if (item->fontSmallSize <= 0) item->fontSmallSize = 22;
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemVisibility(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->visible = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    return 0;
}

int ScriptEngine::L_PMENU_SetItemAlign(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->align = int(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PMENU_SetItemWidth(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->width = float(luaL_optnumber(L, 2, 0));
    return 0;
}

// PMENU.SetItemSounds(name, accept, lightOn). The call site settles the order:
// PainMenu passes o.sndAccept then o.sndLightOn, so the FOCUS sound is the
// third argument, not the second. Only that one is used yet.
// PMENU.EnableItemBG(name, "blaszka") - turn on the plate behind a row. The
// second argument is the BASE name of a three-slice under HUD/blachy_menu.
int ScriptEngine::L_PMENU_EnableItemBG(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->itemBG = luaL_optstring(L, 2, "");
        item->itemBGMat[0] = item->itemBGMat[1] = item->itemBGMat[2] = -1;
    }
    return 0;
}

int ScriptEngine::L_PMENU_SetItemSounds(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->sndLightOn = luaL_optstring(L, 3, "");
    return 0;
}

int ScriptEngine::L_PMENU_DisableItem(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) item->disabled = true;
    return 0;
}

int ScriptEngine::L_PMENU_EnableItem(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) item->disabled = false;
    return 0;
}

namespace {

MenuSystem::Item* MenuItemArg(ScriptEngine* self, lua_State* L, MenuSystem** outMenu) {
    *outMenu = &self->menu();
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return nullptr;
    return (*outMenu)->Find(name);
}

} // namespace

// MOUSE.GetPos() -> the absolute cursor in window pixels, which is what the
// menu hit-tests against. The bare-host stub answers 0,0; this answers where
// the pointer actually is.
int ScriptEngine::L_MOUSE_GetPos(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->input_) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 2;
    }
    lua_pushnumber(L, self->input_->mouseX());
    lua_pushnumber(L, self->input_->mouseY());
    return 2;
}


// SOUND.ApplySoundSettings(master, music, sfx, speakers, pan, reverse, provider)
//
// Game.lua:222 calls this at startup with the values out of config.ini, which
// is how a player's saved volume reaches the mixer. Without it every sound
// plays at full gain no matter what the options say - and the shipped config
// has MasterVolume at 10, so "ignored" is a factor of ten too loud.
//
// The originals are percentages. Engine.dll multiplies argument 1 by 0.01 into
// MilesEngine::SetMasterVolumeLevel and argument THREE by 0.01 into
// Set3DDigitalEffectsVolume; argument 2 (music) is not used here, because the
// streams carry their own volume through SOUND.StreamSetVolume.
//
// Miles has two buses and we have one, so the two are composed: master scales
// everything and sfx scales the 3D effects, and effects are very nearly all we
// play. A separate music bus is worth splitting out when streaming lands.
int ScriptEngine::L_SOUND_ApplySoundSettings(lua_State* L) {
    ScriptEngine* self = From(L);
    const double master = luaL_optnumber(L, 1, 100.0) * 0.01;
    const double sfx    = luaL_optnumber(L, 3, 100.0) * 0.01;
    const double gain = std::max(0.0, std::min(1.0, master)) *
                        std::max(0.0, std::min(1.0, sfx));
    if (self->audio_) self->audio_->SetMasterVolume(float(gain));
    LogInfo("audio: master %.0f%%, sfx %.0f%% -> gain %.2f", master * 100.0, sfx * 100.0,
            gain);
    return 0;
}

int ScriptEngine::L_SOUND_SetMasterVolume(lua_State* L) {
    ScriptEngine* self = From(L);
    const double v = luaL_optnumber(L, 1, 100.0) * 0.01;
    if (self->audio_) self->audio_->SetMasterVolume(float(std::max(0.0, std::min(1.0, v))));
    return 0;
}


// --- stage 2: the widgets that carry a value -------------------------------
//
// The Options screens are almost entirely these. Each declares `option =
// "MasterVolume"`, PainMenu:AddItem seeds it from Cfg[option], and
// PainMenu:ApplySettings reads it back through the accessors below and writes
// Cfg. So getting the accessors right is what makes the settings round-trip.

// PMENU.AddCheckbox(name, text, desc, value)
int ScriptEngine::L_PMENU_AddCheckbox(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Checkbox);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    // The script seeds this from Cfg, where a flag is a real Lua boolean.
    item.value = (lua_isboolean(L, 4) ? lua_toboolean(L, 4) != 0
                                      : luaL_optnumber(L, 4, 0) != 0)
                     ? 1.0 : 0.0;
    return 0;
}

// PMENU.AddSlider(name, text, desc, min, max, isFloat, value, width, ctrlWidth)
//
// PainMenu multiplies a float slider's bounds AND value by 100 before calling
// this, then divides on the way back out, so what arrives here is always in
// the same units whichever kind it is.
int ScriptEngine::L_PMENU_AddSlider(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Slider);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.minValue = luaL_optnumber(L, 4, 0);
    item.maxValue = luaL_optnumber(L, 5, 100);
    item.isFloat = lua_isboolean(L, 6) ? lua_toboolean(L, 6) != 0
                                       : luaL_optnumber(L, 6, 0) != 0;
    item.value = luaL_optnumber(L, 7, item.minValue);
    if (const double w = luaL_optnumber(L, 8, 0); w > 0) item.sliderWidth = float(w);
    return 0;
}

// PMENU.AddNumRange(name, text, desc, min, max, value). A maximum of -1 means
// unbounded, which is how the scripts spell "no frag limit".
int ScriptEngine::L_PMENU_AddNumRange(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::NumRange);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.minValue = luaL_optnumber(L, 4, 0);
    item.maxValue = luaL_optnumber(L, 5, -1);
    item.value = luaL_optnumber(L, 6, item.minValue);
    return 0;
}

// PMENU.AddTextButtonEx(name, text, desc, valueLabel)
//
// The row whose value is one of a list - resolution, texture quality, speaker
// setup. The ENGINE does not hold the list: the script keeps it, and every
// change runs the item's action, which calls ChangeTextButtonExValue with the
// next label. So this stores a caption and nothing more.
int ScriptEngine::L_PMENU_AddTextButtonEx(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TextButtonEx);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.valueText = luaL_optstring(L, 4, "");
    return 0;
}

int ScriptEngine::L_PMENU_ChangeTextButtonExValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->valueText = luaL_optstring(L, 2, "");
    return 0;
}

// PMENU.AddTextEdit(name, text, desc, maxLength, value), and AddNumEdit which
// is the same field restricted to digits.
int ScriptEngine::L_PMENU_AddTextEdit(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::TextEdit);
    item.text = luaL_optstring(L, 2, "");
    item.desc = luaL_optstring(L, 3, "");
    item.maxLength = size_t(luaL_optnumber(L, 4, 0));
    item.valueText = luaL_optstring(L, 5, "");
    return 0;
}

// --- reading the values back ----------------------------------------------

int ScriptEngine::L_PMENU_GetSliderValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushnumber(L, item ? item->value : 0.0);
    return 1;
}

int ScriptEngine::L_PMENU_IsSliderFloat(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushboolean(L, (item && item->isFloat) ? 1 : 0);
    return 1;
}

int ScriptEngine::L_PMENU_GetNumRangeValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushnumber(L, item ? item->value : 0.0);
    return 1;
}

// Returns a BOOLEAN: PainMenu:ApplyCheckbox assigns it straight into Cfg,
// where the shipped config.ini writes true/false rather than 1/0.
int ScriptEngine::L_PMENU_IsItemChecked(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    lua_pushboolean(L, (item && item->value != 0.0) ? 1 : 0);
    return 1;
}

int ScriptEngine::L_PMENU_SetCheckboxValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->value = (lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0
                                           : luaL_optnumber(L, 2, 0) != 0)
                          ? 1.0 : 0.0;
    return 0;
}

int ScriptEngine::L_PMENU_GetTextEditValue(lua_State* L) {
    MenuSystem* menu = nullptr;
    const MenuSystem::Item* item = MenuItemArg(From(L), L, &menu);
    const std::string& s = item ? item->valueText : std::string();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}


// --- stage 3: the frame -----------------------------------------------------

// PMENU.AddBorder(name, dark), then SetBorderSize / SetBorderHeader /
// SetBorderColCount / SetBorderColumn configure it. The Options screens open
// with one of these and lay their rows out inside it.
int ScriptEngine::L_PMENU_AddBorder(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Border);
    item.dark = lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0
                                    : luaL_optnumber(L, 2, 0) != 0;
    return 0;
}

// PMENU.AddTabGroup(name, dark). A framed container whose children the script
// shows and hides wholesale - PainMenu:ShowTabGroup just calls
// SetItemVisibility down the group's item list. It takes SetBorderSize like a
// border does, so it IS one as far as drawing goes; what makes it a group is
// entirely on the script side.
int ScriptEngine::L_PMENU_AddTabGroup(lua_State* L) {
    ScriptEngine* self = From(L);
    const char* name = luaL_optstring(L, 1, nullptr);
    if (!name || !*name) return 0;
    MenuSystem::Item& item = self->menu_.Add(name, MenuSystem::Kind::Border);
    item.dark = lua_isboolean(L, 2) ? lua_toboolean(L, 2) != 0
                                    : luaL_optnumber(L, 2, 0) != 0;
    return 0;
}

int ScriptEngine::L_PMENU_SetBorderSize(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        item->width = float(luaL_optnumber(L, 2, 0));
        item->height = float(luaL_optnumber(L, 3, 0));
    }
    return 0;
}

// The dark band across the top of a panel, where a list puts its column
// captions. The argument is its height in authoring units.
int ScriptEngine::L_PMENU_SetBorderHeader(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu))
        item->headerHeight = float(luaL_optnumber(L, 2, 0));
    return 0;
}

int ScriptEngine::L_PMENU_SetBorderColCount(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        const int n = int(luaL_optnumber(L, 2, 0));
        item->columns.assign(size_t(n < 0 ? 0 : n), 0.f);
    }
    return 0;
}

// SetBorderColumn(name, index, width) - and the index is ZERO-based, which
// PainMenu:SetupScreen shows plainly where it configures FireBorder with
// columns 0 through 3.
int ScriptEngine::L_PMENU_SetBorderColumn(lua_State* L) {
    MenuSystem* menu = nullptr;
    if (MenuSystem::Item* item = MenuItemArg(From(L), L, &menu)) {
        const int index = int(luaL_optnumber(L, 2, -1));
        if (index >= 0 && size_t(index) < item->columns.size())
            item->columns[size_t(index)] = float(luaL_optnumber(L, 3, 0));
    }
    return 0;
}


// R3D.GetAvailableResolutions() -> an array of "WIDTHxHEIGHT" strings.
//
// PainMenu builds the Resolution row directly out of this and calls table.getn
// on it, so a missing native takes the whole VideoOptions screen down rather
// than degrading. The screen upper-cases each entry and compares against
// Cfg.Resolution, which the shipped config writes as "3440X1440" - so the
// separator has to be an 'x' and nothing else.
int ScriptEngine::L_R3D_GetAvailableResolutions(lua_State* L) {
    ScriptEngine* self = From(L);
    lua_newtable(L);
    int n = 0;
    for (const std::string& mode : self->resolutions_) {
        lua_pushnumber(L, ++n);
        lua_pushlstring(L, mode.data(), mode.size());
        lua_settable(L, -3);
    }
    // Never hand back an empty table: the screen indexes visible[currValue]
    // and would then draw a nil. The current window is always a valid mode.
    if (n == 0) {
        char buf[32];
        snprintf(buf, sizeof buf, "%dx%d", self->screenW_, self->screenH_);
        lua_pushnumber(L, 1);
        lua_pushstring(L, buf);
        lua_settable(L, -3);
    }
    return 1;
}

// ---------------------------------------------------------------- binding

void ScriptEngine::Bind(LuaHost& host) {
    host_ = &host;
    const struct {
        const char* module;
        const char* name;
        int (*fn)(lua_State*);
    } natives[] = {
        {"ENTITY", "Create", L_Create},
        {"ENTITY", "Release", L_Release},
        {"ENTITY", "SetPosition", L_SetPosition},
        {"ENTITY", "GetPosition", L_GetPosition},
        {"ENTITY", "SetRotationQ", L_SetRotationQ},
        {"ENTITY", "GetRotationQ", L_GetRotationQ},
        {"ENTITY", "SetOrientation", L_SetOrientation},
        {"ENTITY", "GetOrientation", L_GetOrientation},
        {"ENTITY", "SetScale", L_SetScale},
        {"ENTITY", "EnableDraw", L_EnableDraw},
        {"PARTICLE", "AddEmitter", L_PARTICLE_AddEmitter},
        {"PARTICLE", "SetupEmitter", L_PARTICLE_SetupEmitter},
        {"PARTICLE", "SetEvolve", L_PARTICLE_SetEvolve},
        {"MDL", "SetAnim", L_MDL_SetAnim},
        {"MDL", "GetAnimLength", L_MDL_GetAnimLength},
        {"MDL", "GetAnimTime", L_MDL_GetAnimTime},
        {"MDL", "SetAnimTime", L_MDL_SetAnimTime},
        {"MDL", "GetAnimTimeScale", L_MDL_GetAnimTimeScale},
        {"MDL", "SetAnimTimeScale", L_MDL_SetAnimTimeScale},
        {"MDL", "ResetFrame", L_MDL_ResetFrame},
        {"MDL", "LoadAnim", L_MDL_LoadAnim},
        {"MDL", "GetAnimMovement", L_MDL_GetAnimMovement},
        {"MDL", "TransformPointByJoint", L_MDL_TransformPointByJoint},
        {"MDL", "GetJointPos", L_MDL_GetJointPos},
        {"MDL", "GetJointIndex", L_MDL_GetJointIndex},
        {"MDL", "GetJointName", L_MDL_GetJointName},
        {"MDL", "GetJointRotation", L_MDL_GetJointRotation},
        {"MDL", "ApplyJointRotation", L_MDL_ApplyJointRotation},
        {"MDL", "GetVelocitiesFromJoint", L_MDL_GetVelocitiesFromJoint},
        {"PARTICLE", "SetFixedTransform", L_NoOpNative},
        {"BILLBOARD", "SetupCorona", L_BILLBOARD_SetupCorona},
        {"ENTITY", "GetVelocity", L_GetVelocity},
        {"ENTITY", "SetVelocity", L_SetVelocity},
        {"ENTITY", "SetTimeToDie", L_SetTimeToDie},
        {"ENTITY", "PO_Hit", L_PO_Hit},
        {"WORLD", "HitPhysicObject", L_WORLD_HitPhysicObject},
        {"ENTITY", "PO_Create", L_PO_Create},
        {"ENTITY", "PO_Move", L_PO_Move},
        {"ENTITY", "PO_SetMonsterType", L_PO_SetMonsterType},
        {"ENTITY", "PO_SetMonsterMovementConst", L_PO_SetMonsterMovementConst},
        {"ENTITY", "PO_IsOnFloor", L_PO_IsOnFloor},
        {"ENTITY", "PO_SetSightParams", L_PO_SetSightParams},
        {"ENTITY", "SeesEntity", L_SeesEntity},
        {"SOUND", "ApplySoundSettings", L_SOUND_ApplySoundSettings},
        {"SOUND", "SetMasterVolume", L_SOUND_SetMasterVolume},
        {"SOUND", "Play2D", L_SOUND_Play2D},
        {"SOUND", "Play3D", L_SOUND_Play3D},
        {"SOUND", "SetPlayerPos", L_SOUND_SetPlayerPos},
        {"SOUND", "SetPlayerOrientation", L_SOUND_SetPlayerOrientation},
        {"SOUND2D", "Create", L_SND_Create2D},
        {"SOUND2D", "Play", L_SND_Play},
        {"SOUND2D", "Stop", L_SND_Stop},
        {"SOUND2D", "Pause", L_SND_Pause},
        {"SOUND2D", "IsPlaying", L_SND_IsPlaying},
        {"SOUND2D", "SetVolume", L_SND_SetVolume},
        {"SOUND2D", "SetLoopCount", L_SND_SetLoopCount},
        {"SOUND2D", "SetSoundSpeed", L_SND_SetSoundSpeed},
        {"SOUND2D", "Delete", L_SND_Delete},
        {"SOUND2D", "Forget", L_SND_Forget},
        {"SOUND3D", "Create", L_SND_Create3D},
        {"SOUND3D", "Play", L_SND_Play},
        {"SOUND3D", "Stop", L_SND_Stop},
        {"SOUND3D", "IsPlaying", L_SND_IsPlaying},
        {"SOUND3D", "SetVolume", L_SND_SetVolume},
        {"SOUND3D", "SetLoopCount", L_SND_SetLoopCount},
        {"SOUND3D", "SetPosition", L_SND_SetPosition},
        {"SOUND3D", "SetHearingDistance", L_SND_SetHearingDistance},
        {"SOUND3D", "Delete", L_SND_Delete},
        {"SOUND3D", "Forget", L_SND_Forget},
        {"WPT", "Load", L_WPT_Load},
        {"PATH", "Create", L_PATH_Create},
        {"PATH", "Release", L_PATH_Release},
        {"PATH", "GetShortest", L_PATH_GetShortest},
        {"PATH", "IsFinished", L_PATH_IsFinished},
        {"PATH", "GetNextPoint", L_PATH_GetNextPoint},
        {"ENTITY", "PO_Exist", L_PO_Exist},
        {"ENTITY", "PO_GetMaxSphereRay", L_PO_GetMaxSphereRay},
        {"ENTITY", "PO_SetMass", L_PO_SetMass},
        {"ENTITY", "PO_SetFriction", L_PO_SetFriction},
        {"ENTITY", "PO_SetRestitution", L_PO_SetRestitution},
        {"ENTITY", "PO_SetLinearDamping", L_PO_SetLinearDamping},
        {"ENTITY", "PO_SetAngularDamping", L_PO_SetAngularDamping},
        {"ENTITY", "PO_SetPawnHeadPos", L_PO_SetPawnHeadPos},
        {"ENTITY", "PO_GetPawnHeadPos", L_PO_GetPawnHeadPos},
        {"ENTITY", "PO_GetPawnFloorPos", L_PO_GetPawnFloorPos},
        {"ENTITY", "PO_IsEnabled", L_PO_IsEnabled},
        {"ENTITY", "PO_Enable", L_PO_Enable},
        {"ENTITY", "GetDimensions", L_GetDimensions},
        {"ENTITY", "GetWorldPosition", L_GetPosition},
        {"ENTITY", "IsDrawEnabled", L_IsDrawEnabled},
        {"PLAYER", "GetDistanceFromPoint", L_PLAYER_GetDistanceFromPoint},
        {"REGION", "BuildFromPoint", L_REGION_BuildFromPoint},
        {"WORLD", "LineTrace", L_WORLD_LineTrace},
        {"WORLD", "LineTraceFixedGeom", L_WORLD_LineTraceFixedGeom},
        // The AI's shot test against the player. Ours is the same trace: the
        // player has no simulated body to hit yet, so it can only report the
        // world, which reads as "the shot was blocked".
        {"WORLD", "LineTraceHitPlayerBalls", L_WORLD_LineTrace},
        {"ENTITY", "AddToIntersectionSolver", L_AddToIntersectionSolver},
        {"ENTITY", "RemoveFromIntersectionSolver", L_RemoveFromIntersectionSolver},
        // An actor's ragdoll is the same body as the actor here, so the
        // ragdoll pair says the same thing as the plain pair.
        {"ENTITY", "AddRagdollToIntersectionSolver", L_AddToIntersectionSolver},
        {"ENTITY", "RemoveRagdollFromIntersectionSolver", L_RemoveFromIntersectionSolver},
        {"ENTITY", "IsFixedMesh", L_IsFixedMesh},
        {"ENTITY", "GetType", L_GetType},
        {"ENTITY", "SetPosAndRotRelativeToCamera", L_SetPosAndRotRelativeToCamera},
        {"MOUSE", "Lock", L_MOUSE_Lock},
        {"MOUSE", "IsLocked", L_MOUSE_IsLocked},
        {"CAM", "GetPos", L_CAM_GetPos},
        {"CAM", "SetPos", L_CAM_SetPos},
        {"CAM", "SetAng", L_CAM_SetAng},
        {"CAM", "GetForwardVector", L_CAM_GetForwardVector},
        {"CAM", "GetAng", L_CAM_GetAng},
        {"CAM", "GetAngRad", L_CAM_GetAngRad},
        {"CAM", "GetRawRotation", L_CAM_GetRawRotation},
        {"MOUSE", "GetDelta", L_MOUSE_GetDelta},
        {"MOUSE", "SetSensitivity", L_MOUSE_SetSensitivity},
        {"CAM", "SetPositionDisplacement", L_CAM_SetPositionDisplacement},
        {"INP", "GetActionStatus", L_INP_GetActionStatus},
        {"INP", "Action", L_INP_Action},
        {"INP", "UIAction", L_INP_UIAction},
        {"INP", "Key", L_INP_Key},
        {"INP", "IsFireSwitched", L_INP_IsFireSwitched},
        {"INP", "LoadBindings", L_INP_LoadBindings},
        {"INP", "Reset", L_INP_Reset},
        {"ENTITY", "PO_SetAction", L_PO_SetAction},
        {"ENTITY", "PO_AddAction", L_PO_AddAction},
        {"ENTITY", "PO_IsActionState", L_PO_IsActionState},
        {"ENTITY", "PO_JumpedInLastAction", L_PO_JumpedInLastAction},
        {"PLAYER", "ExecAction", L_PLAYER_ExecAction},
        {"PLAYER", "FloorCheck", L_PLAYER_FloorCheck},
        {"PLAYER", "GetCameraFix", L_PLAYER_GetCameraFix},
        {nullptr, "CreatePlayer", L_CreatePlayer},
        {nullptr, "GetPlayerSpeed", L_GetPlayerSpeed},
        {nullptr, "SetPlayerSpeed", L_SetPlayerSpeed},
        {"WORLD", "Init", L_WORLD_Init},
        {"WORLD", "AddEntity", L_WORLD_AddEntity},
        {"WORLD", "FindEntityByName", L_WORLD_FindEntityByName},
        {"WORLD", "LoadMap", L_WORLD_LoadMap},
        {"WORLD", "SetupFog", L_WORLD_SetupFog},
        {"WORLD", "SetFarClipDist", L_WORLD_SetFarClipDist},
        {"WORLD", "AmbientColor", L_WORLD_AmbientColor},
        {"WORLD", "LoadSky", L_WORLD_LoadSky},
        {"WORLD", "LoadLowQualitySky", L_WORLD_LoadLowQualitySky},
        {"WORLD", "SetupSkyLayer", L_WORLD_SetupSkyLayer},
        {"MESH", "SetDefaultDetailMaps", L_MESH_SetDefaultDetailMaps},
        {"MATERIAL", "Create", L_MATERIAL_Create},
        {"MATERIAL", "Release", L_MATERIAL_Release},
        {"MATERIAL", "Size", L_MATERIAL_Size},
        {"HUD", "PrintXY", L_HUD_PrintXY},
        {"HUD", "DrawQuad", L_HUD_DrawQuad},
        {"HUD", "DrawQuadRGBA", L_HUD_DrawQuadRGBA},
        {"HUD", "DrawQuadRotated", L_HUD_DrawQuadRotated},
        {"HUD", "DrawRect", L_HUD_DrawRect},
        {"HUD", "DrawBorder", L_HUD_DrawBorder},
        {"HUD", "SetFont", L_HUD_SetFont},
        {"HUD", "GetTextWidth", L_HUD_GetTextWidth},
        {"HUD", "GetTextHeight", L_HUD_GetTextHeight},
        {"HUD", "SetTransparency", L_HUD_SetTransparency},
        {"HUD", "GetTransparency", L_HUD_GetTransparency},
        {"HUD", "StripColorInfo", L_HUD_StripColorInfo},
        {"HUD", "ColorSubstr", L_HUD_ColorSubstr},
        {"R3D", "ScreenSize", L_R3D_ScreenSize},
        {"R3D", "GetFPS", L_R3D_GetFPS},
        {"R3D", "GetAvailableResolutions", L_R3D_GetAvailableResolutions},
        {"MOUSE", "GetPos", L_MOUSE_GetPos},
        {"PMENU", "Activate", L_PMENU_Activate},
        {"PMENU", "Active", L_PMENU_Active},
        {"PMENU", "Clear", L_PMENU_Clear},
        {"PMENU", "ClearScreen", L_PMENU_ClearScreen},
        {"PMENU", "SetBackground", L_PMENU_SetBackground},
        {"PMENU", "SetMenuWidth", L_PMENU_SetMenuWidth},
        {"PMENU", "SetTopPosition", L_PMENU_SetTopPosition},
        {"PMENU", "ShowMouse", L_PMENU_ShowMouse},
        {"PMENU", "ShowMenu", L_PMENU_ShowMenu},
        {"PMENU", "ReturnToGame", L_PMENU_ReturnToGame},
        {"WORLD", "SetGamePaused", L_WORLD_SetGamePaused},
        {"WORLD", "IsGamePaused", L_WORLD_IsGamePaused},
        {"PMENU", "AddCheckbox", L_PMENU_AddCheckbox},
        {"PMENU", "AddSlider", L_PMENU_AddSlider},
        {"PMENU", "AddNumRange", L_PMENU_AddNumRange},
        {"PMENU", "AddTextButtonEx", L_PMENU_AddTextButtonEx},
        {"PMENU", "ChangeTextButtonExValue", L_PMENU_ChangeTextButtonExValue},
        {"PMENU", "AddTextEdit", L_PMENU_AddTextEdit},
        {"PMENU", "GetSliderValue", L_PMENU_GetSliderValue},
        {"PMENU", "IsSliderFloat", L_PMENU_IsSliderFloat},
        {"PMENU", "GetNumRangeValue", L_PMENU_GetNumRangeValue},
        {"PMENU", "IsItemChecked", L_PMENU_IsItemChecked},
        {"PMENU", "SetCheckboxValue", L_PMENU_SetCheckboxValue},
        {"PMENU", "GetTextEditValue", L_PMENU_GetTextEditValue},
        {"PMENU", "AddBorder", L_PMENU_AddBorder},
        {"PMENU", "AddTabGroup", L_PMENU_AddTabGroup},
        {"PMENU", "SetBorderSize", L_PMENU_SetBorderSize},
        {"PMENU", "SetBorderHeader", L_PMENU_SetBorderHeader},
        {"PMENU", "SetBorderColCount", L_PMENU_SetBorderColCount},
        {"PMENU", "SetBorderColumn", L_PMENU_SetBorderColumn},
        {"PMENU", "AddStaticText", L_PMENU_AddStaticText},
        {"PMENU", "AddTextButton", L_PMENU_AddTextButton},
        {"PMENU", "SetItemText", L_PMENU_SetItemText},
        {"PMENU", "SetItemDesc", L_PMENU_SetItemDesc},
        {"PMENU", "SetItemAction", L_PMENU_SetItemAction},
        {"PMENU", "SetItemPosition", L_PMENU_SetItemPosition},
        {"PMENU", "SetItemColors", L_PMENU_SetItemColors},
        {"PMENU", "SetItemFonts", L_PMENU_SetItemFonts},
        {"PMENU", "SetItemFontsTex", L_PMENU_SetItemFontsTex},
        {"PMENU", "SetItemVisibility", L_PMENU_SetItemVisibility},
        {"PMENU", "SetItemAlign", L_PMENU_SetItemAlign},
        {"PMENU", "SetItemWidth", L_PMENU_SetItemWidth},
        {"PMENU", "EnableItemBG", L_PMENU_EnableItemBG},
        {"PMENU", "SetItemSounds", L_PMENU_SetItemSounds},
        {"PMENU", "DisableItem", L_PMENU_DisableItem},
        {"PMENU", "EnableItem", L_PMENU_EnableItem},
    };
    for (const auto& n : natives) host.RegisterNative(n.module, n.name, n.fn, this);
}

} // namespace painful
