#include "ScriptEngine.h"

#include "../Assets/Emitter.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "../Render/BillboardRenderer.h"
#include "../Render/EntityRenderer.h"
#include "../Render/ParticleRenderer.h"
#include "../Render/TextureCache.h"
#include "PlayerPawn.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace painful {

namespace {

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
}

void ScriptEngine::AttachPhysics(PhysicsWorld* physics, const std::string& dataRoot) {
    physics_ = physics;
    dataRoot_ = dataRoot;
    animations_.SetRoot(dataRoot + "/Models");
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

// SetOrientation/GetOrientation: yaw about Y, radians - BindPoint in
// Utils.lua rotates offsets with cos/sin of the negated value.
int ScriptEngine::L_SetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1))) {
        const float a = float(luaL_optnumber(L, 2, 0)) * 0.5f;
        e->rotWXYZ[0] = std::cos(a);
        e->rotWXYZ[1] = 0;
        e->rotWXYZ[2] = std::sin(a);
        e->rotWXYZ[3] = 0;
        self->SyncPose(*e);
    }
    return 0;
}

int ScriptEngine::L_GetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    // Extract the yaw component of the stored quaternion.
    const float yaw =
        e ? 2.f * std::atan2(e->rotWXYZ[2], e->rotWXYZ[0]) : 0.f;
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
    const Entity* e = self->Find(HandleArg(L, 1));
    float v[3] = {0, 0, 0};
    if (e) {
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
        const float zero[3] = {0, 0, 0};
        self->pawn_->Spawn(zero);
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
static constexpr float kPi = 3.14159265358979f;

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
    float localQuat[4], localRot[9], worldRot[9];
    EngineEulerToQuat(float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
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
        e->animSlots.push_back({name, anim->duration()});
        index = int(e->animSlots.size()) - 1;
    }

    e->animIndex = index;
    e->animTime = 0.f;
    e->animLoop = lua_toboolean(L, 3) != 0;
    // The template's declared speed. A speed of zero would stall the event
    // loop the moment it started, so an unspecified or zero speed plays at 1.
    const float speed = float(luaL_optnumber(L, 4, 1.0));
    e->animScale = speed > 0.f ? speed : 1.f;
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
    e->animSlots.push_back({name, self->animations_.Get(e->source, name)->duration()});
    lua_pushnumber(L, double(e->animSlots.size() - 1));
    return 1;
}

// MDL.GetAnimMovement(e, index, delta) -> how far the animation itself moved
// the actor this frame: root motion, the thing that carries a monster forward
// during an attack.
//
// Zero for now, and that is a placeholder rather than an answer. The real
// value is the root bone's translation between last frame's time and this
// one, and the .ani tracks hold it - but WHICH bone is the root, and how the
// "moving curve" flag in an actor's template selects it, is not established.
// Guessing at that is how the rotation conventions went wrong. See
// Docs/Animation.md.
//
// It must still return three numbers: CActor multiplies them the moment it
// has a moving curve, so returning nothing throws an arithmetic error that
// aborts the whole tick - which is precisely how this need announced itself.
int ScriptEngine::L_MDL_GetAnimMovement(lua_State* L) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
}

// MDL.TransformPointByJoint(e, joint, x,y,z, ...) -> a point carried by a
// bone, plus that bone's rotation: x,y,z,rw,rx,ry,rz. It is how a muzzle
// flash sits at the barrel and how anything else rides a skeleton.
//
// The joint stage has not been built (Docs/Animation.md), so this answers
// with the ENTITY's own position and an identity rotation. That is a
// placeholder, and a deliberately visible one - effects land on the object
// rather than at its barrel, instead of at the world origin.
//
// It has to return all seven values regardless. Returning nothing is what the
// stub did, and once the animation clock let weapons reach this at all, the
// nils flowed into Vector:New and threw an error that aborted Game_Tick
// entirely - taking the whole actor update with it, every frame a weapon
// fired.
int ScriptEngine::L_MDL_TransformPointByJoint(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, e ? e->pos[c] : 0.0);
    lua_pushnumber(L, 1);   // identity quaternion, engine order (w,x,y,z)
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 7;
}

// MDL.GetJointPos(e, joint) -> where a bone is. Same placeholder, same
// reason.
int ScriptEngine::L_MDL_GetJointPos(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, e ? e->pos[c] : 0.0);
    return 3;
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
        {"PARTICLE", "SetFixedTransform", L_NoOpNative},
        {"BILLBOARD", "SetupCorona", L_BILLBOARD_SetupCorona},
        {"ENTITY", "GetVelocity", L_GetVelocity},
        {"ENTITY", "SetVelocity", L_SetVelocity},
        {"ENTITY", "SetTimeToDie", L_SetTimeToDie},
        {"ENTITY", "PO_Hit", L_PO_Hit},
        {"WORLD", "HitPhysicObject", L_WORLD_HitPhysicObject},
        {"ENTITY", "PO_Create", L_PO_Create},
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
    };
    for (const auto& n : natives) host.RegisterNative(n.module, n.name, n.fn, this);
}

} // namespace painful
