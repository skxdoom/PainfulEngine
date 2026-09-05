// ScriptEngine: WORLD.Explosion2 - area damage and blast impulse.
//
// Every explosion in the game arrives here. `Explosion()` in Main/Utils.lua is
// the single funnel (91 call sites) and it calls this in single player and
// MultiplayerExplosion otherwise, so grenades, rockets, barrels, exploding cars
// and the bosses' shockwaves all land on one native.
//
// The engine does NOT deal the damage. It collects what the blast reached and
// posts one EXPLOSION message per entity; Game_GetMsg (Game.lua:1249) looks the
// entity up in EntityToObject and calls obj:OnDamage. Same division as the
// weapon traces - see the Stage 8 note in Docs/Plan.md.
//
// Recovered from PhysicsWorld::Explosion (0x1019BBD0, which forwards to
// FUN_101B79F0). Falloff, constants and the parts not ported are in
// Docs/Reference/Physics.md.

#include "ScriptEngineInternal.h"

namespace painful {

namespace {

// sin((1 - d/range) * pi/2): 1.0 at the centre, 0.0 at the rim, and it holds
// its strength further out than a linear ramp would. The multiplier is the
// float at 0x102C86E4, which reads 1.5707964.
//
// NOT the same curve as PhysicsWorld::SelfExplosion (0x10197D10), which is
// plain (1 - d/range). Two functions, two laws; this is the one the scripts
// reach through WORLD.Explosion2.
float ExplosionFalloff(float distance, float range) {
    if (range <= 0.f) return 0.f;
    const float t = 1.f - distance / range;
    if (t <= 0.f) return 0.f;
    return std::sin(t * float(kPi) * 0.5f);
}

// ECollisionGroups.Noncolliding (7) and 12 measure from the entity's BBOX
// CENTRE rather than the body position - 0x102AE5B0 is the 0.5 that halves
// (lo + hi). Everything else measures from the body.
bool MeasuredFromBounds(int collisionGroup) {
    return collisionGroup == 7 || collisionGroup == 12;
}

}  // namespace

// WORLD.Explosion2(x, y, z, strength, range, clientID, attackType, damage)
//
// Argument order is read straight off 0x1011ECF0: three floats for the centre,
// strength, range, an int clientID (default -1), an int attackType, and the
// damage. The mangled export agrees:
// ?Explosion@PhysicsWorld@@QAEXVVector@@MMHHM@Z = (Vector, float, float, int,
// int, float).
int ScriptEngine::L_WORLD_Explosion2(lua_State* L) {
    ScriptEngine* self = From(L);
    const float centre[3] = {float(luaL_optnumber(L, 1, 0)), float(luaL_optnumber(L, 2, 0)),
                             float(luaL_optnumber(L, 3, 0))};
    const float strength = float(luaL_optnumber(L, 4, 0));
    const float range = float(luaL_optnumber(L, 5, 0));
    const double killer = luaL_optnumber(L, 6, -1);
    const double attackType = luaL_optnumber(L, 7, 0);
    const float damage = float(luaL_optnumber(L, 8, 0));
    self->Explosion(centre, strength, range, killer, attackType, damage);
    return 0;
}

void ScriptEngine::Explosion(const float centre[3], float strength, float range,
                             double killer, double attackType, float damage) {
    if (range <= 0.f) return;

    // One id per blast. Game_GetMsg stores it on the object as `_Exploded` and
    // skips an entity it has already seen, so two entities hit by the SAME
    // explosion must share it and two explosions must not.
    const double explosionId = double(++explosionCounter_);

    // Collected first, posted after. A handler can kill an entity, spawn a
    // ragdoll or start another explosion, and mutating entities_ while walking
    // it invalidates the iterator.
    struct Reached {
        int handle;
        float falloff;
    };
    std::vector<Reached> reached;

    // Pinned active meshes within range + their radius come loose first
    // (FUN_101B79F0's second pass), so the impulse below lands on them too.
    if (physics_) {
        static std::vector<int> released;
        physics_->UnpinActiveMeshesNear(centre, range, released);
        // A destructible's twin in range is swapped for its pieces before the
        // impulse loop, so they take the blast like the original's do.
        ReleaseTwins(released, centre);
    }
    std::vector<float> parts;
    for (auto& kv : entities_) {
        Entity& e = kv.second;

        // A CORPSE, OR A GIB. In Havok every limb is its own body in the same
        // world, and FUN_101B79F0 hands a ragdoll to Ragdoll::SelfExplosion's
        // law - the strength shared across the limbs with a linear falloff -
        // and posts ONE message with the nearest limb's sine falloff. The
        // ragdoll's own flag gates both (FUN_101B0DC0 returns before touching
        // a ragdoll not moved by explosions), which is what lets a fresh gib
        // sit out the blast that made it. Docs/Reference/Physics.md.
        if (e.ragdollSlot >= 0 && physics_ && physics_->RagdollActive(e.ragdollSlot)) {
            if (!e.ragdollMovedByExplosions) continue;
            physics_->RagdollPartPositions(e.ragdollSlot, parts);
            float nearest = range;
            for (size_t p = 0; p + 2 < parts.size(); p += 3) {
                float d2 = 0.f;
                for (int c = 0; c < 3; ++c) {
                    const float k = parts[p + c] - centre[c];
                    d2 += k * k;
                }
                nearest = std::min(nearest, std::sqrt(d2));
            }
            if (nearest >= range) continue;
            reached.push_back({kv.first, ExplosionFalloff(nearest, range)});
            physics_->RagdollSelfExplosion(e.ragdollSlot, centre, strength, range);
            continue;
        }

        if (e.physicsBody < 0 || !e.poEnabled) continue;

        float at[3];
        if (MeasuredFromBounds(e.collisionGroup) && physics_ != nullptr) {
            float lo[3], hi[3];
            if (physics_->ScriptBodyBounds(e.physicsBody, lo, hi))
                for (int c = 0; c < 3; ++c) at[c] = (lo[c] + hi[c]) * 0.5f;
            else
                for (int c = 0; c < 3; ++c) at[c] = e.pos[c];
        } else {
            for (int c = 0; c < 3; ++c) at[c] = e.pos[c];
        }

        float away[3];
        for (int c = 0; c < 3; ++c) away[c] = at[c] - centre[c];
        const float distance =
            std::sqrt(away[0] * away[0] + away[1] * away[1] + away[2] * away[2]);
        if (distance >= range) continue;

        const float falloff = ExplosionFalloff(distance, range);
        reached.push_back({kv.first, falloff});

        // The push. A body that declines to be moved still takes the damage -
        // PO_SetMovedByExplosions governs the impulse alone, which is why a
        // grenade can turn it off for itself and still hurt what it lands on.
        if (!e.movedByExplosions || !physics_ || falloff <= 0.f) continue;
        // Dead centre has no direction to push along. 0.001 is the engine's own
        // floor for this, the double at 0x102AE578.
        if (distance < 0.001f) continue;
        // A FORCE, spent over one step - not an impulse.
        //
        // The engine accumulates into PhysicsObject::EffectForce and spends the
        // total once per step in EffectForces(), which is Havok's applyForce:
        // the body gains force * dt of momentum, not `force`. Taken raw, a
        // shipped 3200-strength blast is 60x too strong and throws a 200 kg
        // barrel the length of the level (measured: 39 units).
        //
        // ASSUMED, and this is the number to retune if blasts feel wrong. The
        // evidence is the name and the accumulate-then-spend pattern, not a
        // decompiled multiply. Docs/Reference/Physics.md.
        const float scale = falloff * strength / distance;
        const float impulse[3] = {away[0] * scale, away[1] * scale, away[2] * scale};
        physics_->AddScriptBodyImpulse(e.physicsBody, at, impulse);
    }

    for (const Reached& r : reached) {
        // The point is the blast CENTRE, not the contact: OnDamage takes it as
        // (x, y, z) at parameters 4..6 and the actors use it for the direction
        // to fall in.
        const double args[8] = {double(r.handle), centre[0], centre[1], centre[2],
                                explosionId,      killer,    attackType,
                                double(damage * r.falloff)};
        host_->PostMsg("EXPLOSION", args, 8);
    }
}

// ENTITY.PO_SetMovedByExplosions(e, on)
//
// PhysicsObject::IsMovedByExplosions (0x1001E330) is a plain setter over one
// bool. 71 call sites, and the blast above is its only reader.
int ScriptEngine::L_PO_SetMovedByExplosions(lua_State* L) {
    ScriptEngine* self = From(L);
    if (Entity* e = self->Find(HandleArg(L, 1)))
        e->movedByExplosions = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    return 0;
}

// ENTITY.PO_SetPinned(e, on) / PO_IsPinned(e)
//
// CObject:PO_Create pins anything whose template says Pinned, and a level
// action releases it: C1L3_Catacombs' blockade is `Pin:C1L3_Blokada_001,false`
// plus `SetImmortal:...,false` on an ambush box, which is what turns the stones
// from scenery into something the dynamite crates can break.
int ScriptEngine::L_PO_SetPinned(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || !self->physics_ || e->physicsBody < 0) return 0;
    const bool pinned = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    e->bodyPinned = pinned;
    self->physics_->SetScriptBodyPinned(e->physicsBody, pinned);
    return 0;
}

int ScriptEngine::L_PO_IsPinned(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e != nullptr && self->physics_ != nullptr && e->physicsBody >= 0 &&
                           self->physics_->IsScriptBodyPinned(e->physicsBody));
    return 1;
}

}  // namespace painful
