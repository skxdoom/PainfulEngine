// ScriptEngine: entity lifecycle, transforms, draw state and attachments.
// The ENTITY.* natives that create, move and destroy things, plus the emitter,
// corona and child-entity hookups that hang off one.

#include "ScriptEngineInternal.h"

namespace painful {

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

    // CHILDREN GO WITH THE PARENT, which is what RegisterChild's fifth
    // argument says and what nothing here was doing. A pickup's corona is its
    // own Billboard entity registered as a child; releasing only the pickup
    // left the corona drawn, in mid-air, for the rest of the level.
    //
    // Taken by value and cleared first: the recursive release erases from
    // entities_, and a child that unregisters itself on the way out would
    // otherwise mutate the list being walked.
    {
        const std::vector<int> kids = it->second.children;
        it->second.children.clear();
        for (int kid : kids) {
            auto k = entities_.find(kid);
            if (k == entities_.end()) continue;
            k->second.parent = 0;                 // no back-link into a dying parent
            if (k->second.dieWithParent) ReleaseEntity(kid);
        }
        // The map may have rehashed under the recursion.
        it = entities_.find(handle);
        if (it == entities_.end()) return;
    }
    // And drop the back-link from whatever owns this one, so a parent released
    // later does not walk a handle that is already gone.
    if (it->second.parent != 0) {
        auto p = entities_.find(it->second.parent);
        if (p != entities_.end()) {
            std::vector<int>& siblings = p->second.children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), handle),
                           siblings.end());
        }
    }
    // A bound sound dies with the thing it was following: a PainHead's rotor
    // loop is held, so nothing would ever hand the slot back on its own.
    if (audio_ && it->second.soundVoice)
        audio_->Release(it->second.soundVoice, false);
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
        // Same for the shooting shadow list. It is rebuilt every frame, but a
        // frame with no tick (a pause, a load) would otherwise leave a dead
        // slot in it - and a reused slot would take a live body out of every
        // trace.
        limbShadowed_.erase(
            std::remove(limbShadowed_.begin(), limbShadowed_.end(), it->second.physicsBody),
            limbShadowed_.end());
    }
    // The ragdoll is its own object, at its own offset in the original and on
    // its own slot here, so it outlives the physics body and has to be freed
    // whether or not the entity ever had one. A corpse reaped by DeathTimer is
    // exactly that case.
    if (physics_ && it->second.ragdollSlot >= 0) {
        physics_->RemoveRagdoll(it->second.ragdollSlot);
        it->second.ragdollSlot = -1;
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

// The magnitude window PO_Hit drops an impulse outside (Engine.dll 0x101337a0,
// the doubles at 0x102c02d0 and 0x102c5688).
constexpr float kMinHitImpulse = 0.01f;
constexpr float kMaxHitImpulse = 10000.f;

// PhysicsObject::AddRotateActor's accumulation scale (_DAT_102c8528). What
// EffectRotateActor then does with the accumulated spin is in ScriptDeath.cpp.
constexpr float kSpinPerImpulse = 0.03f;


// ENTITY.PO_Hit(e, x,y,z, ix,iy,iz) - an impulse at a world point.
//
// A CORPSE IS HIT ON ITS RAGDOLL, NOT ON ITS PHYSICS OBJECT. CActor disables
// the physics object the moment it dies (EnableRagdoll(true, DISABLE_PO)), so
// routing every PO_Hit at the movement body means a shot into a body does
// nothing at all - which is what "the corpses feel too heavy" actually is.
//
// The magnitude gate is the engine's own (0x101337a0): it drops the call
// unless |impulse| is between 0.01 and 10000, read off the two doubles at
// 0x102c02d0 and 0x102c5688. The weapons sit inside it by design - Shotgun
// throws 200 (and 100 up), MiniGun 1400, DriverElectro 80.
int ScriptEngine::L_PO_Hit(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const float at[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                         float(luaL_optnumber(L, 4, 0))};
    const float imp[3] = {float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
                          float(luaL_optnumber(L, 7, 0))};
    const float mag = std::sqrt(imp[0]*imp[0] + imp[1]*imp[1] + imp[2]*imp[2]);
    if (!(mag > kMinHitImpulse && mag < kMaxHitImpulse)) return 0;

    if (e->ragdollSlot >= 0 && self->physics_) {
        self->physics_->AddRagdollImpulse(e->ragdollSlot, at, imp);
        return 0;
    }
    // NOT YET DEAD, BUT ABOUT TO BE. The lethal pellet's PO_Hit arrives before
    // OnDamage has run, so there is no ragdoll to give it to yet - bank it and
    // spend it in EnableRagdoll. A monster that survives never activates one,
    // and the banked impulse is simply overwritten by the next hit.
    if (e->isMonster) {
        for (int c = 0; c < 3; ++c) {
            e->deathImpulse[c] = imp[c];
            e->deathImpulseAt[c] = at[c];
        }
        e->hasDeathImpulse = true;
        // And the LIVE body takes it too: PhysicsObject::Hit is EffectForce
        // on the walking body, capped at 30, and the character tick then
        // decays the knock by half a step. That is the shove a monster shows
        // when a shot lands and does not kill.
    }
    ApplyHitImpulse(L, self->physics_, e->physicsBody);
    return 0;
}

// ENTITY.PO_AccumulateRotation(e, x,y,z, ix,iy,iz) - the spin a shot puts on a
// body it has not killed yet.
//
// THIS IS WHERE A DEATH GETS ITS IMPACT. The shotgun calls it for EVERY pellet
// before any of them has killed anything: OnDamage runs afterwards, on the
// accumulated damage, and only then is the ragdoll created. So the momentum
// cannot be applied when it arrives - it has to be held and spent the moment
// the corpse exists, which is what PhysicsObject::AddRotateActor (0x18bff0)
// and EffectRotateActor (0x1893e0) do between them.
//
// AddRotateActor accumulates one float, the Y component of the cross product
// of the offset and the impulse, times 0.03 (_DAT_102c8528):
//
//     spin += ((pz - bz) * ix - iz * (px - bx)) * 0.03
//
// Same magnitude gate as PO_Hit.
int ScriptEngine::L_PO_AccumulateRotation(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const float at[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                         float(luaL_optnumber(L, 4, 0))};
    const float imp[3] = {float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
                          float(luaL_optnumber(L, 7, 0))};
    const float mag = std::sqrt(imp[0]*imp[0] + imp[1]*imp[1] + imp[2]*imp[2]);
    if (!(mag > kMinHitImpulse && mag < kMaxHitImpulse)) return 0;
    e->deathSpin += ((at[2] - e->pos[2]) * imp[0] - imp[2] * (at[0] - e->pos[0])) * kSpinPerImpulse;
    return 0;
}

// MDL.ApplyPointImpulseToRagdoll(e, x,y,z, ix,iy,iz) - the same thing said
// directly, which is what CAction:Action_ImpulseToRagdoll uses to throw a body
// from an animation. No magnitude gate on this one; the engine's version
// (0x1012b8c0) has none either.
int ScriptEngine::L_MDL_ApplyPointImpulseToRagdoll(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || e->ragdollSlot < 0 || !self->physics_) return 0;
    const float at[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                         float(luaL_optnumber(L, 4, 0))};
    const float imp[3] = {float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
                          float(luaL_optnumber(L, 7, 0))};
    self->physics_->AddRagdollImpulse(e->ragdollSlot, at, imp);
    return 0;
}

// ENTITY.PO_ScaleInertiaTensor - s_Physics.InertiaTensorMultiplier, which is
// 0.1 on every monster that declares one. CActor applies it 15 ticks AFTER
// death (_inertiaTensorDelayedEnable), once the ragdoll has settled into the
// solver. A tenth of the inertia is a body that tumbles instead of toppling.
int ScriptEngine::L_PO_ScaleInertiaTensor(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (e && e->ragdollSlot >= 0 && self->physics_)
        self->physics_->ScaleRagdollInertia(e->ragdollSlot,
                                            float(luaL_optnumber(L, 2, 1.0)));
    return 0;
}

int ScriptEngine::L_WORLD_HitPhysicObject(lua_State* L) {
    ScriptEngine* self = From(L);
    // -1 is what a trace reports for the world itself, which cannot be moved.
    ApplyHitImpulse(L, self->physics_, int(luaL_optnumber(L, 1, -1)));
    return 0;
}

// WORLD.GetLastExplodedEntities(item) -> the debris ExplodeItem just made.
//
// IT MUST RETURN A TABLE, EVEN AN EMPTY ONE. CItem:DestroyItemFX walks the
// answer on the very next line - `for i,o in parts do` - and iterating nil is
// an error in Lua 5.0. Returning nothing did not merely lose the parts: the
// error unwound out of CObject:TickTimers through table.foreachi, which
// abandons the REST OF THE OBJECT LIST for that frame. Every item after the
// one that exploded stopped ticking, so ammo could no longer be picked up and
// the level came apart around one missing return value.
//
// The table is empty until ExplodeItem spawns anything, which is honest rather
// than convenient: no parts exist yet, and the scripts read the emptiness
// correctly.
int ScriptEngine::L_WORLD_GetLastExplodedEntities(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    lua_newtable(L);
    const auto it = self->lastExploded_.find(handle);
    if (it == self->lastExploded_.end()) return 1;
    int index = 1;
    for (const int part : it->second) {
        lua_pushnumber(L, index++);
        lua_pushnumber(L, part);
        lua_settable(L, -3);
    }
    return 1;
}

// ENTITY.ExplodeItem(item, pack, strength, radius, lifetime, _, bindTo, noSelf)
//
// The item becomes its own wreckage: one entity per object in the DestroyPack,
// standing where the item stood and thrown outward. CItem:DestroyItemFX turns
// the item's physics off, calls this, and then immediately asks
// GetLastExplodedEntities for the parts so it can texture them and set the
// burning ones alight - so they have to exist by the time this returns.
int ScriptEngine::L_ENTITY_ExplodeItem(lua_State* L) {
    ScriptEngine* self = From(L);
    const int srcHandle = HandleArg(L, 1);
    Entity* src = self->Find(srcHandle);
    const char* packArg = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
    if (!src || !packArg || !*packArg) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float strength = float(luaL_optnumber(L, 3, 0));
    const float lifetime = float(luaL_optnumber(L, 5, 0));

    DatPack pack;
    if (!DatPack::Load(self->host_->ResolvePath(packArg), pack) || pack.objects.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Taken before the registry grows: the parts inherit the item's pose.
    const std::string source = packArg;
    const float scale = src->scale;
    // THE PARTS INHERIT THE ITEM'S VELOCITY - this is what makes wreckage carry
    // the blast that broke it, or the momentum of the fall. CItem:DestroyItemFX
    // reads it, scales it by Destroy.VelocityFactor and writes it back just
    // before calling this, and the only two templates that set that factor use
    // (0,0,0) - so the factor is an opt-OUT and inheriting is the default.
    // The body is read first: it carries the impulse WORLD.Explosion2 just
    // applied, which the entity store has not seen.
    float inherited[3] = {src->velocity[0], src->velocity[1], src->velocity[2]};
    // Only while the body is still in the world - once DestroyItemFX has
    // disabled it, PO_Enable's snapshot in src->velocity is the truthful one.
    if (src->poEnabled && self->physics_ && src->physicsBody >= 0) {
        float v[3];
        if (self->physics_->GetScriptBodyVelocity(src->physicsBody, v))
            for (int c = 0; c < 3; ++c) inherited[c] = v[c];
    }
    float pos[3], rot[4];
    for (int c = 0; c < 3; ++c) pos[c] = src->pos[c];
    for (int c = 0; c < 4; ++c) rot[c] = src->rotWXYZ[c];

    std::string packName;
    const bool haveShape = self->SplitPackSource(source, packName);

    std::vector<int>& parts = self->lastExploded_[srcHandle];
    parts.clear();
    parts.reserve(pack.objects.size());

    for (const MapObject& object : pack.objects) {
        Entity part;
        part.type = kMesh;
        part.source = source;
        part.mesh = object.name;
        part.scale = scale;
        for (int c = 0; c < 3; ++c) part.pos[c] = pos[c];
        for (int c = 0; c < 4; ++c) part.rotWXYZ[c] = rot[c];
        part.inWorld = true;
        // LifetimeAfterExplosion. Without it the debris is permanent, and it
        // is debris with real bodies - it would pile up as collision.
        part.timeToDie = lifetime > 0.f ? lifetime : 3.f;

        // Which way a piece goes: its own offset inside the pack. The parts are
        // modelled in place - a barrel's staves sit where they were before it
        // came apart - so each centre already points away from the middle, and
        // the wreck separates the way it was assembled.
        float dir[3] = {0, 0, 0};
        float len = 0.f;
        for (int c = 0; c < 3; ++c) {
            dir[c] = 0.5f * (object.bboxMin[c] + object.bboxMax[c]);
            len += dir[c] * dir[c];
        }
        len = std::sqrt(len);
        if (len > 1e-4f) for (int c = 0; c < 3; ++c) dir[c] /= len;
        else            { dir[0] = 0.f; dir[1] = 1.f; dir[2] = 0.f; }

        // Destroy.Strength runs from 1 to 50 across the shipped items. How the
        // engine turns that into a speed has NOT been recovered from the binary
        // yet, so this is an approximation with a lift on it - enough that the
        // pieces leave the ground rather than sliding apart along the floor.
        const float speed = 0.5f + strength * 0.05f;
        for (int c = 0; c < 3; ++c) part.velocity[c] = inherited[c] + dir[c] * speed;
        part.velocity[1] += speed * 0.5f;

        const int handle = self->nextHandle_++;
        Entity& live = self->entities_.emplace(handle, part).first->second;
        ++self->created_;
        self->CreateRendererInstance(live);

        // A real body, so the wreckage falls, bounces and settles instead of
        // hanging where the item was.
        //
        // BodyTypes.FromMesh (4), NOT Default (0).
        //
        // Nothing in a .dat carries collision - an object is geometry, a
        // material and a bbox - so the shape is derived, and Default derives a
        // SPHERE sized by the largest half-extent. For a barrel stave measuring
        // 0.50 x 3.84 x 1.12 that is a ball of radius 1.9 wrapped around a
        // plank: the wreckage rolls like barrels because every piece of it
        // literally is one. FromMesh takes the convex hull instead, which for
        // these parts is 50-70 points apiece.
        if (self->physics_ && haveShape) {
            const int slot = self->physics_->CreateScriptBody(
                4, "", packName, live.mesh, live.scale, live.pos, live.rotWXYZ,
                self->dataRoot_, 3 /* ECollisionGroups.Normal */);
            if (slot >= 0) {
                live.physicsBody = slot;
                self->bodyToEntity_[slot] = handle;
                self->physics_->SetScriptBodyVelocity(slot, live.velocity);
            }
        }
        parts.push_back(handle);
    }

    // What is left standing is the debris, not the item - and the item has to
    // stop being SOLID as well as stop being drawn. DestroyItemFX turns its
    // physics off just before calling this, but PO_Enable(false) only sleeps a
    // body, and a sleeping body still collides. Hiding it alone left an
    // invisible barrel standing exactly where the barrel had been, which reads
    // as the debris having inherited the whole item's collision.
    src->visible = false;
    self->SyncPose(*src);
    src->bodyNonColliding = true;
    if (self->physics_ && src->physicsBody >= 0)
        self->physics_->MakeScriptBodyNonColliding(src->physicsBody);

    lua_pushboolean(L, 1);
    return 1;
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
            // SetPosition gives the FEET (see SyncPlayerFromPawn); the pawn is
            // driven from the eye.
            self->pawn_->SetFloorPos(e->pos);   // teleports (spawn, checkpoints)
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
        // PhysicsObject::SetOrientation (0x10189F70) writes the body's
        // rotation; a character body's yaw is the scripts', not the solver's.
        if (self->physics_ && e->physicsBody >= 0 && e->isMonster)
            self->physics_->SetScriptBodyRotation(e->physicsBody, e->rotWXYZ);
    }
    return 0;
}

// ENTITY.GetOrientation(e) -> yaw, radians.
//
// 0x10132420: with an enabled physics object the answer is
// PhysicsObject::GetOrientation off the body's rotation; otherwise it rotates
// (0,0,1) by the entity's rotation and returns atan2(x, z) of the result. Both
// are the heading of the entity's local +Z. The PLAYER has no entity rotation
// here - the pawn carries its facing - so it answers from the pawn's forward,
// which is what MiniGunRL:Fire and RifleFlameThrower:ComboCheck turn into the
// projectile's yaw (`-orientation + 1.57`). Answering 0 for the player put
// every rocket's long axis 90 degrees off its flight.
int ScriptEngine::L_GetOrientation(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    float fwd[3] = {0, 0, 1};
    if (self->pawn_ && handle == self->playerHandle_ && self->playerHandle_) {
        // The same basis CAM.GetForwardVector answers with.
        const float cp = std::cos(self->camPitch_);
        fwd[0] = std::cos(self->camYaw_) * cp;
        fwd[1] = std::sin(self->camPitch_);
        fwd[2] = std::sin(self->camYaw_) * cp;
    } else if (const Entity* e = self->Find(handle)) {
        const float z[3] = {0, 0, 1};
        EngineQuatRotate(e->rotWXYZ, z, fwd);
    } else {
        lua_pushnumber(L, 0);
        return 1;
    }
    lua_pushnumber(L, std::atan2(fwd[0], fwd[2]));
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
    Entity::EmitterRec rec;
    rec.file = luaL_optstring(L, 2, "");
    e->emitterRecs.push_back(rec);
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
    if (idx < e->emitterRecs.size()) {
        Entity::EmitterRec& rec = e->emitterRecs[idx];
        rec.setup = true;
        rec.scale = float(luaL_optnumber(L, 3, 1.0));
        for (int c = 0; c < 3; ++c) { rec.offset[c] = offset[c]; rec.rotDeg[c] = rotDeg[c]; }
    }
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
    e->hasCorona = true;
    for (int i = 0; i < 9; ++i) e->coronaArgs[i] = args[i];
    e->coronaTex = luaL_optstring(L, 11, "");
    e->coronaColor = uint32_t(int64_t(luaL_optnumber(L, 12, 0)));
    e->coronaBlend = int(luaL_optnumber(L, 13, 1));
    e->coronaSpriteOnly = lua_toboolean(L, 14) != 0;
    e->spriteSlot = self->billboards_->SetupScriptCorona(
        e->spriteSlot, args, e->coronaTex, e->coronaColor, e->coronaBlend,
        e->coronaSpriteOnly, *self->textures_, self->world_.levelName);
    self->billboards_->SetScriptSpritePos(e->spriteSlot, e->pos);
    return 0;
}

// ENTITY.EnableDraw(e, on, alsoChildren)
//
// Entity::EnableDraw (0x1d0af0) only flips bit 0x40 on the entity itself, so
// the third argument is the script glue's: CObject names it `alsoChildren`
// where it passes one through.
//
// The checkpoints need it. CheckPoint:OnApply is
// ENTITY.EnableDraw(self._Entity, not self.Frozen, true) and every level
// instance ships o.Frozen = true, so a checkpoint is meant to be invisible
// until its script launches it. Its glow is not part of its model: OnCreateEntity
// BindFX()es four particle effects, and BindFX makes each one a SEPARATE entity
// registered as a child. Hiding only the parent left all four burning at spawn.
void ScriptEngine::SetDrawEnabled(Entity& e, bool on, bool alsoChildren, int depth) {
    e.visible = on;
    SyncPose(e);
    if (!alsoChildren || depth > 8) return;
    for (int child : e.children)
        if (Entity* c = Find(child)) SetDrawEnabled(*c, on, true, depth + 1);
}

int ScriptEngine::L_EnableDraw(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1)))
        self->SetDrawEnabled(*e, lua_toboolean(L, 2) != 0, lua_toboolean(L, 3) != 0, 0);
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
        // A projectile's velocity lives in the entity store, for the same
        // reason its position does: TickProjectiles owns it, and gravity is
        // integrated there. The body still holds whatever SetVelocity launched
        // it with and never hears about the arc, so reading the body back gave
        // a stake that was visibly falling a velocity that was still dead
        // level - and Stake:Tick builds both its trace direction and its
        // tumble axis out of that vector.
        if (e->isProjectile ||
            !(self->physics_ && e->physicsBody >= 0 &&
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
    const int handle = HandleArg(L, 1);
    float v[3];
    for (int c = 0; c < 3; ++c) v[c] = float(luaL_optnumber(L, c + 2, 0));
    // The player is the pawn, not a script body, so it needs the same special
    // case GetVelocity has - JumpPad:OnEnter is nothing but this call, and
    // writing the entity store alone left the pad inert.
    if (self->pawn_ && handle == self->playerHandle_ && self->playerHandle_) {
        self->pawn_->SetVelocity(v);
        return 0;
    }
    Entity* e = self->Find(handle);
    if (!e) return 0;
    for (int c = 0; c < 3; ++c) e->velocity[c] = v[c];
    if (self->physics_ && e->physicsBody >= 0)
        self->physics_->SetScriptBodyVelocity(e->physicsBody, e->velocity);
    return 0;
}

// ENTITY.SetAngularVelocity(e, x, y, z)
//
// A world-space axis scaled by radians per second: the native reads three
// floats from arguments 2..4 and hands them straight to
// PhysicsObject::SetAngularVel (0x10132260). Stake:Tick is the caller that
// matters - the moment the stake starts to fall it spins it about the
// horizontal axis across its own travel, which is what turns the arc into a
// stake going nose-first into the floor rather than sliding down flat.
int ScriptEngine::L_SetAngularVelocity(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    for (int c = 0; c < 3; ++c)
        e->angVel[c] = float(luaL_optnumber(L, c + 2, 0));
    return 0;
}

// ECollisionGroups.Fixed, from LScripts/Main/Definitions.lua. The static
// world is this and so is anything a script pins in place.
constexpr int kCollisionFixed = 1;

// ENTITY.PO_Remove(e) - drop the physics object and stop moving.
//
// This is how the Painkiller's head STICKS. PainHead:Tick, on a hit it does not
// bounce off, does exactly two things: SetPosition to the impact point, then
// PO_Remove. Without it the head kept its velocity and sailed on through the
// wall, so the alt fire never planted itself and the beam it anchors never
// had an anchor.
//
// Removing the body is not enough on its own: a projectile is moved by
// TickProjectiles from the entity's own velocity, not by the solver, so that
// has to stop too.
int ScriptEngine::L_PO_Remove(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    if (self->physics_ && e->physicsBody >= 0) {
        self->physics_->RemoveScriptBody(e->physicsBody);
        self->bodyToEntity_.erase(e->physicsBody);
        e->physicsBody = -1;
    }
    e->isProjectile = false;
    for (int c = 0; c < 3; ++c) { e->velocity[c] = 0.f; e->angVel[c] = 0.f; }
    return 0;
}

// ENTITY.PO_GetCollisionGroup(e) -> ECollisionGroups, and PO_IsFixed(e).
//
// PainHead:Tick reads the group of whatever it hit and ignores Noncolliding (7)
// and Particles (8) - another blade in flight, or a shell casing, is not
// something to stick into. The static world has no entity and no body, and it
// is Fixed (1): answering 0 there made the head treat every wall as a live
// collision group.
int ScriptEngine::L_PO_GetCollisionGroup(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushnumber(L, e ? e->collisionGroup : kCollisionFixed);
    return 1;
}

// PainHead sticks into a fixed mesh and bounces off one that only LOOKS fixed:
//     if ENTITY.IsFixedMesh(e) and not ENTITY.PO_IsFixed(e) then back end
// Unimplemented, `not nil` was true, so every wall sent the head home.
int ScriptEngine::L_PO_IsFixed(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    const Entity* e = self->Find(handle);
    lua_pushboolean(L, (!e && handle == 0) || (e && e->collisionGroup == kCollisionFixed));
    return 1;
}

// R3D.DistToLine(px,py,pz, ax,ay,az, bx,by,bz) -> distance from the point to
// the segment a..b.
//
// The Painkiller gates its beam on it: the head is only wired to the gun while
// it is near the line of sight, `d < PainRayTolerance`. Lua 5.0 compares nil
// with a number by raising, so an unimplemented DistToLine did not merely skip
// the beam - it threw out of PainKiller:OnUpdate every frame the head was out.
int ScriptEngine::L_R3D_DistToLine(lua_State* L) {
    float p[3], a[3], b[3];
    for (int c = 0; c < 3; ++c) {
        p[c] = float(luaL_optnumber(L, 1 + c, 0));
        a[c] = float(luaL_optnumber(L, 4 + c, 0));
        b[c] = float(luaL_optnumber(L, 7 + c, 0));
    }
    float ab[3], ap[3], len2 = 0.f, dot = 0.f;
    for (int c = 0; c < 3; ++c) {
        ab[c] = b[c] - a[c];
        ap[c] = p[c] - a[c];
        len2 += ab[c] * ab[c];
        dot += ap[c] * ab[c];
    }
    float t = len2 > 1e-8f ? dot / len2 : 0.f;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    float d2 = 0.f;
    for (int c = 0; c < 3; ++c) {
        const float v = ap[c] - ab[c] * t;
        d2 += v * v;
    }
    lua_pushnumber(L, std::sqrt(d2));
    return 1;
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

    // Argument 4 is the ECollisionGroups value, and it decides whether this is
    // a rigid body at all: Noncolliding (7) is how every projectile is made.
    const int collisionGroup = int(luaL_optnumber(L, 4, 0));
    // BodyTypes.Sphere / SphereSweep with an explicit scale is a sphere of
    // radius scale * 1.1, whatever the mesh looks like (the sizer's case 1).
    const float argScale = float(luaL_optnumber(L, 3, -1.0));
    const float sphereRadius =
        (bodyType == 1 || bodyType == 9) && argScale > 0.f ? argScale * 1.1f : 0.f;
    const int slot = self->physics_->CreateScriptBody(
        bodyType, model, pack, e->mesh, scale, e->pos, e->rotWXYZ, self->dataRoot_,
        collisionGroup, sphereRadius);
    if (slot >= 0) {
        e->physicsBody = slot;
        e->bodyType = bodyType;
        e->bodyArgScale = argScale;
        // Noncolliding (7) only. That group means "touches nothing", which is
        // what the stake, the bolt and the electro disk are made with, and it
        // is the one unambiguous signal. Particles (8) is NOT included: shell
        // casings live there too, and they are meant to tumble. Anything else
        // that is driven says so through RemoveFromIntersectionSolver.
        e->isProjectile = collisionGroup == 7;
        e->collisionGroup = collisionGroup;
        self->bodyToEntity_[slot] = HandleArg(L, 1);
    }
    return 0;
}

// ENTITY.PO_Move(e, x, y, z) - where this actor WANTS to go, as a velocity.
//
// A pure setter, exactly as in the original: 0x10130D50 writes the three
// floats to PhysicsObject+0x34 and returns. Nothing moves here; the physics
// step spends it (PhysicsWorld::StepCharacters). CActor calls this with
// `mv * (1/delta)`, which is why the units are per second.
int ScriptEngine::L_PO_Move(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    for (int c = 0; c < 3; ++c) e->moveWish[c] = float(luaL_optnumber(L, c + 2, 0));
    if (self->physics_ && e->physicsBody >= 0)
        self->physics_->SetCharacterWish(e->physicsBody, e->moveWish);
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
    if (self->physics_ && e->physicsBody >= 0) {
        // UNCONDITIONAL. Being a monster is not contingent on the shape: the
        // engine sets the flag at PhysicsObject+0x74 whatever the rig looks
        // like, and in CreatePhysicsObject the ROOOT test guards only the
        // reading of that joint's position, not the object.
        //
        // Four of ten shipped rigs have no ROOOT at all - zombie, zombie_v2,
        // vamp_small, raven - so gating the call on finding one left those
        // bodies DYNAMIC. They fell out from under their monsters with their
        // own gravity and mass, which is not a placement bug at all.
        //
        // A k of 0 keeps whatever shape the body already has; only the sizing
        // depends on the joint.
        float k = 0.f, rootOffsetY = 0.f;
        self->MonsterBodyScale(*e, k, rootOffsetY);
        self->physics_->MakeScriptBodyCharacter(e->physicsBody, k, rootOffsetY);
        // Whatever the scripts set before the flag arrived.
        self->physics_->SetCharacterMovement(e->physicsBody, e->monsterMoveConst,
                                             e->monsterMoveFlag);
        self->physics_->SetCharacterFlying(e->physicsBody, e->monsterFlying);
        self->physics_->SetCharacterWish(e->physicsBody, e->moveWish);
    }
    return 0;
}


// --- entity children -------------------------------------------------------
//
// BindSoundToEntity creates a Sound entity, names it through SND.Setup3D and
// hangs it off its owner with RegisterChild; the Quad and WeaponModifier
// pickups use that to give the player a looping sound while the powerup runs,
// and remove it by name when it expires.
//
// GetChildByName returning NOTHING was a real bug rather than a missing
// feature. QuadSound runs on every shot and tests `if quad ~= 0`, and in Lua
// `nil ~= 0` is TRUE - so a stub that returned nothing announced that the
// player was holding every powerup, and the damage loop played on every shot.
// A native whose absence inverts a script's test is worse than one that does
// nothing, because the script takes the wrong branch confidently.
int ScriptEngine::L_ENTITY_RegisterChild(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* parent = self->Find(HandleArg(L, 1));
    const int child = HandleArg(L, 2);
    Entity* childEntity = self->Find(child);
    if (!parent || child == 0 || !childEntity) return 0;
    if (std::find(parent->children.begin(), parent->children.end(), child) ==
        parent->children.end())
        parent->children.push_back(child);
    // The back-link, which is what lets the child be PLACED. The parent's list
    // alone says who owns the child, not where it goes.
    childEntity->parent = HandleArg(L, 1);
    // Fifth argument, default true - see Entity::dieWithParent.
    childEntity->dieWithParent = lua_isnone(L, 5) || lua_toboolean(L, 5) != 0;
    return 0;
}

// ENTITY.GetPtrByIndex(entity) -> the entity's pointer, or NIL when it is gone.
//
// 0x1012f690 bounds-checks the index against the entity table, reads the slot,
// and pushes nil when the index is out of range or the slot is empty; otherwise
// it pushes an int off the entity. The scripts use it as an existence test, and
// the pointer value itself is never compared to anything.
//
// Left unbound it returned nil, which reads as "this entity is gone" - and
// ShurikenW:Tick opens with
//
//     if not ENTITY.GetPtrByIndex(self._Entity) or self._ExplodeTimer <= 0 then
//         ... Explosion(...) ... GObjects:ToKill(self)
//
// so the electro shuriken detonated on its FIRST tick, every time, with the
// timer never reaching zero because nothing waited for it. BoltStick and Stake
// ask the same question.
int ScriptEngine::L_ENTITY_GetPtrByIndex(lua_State* L) {
    ScriptEngine* self = From(L);
    const int handle = HandleArg(L, 1);
    if (handle == 0 || !self->Find(handle)) {
        lua_pushnil(L);
        return 1;
    }
    // The handle IS our pointer: stable for the entity's life and never zero,
    // which is all the scripts ask of it.
    lua_pushnumber(L, handle);
    return 1;
}

// PARTICLE.SetParentOffset(pfx, x, y, z, joint, ...)
//
// Where a bound effect sits on the thing it is bound to. Arguments 2..4 are the
// offset and argument 5 is the JOINT - an index when it is a number, a name
// when it is a string, which is the branch the engine takes on the Lua type
// (0x10139e30 tests for LUA_TNUMBER before choosing GetInt or GetString).
//
// CActor:BindFX calls this for every effect a monster carries, right after
// RegisterChild. Unimplemented, every one of them stayed at the world origin.
int ScriptEngine::L_PARTICLE_SetParentOffset(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    for (int c = 0; c < 3; ++c)
        e->parentOffset[c] = float(luaL_optnumber(L, c + 2, 0));

    e->parentJoint.clear();
    e->parentJointIndex = -2;
    if (lua_isnumber(L, 5)) {
        e->parentJointIndex = int(lua_tonumber(L, 5));
    } else if (lua_isstring(L, 5)) {
        e->parentJoint = lua_tostring(L, 5);
    }
    // Arguments 9..11: an Euler rotation (the engine's own qz*qy*qx order,
    // FUN_1011bcd0) the effect carries on top of its joint. Given only when
    // the 11th argument exists, exactly as 0x10139e30 tests it. The
    // flamethrower binds RFT_flame with (0, 1.57, 0) to turn the emitter's +X
    // velocity down the barrel. Arguments 6..8 are a per-axis pull toward the
    // camera, which nothing in the shipped scripts passes non-zero.
    e->parentRotBound = lua_gettop(L) >= 11;
    if (e->parentRotBound) {
        EngineEulerToQuat(float(luaL_optnumber(L, 9, 0)), float(luaL_optnumber(L, 10, 0)),
                          float(luaL_optnumber(L, 11, 0)), e->parentRotWXYZ);
    }
    e->parentBound = true;
    self->PlaceAttached(*e);
    return 0;
}

// PARTICLE.Die(pfx) - a bound effect ends: 0x10139a30 unregisters it from its
// parent and, for every emitter, zeroes the spawn budget and clears the
// evolve flag. The entity then goes the way a spent one-shot does, once its
// last particle has died (TickLifetimes). RifleFlameThrower:EnableFX calls
// this on the flame the moment the trigger is released.
int ScriptEngine::L_PARTICLE_Die(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    if (e->parent != 0) {
        if (Entity* parent = self->Find(e->parent)) {
            std::vector<int>& kids = parent->children;
            kids.erase(std::remove(kids.begin(), kids.end(), HandleArg(L, 1)), kids.end());
        }
        e->parent = 0;
        e->parentBound = false;
    }
    for (Entity::EmitterRec& rec : e->emitterRecs) rec.stopped = true;
    if (self->particles_)
        for (int slot : e->emitterSlots)
            if (slot >= 0) self->particles_->StopScriptEmitter(slot);
    return 0;
}

// A bone by name, or -1. The same lookup MDL.GetJointIndex does, factored out
// because an attachment resolves its joint once and then rides it.
int ScriptEngine::JointIndexByName(Entity& e, const std::string& name) {
    if (e.type != kModel || name.empty()) return -1;
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel) return -1;
    for (size_t i = 0; i < skel->bones.size(); ++i)
        if (EqualsCI(skel->bones[i].name, name.c_str())) return int(i);
    return -1;
}

// Puts one bound entity where its parent says it goes.
void ScriptEngine::PlaceAttached(Entity& e) {
    if (!e.parentBound || e.parent == 0) return;
    Entity* parent = Find(e.parent);
    if (!parent) return;

    // A named joint is resolved once and remembered: the lookup is by string
    // against the skeleton, and these are placed every frame.
    if (e.parentJointIndex == -2 && !e.parentJoint.empty())
        e.parentJointIndex = JointIndexByName(*parent, e.parentJoint);

    // ParticleEffect::Tick (0x101e59a0): with a joint, the position is the
    // offset through the joint's transform and the rotation is the joint's
    // composed with the bound Euler - or the PARENT's rotation when no Euler
    // was given. Without a joint, the offset is rotated by the parent and the
    // rotation is the parent's composed with the Euler, if any.
    float world[3];
    float rot[4];
    bool haveRot = false;
    if (e.parentJointIndex >= 0 && JointToWorld(*parent, e.parentJointIndex,
                                                e.parentOffset, world)) {
        if (e.parentRotBound) {
            float joint[4];
            if (JointWorldRotation(*parent, e.parentJointIndex, joint)) {
                EngineQuatMul(joint, e.parentRotWXYZ, rot);
                haveRot = true;
            }
        } else {
            for (int c = 0; c < 4; ++c) rot[c] = parent->rotWXYZ[c];
            haveRot = true;
        }
    } else {
        float turned[3];
        EngineQuatRotate(parent->rotWXYZ, e.parentOffset, turned);
        for (int c = 0; c < 3; ++c) world[c] = parent->pos[c] + turned[c];
        if (e.parentRotBound) {
            EngineQuatMul(parent->rotWXYZ, e.parentRotWXYZ, rot);
            haveRot = true;
        }
    }
    for (int c = 0; c < 3; ++c) e.pos[c] = world[c];
    if (haveRot)
        for (int c = 0; c < 4; ++c) e.rotWXYZ[c] = rot[c];
    SyncPose(e);
}

// Every bound entity, once the parents have finished moving for the frame.
void ScriptEngine::UpdateAttached() {
    for (auto& kv : entities_)
        if (kv.second.parentBound) PlaceAttached(kv.second);
}


}  // namespace painful
