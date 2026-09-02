// ScriptEngine: contacts, reported to the scripts as COLLISION_WITH_OTHER_ENTITY.
//
// This is how a destructible breaks. Nothing damages a crate to destroy it -
// CItem:Apply installs StdOnCollision on anything carrying
// Destroy.MinSpeedOnCollision, and that handler compares the IMPACT SPEED
// against it:
//
//     vl = vl * INP.GetTimeMultiplier()
//     if vl >= self.Destroy.MinSpeedOnCollision then
//         self:OnDamage(vl * 0.3, self, AttackTypes.ItemCollision)
//
// So the shotgun's freeze bolt breaks a crate by hitting it fast enough, not by
// dealing damage - which is why the same weapon correctly imparts no impulse.
// BarrelBig wants 18, AmmoBox 13.
//
// The message the engine sends carries ten values:
//
//     Game_GetMsg('COLLISION_WITH_OTHER_ENTITY',
//                 e_me, x,y,z, nx,ny,nz, e_other, h_me, h_other)
//
// and the SCRIPT fills in the rest itself - Game_GetMsg reads the two body
// velocities back through PHYSICS.GetHavokBodyVelocity and computes the
// relative speed as arg[14]. That is why this posts body handles as well as
// entities: without them the handler has nothing to measure.

#include "ScriptEngineInternal.h"

namespace painful {

// ENTITY.EnableCollisions(entity, on = true, minTime = 0.4, minStrength = 0.6)
//
// 0x10130420 reads the arguments in that order and forwards them to
// PhysicsObject::SetCollisionCallbacks, and it does nothing at all unless the
// entity has a physics object. Ball.CItem declares the pair as
// `CollisionDetect = { MinTime = 0.3, MinStren = 5.0 }`, which is the same two
// numbers by another name.
int ScriptEngine::L_ENTITY_EnableCollisions(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || e->physicsBody < 0) return 0;
    e->collisionsOn = lua_isnone(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    e->collisionMinTime = float(luaL_optnumber(L, 3, 0.4));
    e->collisionMinStrength = float(luaL_optnumber(L, 4, 0.6));
    e->collisionCooldown = 0.f;
    return 0;
}

// PHYSICS.GetHavokBodyVelocity(body) -> vx, vy, vz, speed
//
// Game_GetMsg calls this on BOTH handles of a collision to work out how hard it
// was. Four returns: the vector and its magnitude, because every caller wants
// the magnitude and none of them should have to compute it.
int ScriptEngine::L_PHYSICS_GetHavokBodyVelocity(lua_State* L) {
    ScriptEngine* self = From(L);
    float v[3] = {0, 0, 0};
    const int slot = lua_isnumber(L, 1) ? int(lua_tonumber(L, 1)) : -1;
    // A body involved in this frame's collisions answers with the velocity it
    // had AT THE CONTACT. The scripts ask this while handling the message, by
    // which point the solver has already spent the impact and the live value is
    // near zero - so the live value would report every crash as a nudge.
    const auto remembered = self->contactVelocity_.find(slot);
    if (remembered != self->contactVelocity_.end()) {
        for (int c = 0; c < 3; ++c) v[c] = remembered->second[c];
    } else if (self->physics_ && slot >= 0) {
        self->physics_->GetScriptBodyVelocity(slot, v);
    }
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, v[c]);
    lua_pushnumber(L, std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    return 4;
}

// INP.GetTimeMultiplier / SetTimeMultiplier - the game-speed scale.
//
// 0x1011ce00 pushes a float straight off the engine (GEngine+0x100); F3 and F4
// halve and double it in Game.lua, so it is a slow-motion knob whose normal
// value is 1. It matters here because StdOnCollision multiplies the impact
// speed by it: unbound, `vl * nil` throws inside the handler rather than
// merely reading wrong, which would take the whole collision path down.
int ScriptEngine::L_INP_GetTimeMultiplier(lua_State* L) {
    lua_pushnumber(L, From(L)->timeMultiplier_);
    return 1;
}

int ScriptEngine::L_INP_SetTimeMultiplier(lua_State* L) {
    ScriptEngine* self = From(L);
    const float v = float(luaL_optnumber(L, 1, 1.0));
    if (v > 0.f) self->timeMultiplier_ = v;
    return 0;
}

// One frame's contacts, turned into messages.
//
// Reported per SIDE, not per contact: a crate hit by a bolt and a bolt hitting
// a crate are two different scripts asking two different questions, and each
// only hears about it if it asked (EnableCollisions) and its own cooldown has
// run out. That mirrors the original, where the callback lives on the
// PhysicsObject rather than on the pair.
void ScriptEngine::TickCollisions(float dt) {
    for (auto& kv : entities_)
        if (kv.second.collisionCooldown > 0.f) kv.second.collisionCooldown -= dt;

    if (!physics_ || !host_) return;
    contactVelocity_.clear();
    physics_->CollectScriptContacts(contactScratch_);
    if (contactScratch_.empty()) return;

    // What each body was doing when it hit, for the whole of this dispatch.
    // Filled before any message goes out, because a script handling the first
    // collision may ask about a body involved in the second.
    for (const ScriptContact& c : contactScratch_) {
        if (c.slotA >= 0) contactVelocity_[c.slotA] = {c.velA[0], c.velA[1], c.velA[2]};
        if (c.slotB >= 0) contactVelocity_[c.slotB] = {c.velB[0], c.velB[1], c.velB[2]};
    }

    for (const ScriptContact& c : contactScratch_) {
        // Both directions, each gated on the side that would receive it.
        for (int side = 0; side < 2; ++side) {
            const int mySlot = side == 0 ? c.slotA : c.slotB;
            const int otherSlot = side == 0 ? c.slotB : c.slotA;
            // The world is never the receiver: it has no script object to tell.
            if (mySlot < 0) continue;
            auto me = bodyToEntity_.find(mySlot);
            if (me == bodyToEntity_.end()) continue;
            Entity* e = Find(me->second);
            if (!e || !e->collisionsOn || e->collisionCooldown > 0.f) continue;

            // The engine's own strength gate, before the script's. Measured as
            // the closing speed along the contact normal, which is what "how
            // hard" means for a contact; the scripts' MinSpeedOnCollision is a
            // second, coarser test on the relative speed they compute
            // themselves.
            const float* vMe = side == 0 ? c.velA : c.velB;
            const float* vOther = side == 0 ? c.velB : c.velA;
            const float rel[3] = {vMe[0] - vOther[0], vMe[1] - vOther[1], vMe[2] - vOther[2]};
            const float closing = std::fabs(rel[0] * c.normal[0] + rel[1] * c.normal[1] +
                                            rel[2] * c.normal[2]);
            if (closing < e->collisionMinStrength) continue;

            auto other = bodyToEntity_.find(otherSlot);
            const int otherEntity = other == bodyToEntity_.end() ? 0 : other->second;

            const double args[10] = {double(me->second),
                                     c.point[0], c.point[1], c.point[2],
                                     c.normal[0], c.normal[1], c.normal[2],
                                     double(otherEntity),
                                     double(mySlot), double(otherSlot)};
            host_->PostMsg("COLLISION_WITH_OTHER_ENTITY", args, 10);
            e->collisionCooldown = e->collisionMinTime;
        }
    }
}

// ENTITY.PO_SetGrenade(e, on = true): flag 0x20 at PhysicsObject+0x74
// (0x101369b0). Grenade:OnCreateEntity sets it; the flag means UpdateEntity
// runs FixGrenadeFlight on this body every step. PO_SetMissile, which the
// same script calls next, is multiplayer only - the native (0x10136a60) does
// nothing without the NetworkDevice2 at GEngine+0xdc, which single player
// never creates - so it stays a stub.
int ScriptEngine::L_PO_SetGrenade(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->isGrenade = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    return 0;
}

// PhysicsObject::FixGrenadeFlight (0x1018d990), for every flagged body.
//
// After the step, the path from where the ENTITY was (last frame's synced
// position) to where the BODY is now is traced, up to ten times. Each hit
// posts COLLISION_WITH_OTHER_ENTITY when callbacks are on, mirrors the
// velocity and the rest of the path across the surface, and carries on from
// 2 mm past the hit. Once anything was hit the velocity is scaled by
// (1.6 - friction) - the body wrapper's friction, which is what
// PO_SetFriction sets - and the body is moved to the mirrored end point.
// Ten hits without escaping stops it dead. Constants: 0x102c852c (1.6),
// 0x102c8530 (0.002).
void ScriptEngine::TickGrenades() {
    if (!physics_) return;
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (!e.isGrenade || !e.poEnabled || e.physicsBody < 0) continue;
        if (!physics_->ScriptBodyExists(e.physicsBody)) continue;

        float end[3], vel[3];
        if (!physics_->GetScriptBodyPosition(e.physicsBody, end)) continue;
        if (!physics_->GetScriptBodyVelocity(e.physicsBody, vel)) continue;
        float start[3] = {e.pos[0], e.pos[1], e.pos[2]};

        const int exclude[1] = {e.physicsBody};
        int hits = 0;
        for (; hits < 10; ++hits) {
            PhysicsWorld::RayHit hit;
            if (!physics_->RayCast(start, end, hit, false, exclude, 1)) break;
            const float* n = hit.normal;

            if (e.collisionsOn) {
                auto other = bodyToEntity_.find(hit.bodySlot);
                const int otherEntity = other == bodyToEntity_.end() ? 0 : other->second;
                const double args[10] = {double(kv.first),
                                         hit.point[0], hit.point[1], hit.point[2],
                                         n[0], n[1], n[2],
                                         double(otherEntity),
                                         double(e.physicsBody), double(hit.bodySlot)};
                host_->PostMsg("COLLISION_WITH_OTHER_ENTITY", args, 10);
                e.collisionCooldown = e.collisionMinTime;
            }

            const float vn = 2.f * (vel[0] * n[0] + vel[1] * n[1] + vel[2] * n[2]);
            for (int c = 0; c < 3; ++c) vel[c] -= n[c] * vn;

            float rest[3];
            for (int c = 0; c < 3; ++c) rest[c] = end[c] - hit.point[c];
            const float rn = 2.f * (rest[0] * n[0] + rest[1] * n[1] + rest[2] * n[2]);
            for (int c = 0; c < 3; ++c) {
                end[c] -= n[c] * rn;
                rest[c] -= n[c] * rn;
            }
            const float len = std::sqrt(rest[0] * rest[0] + rest[1] * rest[1] +
                                        rest[2] * rest[2]);
            if (len > 0.002f)
                for (int c = 0; c < 3; ++c) rest[c] *= 0.002f / len;
            for (int c = 0; c < 3; ++c) start[c] = hit.point[c] + rest[c];
        }
        if (hits == 0) continue;
        if (hits >= 10) {
            for (int c = 0; c < 3; ++c) vel[c] = 0.f;
        } else {
            const float k = 1.6f - e.bodyFriction;
            for (int c = 0; c < 3; ++c) vel[c] *= k;
        }
        physics_->SetScriptBodyPose(e.physicsBody, end, e.rotWXYZ);
        physics_->SetScriptBodyVelocity(e.physicsBody, vel);
    }
}

}  // namespace painful
