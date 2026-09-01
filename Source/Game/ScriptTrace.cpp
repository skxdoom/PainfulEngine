// ScriptEngine: world line traces and the intersection solver membership.

#include "ScriptEngineInternal.h"

namespace painful {

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
    const bool gotWorld = self->TraceRay(from, to, hit, staticOnly);

    // THE LIMB BOXES ARE THE SHOOTING SHAPE; the world trace is everything
    // else. Whichever is nearer is what the shot hit, and TraceLimbs is handed
    // the world hit's distance so it can only report something in FRONT of it -
    // a shot that stops at a wall must not reach the monster behind.
    //
    // LineTraceFixedGeom never consults them. It asks about the world mesh
    // alone, and the actors use it for their ground and step probes, where
    // finding each other's limbs would be noise.
    LimbHit limb;
    const bool gotLimb =
        !staticOnly && self->TraceLimbs(from, to, gotWorld ? hit.distance : -1.f, limb);

    // WATER IS NOT IN THE COLLIDABLE WORLD and cannot be: every shipped water
    // object is named `noclip`, which is what lets you swim through it. It is
    // still something a shot HITS, though - a rocket splashes rather than
    // exploding - so the surface is tested here, and wins when it is nearer
    // than the solid hit. Without this every `if ENTITY.IsWater(e)` in the
    // weapon scripts is unreachable, whatever that native answers.
    float waterT = 0.f;
    int waterEntity = 0;
    bool gotWater = self->TraceWater(from, to, waterT, waterEntity);
    float waterDistance = 0.f;
    if (gotWater) {
        const float span[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        waterDistance =
            waterT * std::sqrt(span[0]*span[0] + span[1]*span[1] + span[2]*span[2]);
        const float nearest = gotWorld ? hit.distance : 1e30f;
        const float nearestLimb = gotLimb ? limb.distance : 1e30f;
        if (waterDistance > nearest || waterDistance > nearestLimb) gotWater = false;
    }
    if (gotWater) {
        // The surface itself: a hit with no body, an upward normal, and the
        // water object's own entity so IsWater can recognise it.
        lua_pushboolean(L, 1);
        lua_pushnumber(L, waterDistance);
        for (int c = 0; c < 3; ++c)
            lua_pushnumber(L, from[c] + (to[c] - from[c]) * waterT);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, -1);
        lua_pushnumber(L, waterEntity);
        return 10;
    }

    const bool got = gotWorld || gotLimb;

    // A MISS RETURNS ONE VALUE. The engine pushes the boolean and stops
    // (0x1012cea0: PushBool(0) then return 1), so a script reading
    //     local b,d,tx,ty,tz,nx,ny,nz,he,e = WORLD.LineTrace(...)
    // gets false and nine nils. We used to return all ten every time, with 0
    // for the entity and -1 for the body - and 0 IS TRUE IN LUA. Stake:Tick
    // asks `if e then` right after its trace, so every clear shot read as an
    // impact: the stake killed itself on its first tick and left the entity
    // flying on as a ghost that passed through walls. The same `if e then`
    // appears in every projectile script, so this was all of them.
    lua_pushboolean(L, got);
    if (!got) return 1;

    // A LIMB HIT REPORTS THE LIMB'S HANDLE, NOT A BODY SLOT. That ninth value
    // is what the scripts carry into OnDamage as `he` and hand straight back
    // to PHYSICS.GetHavokBodyInfo, which is where the bone comes from: the
    // Tank doubles damage on `b1` / `b2`, the Gladiator refuses it on
    // `sword1`. Without a handle that names a bone, every one of those tests
    // reads the same on a shot to the head as on a shot to the foot.
    if (gotLimb) {
        lua_pushnumber(L, limb.distance);
        for (int c = 0; c < 3; ++c) lua_pushnumber(L, limb.point[c]);
        for (int c = 0; c < 3; ++c) lua_pushnumber(L, limb.normal[c]);
        lua_pushnumber(L, self->LimbHandle(limb.entity, limb.joint));
        lua_pushnumber(L, limb.entity);
        return 10;
    }

    lua_pushnumber(L, hit.distance);
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, hit.point[c]);
    for (int c = 0; c < 3; ++c) lua_pushnumber(L, hit.normal[c]);
    lua_pushnumber(L, hit.bodySlot);
    // A WORLD HIT REPORTS ENTITY 0, NOT NIL - see IsFixedMesh, which answers
    // true for exactly that handle.
    //
    // The engine does have a push-nil branch here, for a body whose owner is
    // null. That is not the world: in PainEngine level geometry is made of
    // entities (the scripts ask GetType(e) == ETypes.Mesh and IsFixedMesh(e)
    // about what they hit), so a wall trace there yields a real handle. Ours
    // is one anonymous collision body, and 0 is the stand-in - truthy, which
    // is what the scripts need, because the whole impact-and-nail path in
    // Stake:Tick sits inside `if e then`. Pushing nil here skipped it, and the
    // stake registered a hit on a wall and flew straight on through it.
    lua_pushnumber(L, self->EntityForBody(hit.bodySlot));
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
    // The scripts' own exclusions, plus every movement body the limb boxes
    // have taken over from.
    //
    // A monster's walking shape is three stacked spheres wider than its arms,
    // so leaving it in the trace would swallow the very shots the limbs exist
    // to answer: the boxes would be derived, posed, drawn and never reached.
    // It stays in the simulation - it is still what you bump into and cannot
    // stand inside - it just stops being what a shot tests against.
    //
    // staticOnly never needs it: LineTraceFixedGeom is the world mesh alone
    // and no script body is in that layer to begin with.
    const int* exclude = excludedSlots_.empty() ? nullptr : excludedSlots_.data();
    size_t count = excludedSlots_.size();
    if (!staticOnly && !limbShadowed_.empty()) {
        traceExclude_ = excludedSlots_;
        traceExclude_.insert(traceExclude_.end(), limbShadowed_.begin(), limbShadowed_.end());
        exclude = traceExclude_.data();
        count = traceExclude_.size();
    }
    return physics_->RayCast(from, to, hit, staticOnly, exclude, count);
}

int ScriptEngine::EntityForBody(int bodySlot) const {
    if (bodySlot < 0) return 0;                  // the world
    auto it = bodyToEntity_.find(bodySlot);
    return it == bodyToEntity_.end() ? 0 : it->second;
}

// ENTITY.RemoveFromIntersectionSolver(e) / AddToIntersectionSolver(e) - take
// an entity out of the traces and put it back. Always bracketed, so this has
// to be exact: leaking a Remove would leave something permanently unhittable.
//
// THE ENTITY HAS TWO TRACE SWITCHES, NOT ONE. The engine keeps a PhysicsObject
// at +0xac and a Ragdoll at +0x7b8, each with its own
// EnableLineTraceCollision, and the two script pairs are not the same call:
//
//   AddToIntersectionSolver       (0x101349a0)  body AND ragdoll
//   AddRagdollToIntersectionSolver(0x10134830)  ragdoll ONLY
//
// (and the Remove pair, symmetrically, at 0x101348e0 and 0x10134630). Aliasing
// them was harmless while a monster was a single sphere and there was only one
// shape to hide. It is not any more: the scripts bracket a shot with the
// RAGDOLL pair, and putting that through the body flag would hide the walking
// shape while leaving the limbs shootable - exactly backwards.
void ScriptEngine::SetSolverBody(Entity& e, bool on) {
    if (e.inSolver == on) return;              // idempotent: no doubled entries
    e.inSolver = on;
    if (e.physicsBody < 0) return;
    if (on) {
        auto& v = excludedSlots_;
        v.erase(std::remove(v.begin(), v.end(), e.physicsBody), v.end());
    } else {
        excludedSlots_.push_back(e.physicsBody);
    }
}

int ScriptEngine::L_RemoveFromIntersectionSolver(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    // The engine's own meaning (0x101348e0): line-trace collision off, on the
    // body and the ragdoll both.
    self->SetSolverBody(*e, false);
    e->ragdollInSolver = false;

    // AND the projectile reading, which this native has carried on its own
    // until now. THESE WERE TWO SEPARATE FUNCTIONS REGISTERED UNDER ONE NAME,
    // and lua_rawset means the later one won - so the trace half above has
    // never actually run, and RemoveFromIntersectionSolver has only ever
    // marked projectiles. Merging them is what restores the half the scripts
    // are really asking for; nothing is taken away.
    //
    // The projectile half stays because it is load-bearing and cannot simply
    // move to PO_Create: the rocket is made in the PARTICLES group, which
    // CreateScriptBody deliberately does NOT treat as driven because shell
    // casings live there too and are meant to tumble. Rocket:OnCreateEntity
    // asks for it outright and never pairs the call with an Add, which is the
    // signal - but it is a signal only over time, and this native cannot see
    // it. Left as found; that is its own question.
    e->isProjectile = true;
    if (self->physics_ && e->physicsBody >= 0)
        self->physics_->MakeScriptBodyNonColliding(e->physicsBody);
    return 0;
}

int ScriptEngine::L_AddToIntersectionSolver(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    self->SetSolverBody(*e, true);
    e->ragdollInSolver = true;
    return 0;
}

// The ragdoll half alone. In the engine both of these are also gated on the
// entity being a MODEL (ETypes.Model = 4 - the gate is on the render type, not
// on being a CActor) and having a ragdoll; here an entity with no limb boxes
// simply has nothing for the flag to govern, which comes to the same thing.
int ScriptEngine::L_RemoveRagdollFromIntersectionSolver(lua_State* L) {
    Entity* e = From(L)->Find(HandleArg(L, 1));
    if (e) e->ragdollInSolver = false;
    return 0;
}

int ScriptEngine::L_AddRagdollToIntersectionSolver(lua_State* L) {
    Entity* e = From(L)->Find(HandleArg(L, 1));
    if (e) e->ragdollInSolver = true;
    return 0;
}

// PHYSICS.GetHavokBodyInfo(he) -> type [, entity [, joint ]]
//
// THE NUMBER OF RETURN VALUES IS PART OF THE CONTRACT. The engine
// (0x101291a0) branches on what PhysicsEngine::RigidBodyInfo made of the body
// and pushes a different count for each: one value for a body it does not
// recognise, two for a plain physics object, three for a ragdoll limb.
//
// That is why every weak-point script reads `local t,e,j` and then asks
// `if j then` - a hit on something that is not a limb has to leave the joint
// NIL, not -1. Returning three values with j = -1 would make Apoc_zombie's
// `if j then` true for a shot at a barrel and send it looking up bone -1.
int ScriptEngine::L_PHYSICS_GetHavokBodyInfo(lua_State* L) {
    ScriptEngine* self = From(L);
    // A missing or non-numeric handle is NOT body slot 0. lua_tonumber would
    // quietly make it one, and slot 0 is a real body someone owns.
    if (!lua_isnumber(L, 1)) {
        lua_pushnumber(L, 0);
        return 1;
    }
    const int handle = int(lua_tonumber(L, 1));

    int entity = 0, joint = -1;
    if (self->LimbFromHandle(handle, entity, joint)) {
        lua_pushnumber(L, 2);               // a ragdoll limb: it has a joint
        lua_pushnumber(L, entity);
        lua_pushnumber(L, joint);
        return 3;
    }

    const int owner = (handle >= 0) ? self->EntityForBody(handle) : 0;
    if (owner != 0) {
        lua_pushnumber(L, 1);               // a plain physics object
        lua_pushnumber(L, owner);
        return 2;
    }

    lua_pushnumber(L, 0);                   // the world, or nothing we know
    return 1;
}

// MDL.GetJointFromHavokBody(e, he) -> joint index, or -1.
//
// The engine (0x1012d320) resolves the entity, checks it is a MODEL (type 4 is
// ETypes.Model, which is our kModel - the gate is the RENDER type, not the
// script class) and that it HAS a ragdoll, and then asks that ragdoll which of
// its own bodies this is. A handle belonging to a different monster answers -1
// rather than leaking a joint index across actors, which matters because the
// projectile scripts call this with `e_other` and a handle from the same
// collision, and would otherwise trust a bone index from the wrong skeleton.
int ScriptEngine::L_MDL_GetJointFromHavokBody(lua_State* L) {
    ScriptEngine* self = From(L);
    const int owner = HandleArg(L, 1);
    int entity = 0, joint = -1;
    if (!lua_isnumber(L, 2) ||
        !self->LimbFromHandle(int(lua_tonumber(L, 2)), entity, joint) || entity != owner) {
        lua_pushnumber(L, -1);
        return 1;
    }
    lua_pushnumber(L, joint);
    return 1;
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

    e->viewAttached = true;
    for (int c = 0; c < 3; ++c) {
        e->viewOffset[c] = float(luaL_optnumber(L, c + 2, 0));
        e->viewAngles[c] = float(luaL_optnumber(L, c + 5, 0));
    }
    self->PlaceViewAttached(*e);
    return 0;
}

// The camera-space placement itself, shared by the native above and by
// UpdateViewAttached.
void ScriptEngine::PlaceViewAttached(Entity& entity) {
    Entity* e = &entity;
    const float lx = e->viewOffset[0], ly = e->viewOffset[1], lz = e->viewOffset[2];

    // Camera space is +X right, +Y up, -Z forward. Placing the offset from
    // the camera's own basis keeps this independent of how a rotation is
    // spelled as a quaternion, and that basis is the one already driving both
    // the view matrix and the player's movement.
    const float cp = std::cos(camPitch_), sp = std::sin(camPitch_);
    const float cy = std::cos(camYaw_), sy = std::sin(camYaw_);
    const float fwd[3] = {cy * cp, sp, sy * cp};
    const float right[3] = {-sy, 0.f, cy};
    // up = right x forward, which tilts with the pitch as the view does.
    const float up[3] = {right[1] * fwd[2] - right[2] * fwd[1],
                         right[2] * fwd[0] - right[0] * fwd[2],
                         right[0] * fwd[1] - right[1] * fwd[0]};
    // Anchored to the DISPLACED eye, which is the one actually rendered:
    // TakeCameraPose hands the renderer camPos_ + camDisplacement_, and
    // CPlayer drives the head bob through CAM.SetPositionDisplacement. Hung off
    // camPos_ alone the weapon sits still while the view bobs around it, and
    // the difference reads as the gun swinging hard with every step.
    for (int c = 0; c < 3; ++c)
        e->pos[c] = camPos_[c] + camDisplacement_[c] + right[c] * lx + up[c] * ly -
                    fwd[c] * lz;

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
    EngineEulerToQuat(e->viewAngles[0], -e->viewAngles[1], e->viewAngles[2], localQuat);
    EngineQuatToRot9(localQuat, localRot);
    // Row-vector order: the weapon's own rotation first, then the camera's.
    EngineRot9Mul(localRot, camRot, worldRot);
    EngineRot9ToQuat(worldRot, e->rotWXYZ);

    SyncPose(*e);
}

void ScriptEngine::UpdateViewAttached() {
    for (auto& kv : entities_)
        if (kv.second.viewAttached) PlaceViewAttached(kv.second);
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


}  // namespace painful
