// ScriptEngine: the animation clock, the posed skeleton and the joint natives.
#include "ScriptEngineInternal.h"

namespace painful {

namespace {

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

}  // namespace

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
// get broken here. See Docs/Reference/Animation.md.
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

namespace {

// Which slot holds this animation, or -1. A cross-fade knows the outgoing
// Animation* but not the slot that carries its movement curve.
int SlotOfAnim(const ScriptEngine::Entity& e, const Animation* anim) {
    if (!anim) return -1;
    for (size_t i = 0; i < e.animSlots.size(); ++i)
        if (e.animSlots[i].anim == anim) return int(i);
    return -1;
}

}  // namespace

// How far one animation's curve bone has travelled at `time`, on the axes that
// animation declares. Zero for a slot with no curve, which is what makes the
// blend arithmetic below work without a special case.
void ScriptEngine::CurveOffset(Entity& e, const SkeletonCache::Entry* skel, int slotIndex,
                               const std::vector<const AnimTrack*>& tracks, float time,
                               float out[3]) {
    out[0] = out[1] = out[2] = 0.f;
    if (!skel || slotIndex < 0 || size_t(slotIndex) >= e.animSlots.size()) return;
    Entity::AnimSlot& slot = e.animSlots[size_t(slotIndex)];
    if (slot.curveMask == 0 || ResolveCurveBone(slot, *skel) < 0) return;
    if (tracks.size() != skel->bones.size()) return;

    float at[3];
    if (!ComputeBonePositionAtTime(skel->bones, tracks, slot.curveBoneIndex, time, at)) return;
    static const uint32_t kAxisBit[3] = {1, 2, 4};
    for (int c = 0; c < 3; ++c)
        if (slot.curveMask & kAxisBit[c]) out[c] = at[c];
}

const std::vector<Mat4>* ScriptEngine::PosedBones(Entity& e) {
    if (e.type != kModel || e.source.empty()) return nullptr;
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel || skel->bones.empty()) return nullptr;

    // ONCE THE SOLVER HAS IT, THE ANIMATION DOES NOT. A ragdoll is not a pose
    // the clock can advance - CActor stops the clock itself by setting
    // _CurAnimLength to 99999 - so everything that asks where a bone is, from
    // the draw to GetJointPos to the limb traces, has to be answered from the
    // simulation instead.
    if (e.ragdollSlot >= 0 && e.ragdollPose.size() == skel->bones.size())
        return &e.ragdollPose;
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
        // it. The mover is what actually carries the actor (see "The mover
        // drives, the animation plays in place" in Docs/Reference/Animation.md),
        // so that travel has to come out or the mesh strides away from the
        // monster and snaps back every time the loop wraps.
        //
        // Only the axes the curve declares are removed. ETransZ takes the
        // forward travel out and deliberately leaves the vertical, so the
        // actor still bobs as it walks.
        //
        // BOTH SIDES OF A CROSS-FADE, weighted the same way the pose is.
        //
        // A blended pose contains the OUTGOING animation's bones too, and its
        // root travel is still in them. Subtracting only the incoming
        // animation's curve left the walk's accumulated stride in the blend:
        // switching to idle snapped the mesh 2.922 units in one frame and slid
        // it back over the 0.2s fade. Fading to an animation with no curve at
        // all is the common case - every walk that ends in idle - which is why
        // it read as the monster jumping whenever it stopped.
        {
            float at[3] = {0.f, 0.f, 0.f};
            CurveOffset(e, skel, e.animIndex, e.pose.tracks, e.animTime, at);
            if (e.blendFrom && blendU < 1.f) {
                float from[3] = {0.f, 0.f, 0.f};
                CurveOffset(e, skel, SlotOfAnim(e, e.blendFrom), e.blendFromTracks,
                            e.blendFromTime, from);
                for (int c = 0; c < 3; ++c)
                    at[c] = from[c] * (1.f - blendU) + at[c] * blendU;
            }
            if (at[0] != 0.f || at[1] != 0.f || at[2] != 0.f)
                for (Mat4& m : e.pose.boneWorld)
                    for (int c = 0; c < 3; ++c) m.m[12 + c] -= at[c];
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
// Ragdoll is its own system and has not been built (Docs/Reference/Animation.md stage
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

bool ScriptEngine::JointWorldRotation(Entity& e, int joint, float outWXYZ[4]) {
    const std::vector<Mat4>* bones = PosedBones(e);
    if (!bones || joint < 0 || size_t(joint) >= bones->size()) return false;
    const Mat4& m = (*bones)[size_t(joint)];
    float rot[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) rot[r * 3 + c] = m.m[r * 4 + c];
    Normalize3x3Rows(rot);
    float boneQuat[4];
    EngineRot9ToQuat(rot, boneQuat);
    EngineQuatMul(e.rotWXYZ, boneQuat, outWXYZ);
    return true;
}

int ScriptEngine::L_MDL_GetJointRotation(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int joint = int(lua_tonumber(L, 2));

    float quat[4] = {1, 0, 0, 0};
    if (e) self->JointWorldRotation(*e, joint, quat);
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


// MDL.SetMeshVisibility(entity, meshName, on)
//
// One named mesh of one model, shown or hidden. 0x1013c780 reads the handle,
// the name and a BOOL DEFAULTING TO FALSE, and skips anything that is not a
// Model (it tests the entity's type against 4 before touching it).
//
// The Painkiller's alt fire hides nine of them - polySurfaceShape28, 49, 46,
// 50, 47, 44, pCylinderShape14, 29 and kolekShape - so the gun reads as empty
// while its head is away, and BackHeadSFX shows them again when it returns.
// Monsters use the same call to drop gib parts, and the menu to swap heads.
int ScriptEngine::L_MDL_SetMeshVisibility(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e || e->type != kModel) return 0;
    const char* mesh = luaL_optstring(L, 2, "");
    if (!mesh || !*mesh) return 0;
    const bool on = lua_toboolean(L, 3) != 0;
    // Remembered on the entity as well as pushed at the renderer: a script
    // instance is rebuilt whenever its model is reassigned, and the hidden
    // set has to survive that or the blades come back on their own.
    e->hiddenMeshes[mesh] = on;
    if (self->renderer_ && e->rendererInstance >= 0)
        self->renderer_->SetScriptMeshVisibility(e->rendererInstance, mesh, on);
    return 0;
}

}  // namespace painful
