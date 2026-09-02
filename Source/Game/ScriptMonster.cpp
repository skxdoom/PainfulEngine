// ScriptEngine: monster body sizing and the per-frame monster bookkeeping.
//
// The walking itself is PhysicsWorld::StepCharacters: a monster is a dynamic
// body the physics step re-commands every tick, exactly as PhysicsObject::Tick
// does in Engine.dll. What is left here is what the frame needs to know about
// each actor - which bodies the limb boxes shadow, the test hooks - and the
// rule that sizes the body. Docs/Reference/MonsterMovement.md.

#include "ScriptEngineInternal.h"

namespace painful {

// The monster's BODY: three stacked spheres, standing on its soles. k is the
// sizer's working unit (0.2 * bodyScale) and rootOffsetY where the stack's
// origin sits above the entity position.
bool ScriptEngine::MonsterBodyScale(Entity& e, float& k, float& rootOffsetY) {
    // THE RIG SIZES THE BODY, NOT THE MESH BOUNDS.
    //
    // PhysicsWorld::CreatePhysicsObject (0x101999F0) looks up the joint named
    // "ROOOT" and, when the model has one, sizes and places the shape from it
    // alone:
    //
    //     param_5 = (root.y - entity.y) * 0.909090;   // 10/11
    //     FUN_101b3e20(&local_78, param_5, bodyType, group);
    //     local_78 = -root.x;  local_74 = -root.y;  local_70 = -root.z;
    //
    // One scalar out of the skeleton drives the whole shape, and the shape is
    // then offset by the NEGATED root position. No mesh extents are consulted
    // anywhere in that path - which is why sizing from them, as this used to,
    // put the body in a different place on every rig depending on where its
    // author had left the origin.
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel || skel->bones.empty()) return false;

    // THE HIP JOINT, WHATEVER THIS RIG CALLS IT.
    //
    // The engine looks up "ROOOT", and six of ten shipped rigs have exactly
    // that. The other four spell the same joint differently and sit it at the
    // same kind of height:
    //
    //     zombie      root      y = 8.59
    //     vamp_small  root      y = 6.43
    //     raven       root      y = 2.37   (under a big_root at the origin)
    //
    // So it is one joint under two names, not two different things - and
    // matching only the first spelling left those rigs with no measure at all,
    // which is why they were the ones still sunk through the floor. "ROOOT"
    // wins where both exist; raven is why "root" is preferred over bone 0,
    // whose big_root sits at the origin and measures nothing.
    int root = -1;
    for (const char* name : {"ROOOT", "root"}) {
        for (size_t i = 0; i < skel->bones.size(); ++i)
            if (EqualsCI(skel->bones[i].name, name)) { root = int(i); break; }
        if (root >= 0) break;
    }
    if (root < 0 || size_t(root) >= skel->bindWorld.size()) return false;

    // The root in MODEL space; the engine's (root.y - entity.y) is the same
    // quantity once the entity's own scale is applied, since the root is
    // measured from the entity's origin.
    float rootPos[3];
    skel->bindWorld[size_t(root)].TransformPoint(0.f, 0.f, 0.f, rootPos);

    // ROOOT MARKS THE HIP, AND THE MEASURE IS ITS HEIGHT ABOVE THE SOLES.
    //
    // Read across the rigs, the joint's own translation is not comparable -
    // banshee has it at 8.34 and evilmonkv2 at 0.00 - because the two put the
    // model ORIGIN in different places: banshee's is at the feet, evilmonkv2's
    // at mid-body. Subtract the model's lowest point and they agree:
    //
    //     banshee     8.34 - (-2.84) = 11.18
    //     nun         8.12 - (-3.79) = 11.91
    //     evilmonkv2  0.00 - (-12.80) = 12.80
    //     DevilMonkv2 0.00 - (-12.85) = 12.85
    //
    // One number for a humanoid, whatever its author did with the origin. That
    // is the quantity the engine's (root.y - entity.y) is after, and taking it
    // from the origin instead gave 0 for half the bestiary.
    const float hipAboveSoles = (rootPos[1] - skel->lo[1]) * e.scale;
    if (hipAboveSoles <= 0.f) return false;

    // SCALED SO THE SPHERES SPAN THE MODEL.
    //
    // Deriving k from the hip height is what the engine's
    // (root.y - entity.y) * 10/11 appears to do, and it is right for a rig
    // whose root sits at a humanoid hip - about 0.53 of total height. It is
    // wrong wherever a rig disagrees, and they do:
    //
    //     banshee   root at 0.70 of height -> body came out 1.30x the model
    //     vamp_v2   root at 0.245          -> body came out 0.46x
    //
    // The error tracked that ratio exactly, which is the tell: the reference
    // the engine measures from (Entity+0x58) is NOT the soles, and until it is
    // identified the hip is the wrong thing to scale by.
    //
    // The shape's own geometry gives a reference that cannot drift. The three
    // spheres run from -4.8k to +5.5k, so k = height / 10.3 makes the body span
    // the model on every rig by construction. The LAYOUT is still the engine's;
    // only what sets its size is ours.
    const float modelHeight = (skel->hi[1] - skel->lo[1]) * e.scale;
    if (modelHeight <= 0.f) return false;
    k = modelHeight / 10.3f;
    // Placed from the SOLES, for the same reason: the lowest sphere reaches
    // 4.8k below the offset point, so putting the offset that far above the
    // model's lowest point stands the body on the ground. Anchoring to the
    // root instead inherited the root's own inconsistency, which is what put
    // the hip-origin rigs through the floor.
    rootOffsetY = skel->lo[1] * e.scale + 4.8f * k;
    return k > 0.f;
}

void ScriptEngine::TickMonsters(float dt) {
    if (dt <= 0.f || !physics_) return;

    // PAINFUL_MONSTER_TRACE=<frame> dumps, once, where each monster's body,
    // floor point and soles sit against the geometry below. None of it is
    // reachable from Lua, which is why a script-side trace cannot answer "is
    // this monster sunk".
    static int traceTick = 0;
    ++traceTick;
    const char* traceAt = std::getenv("PAINFUL_MONSTER_TRACE");
    const bool dumpGround = traceAt && traceTick == std::atoi(traceAt);

    // Rebuilt every frame: which movement bodies the limb boxes have taken
    // over from, for TraceRay. Rebuilt rather than tracked at creation because
    // it depends on the entity's MODEL - a script can give an actor a
    // different one, and a body slot outlives that change.
    limbShadowed_.clear();

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
        if (e.ragdollSlot >= 0) continue;

        // An actor whose .rde gives it limbs is shot at through those, so its
        // walking body stops answering traces. One that has none keeps it -
        // otherwise a monster with no ragdoll definition becomes unshootable.
        const std::vector<LimbBounds>* shootable = Hitboxes(e.source);
        if (shootable && !shootable->empty()) limbShadowed_.push_back(e.physicsBody);

        if (dumpGround) {
            const SkeletonCache::Entry* s = skeletons_.Get(e.source);
            const float soles = e.pos[1] + (s ? s->lo[1] * e.scale : 0.f);
            float floorPos[3] = {e.pos[0], e.pos[1], e.pos[2]};
            float normal[3];
            physics_->CharacterFloorPos(e.physicsBody, floorPos);
            const bool onFloor = physics_->CharacterOnFloor(e.physicsBody, normal);
            PhysicsWorld::RayHit hit;
            const float from[3] = {e.pos[0], e.pos[1] + 1.f, e.pos[2]};
            const float to[3] = {e.pos[0], e.pos[1] - 30.f, e.pos[2]};
            const bool got = physics_->RayCast(from, to, hit, true);
            float vel[3] = {0, 0, 0};
            physics_->GetScriptBodyVelocity(e.physicsBody, vel);
            LogInfo("MONSTER %-12s pos=%8.3f scale=%.3f lo=%7.2f floorPos=%8.3f floor=%8.3f "
                    "soles=%8.3f soleGap=%+.3f onFloor=%d n=(%.2f %.2f %.2f) vy=%+.3f",
                    e.source.c_str(), e.pos[1], e.scale, s ? s->lo[1] : 0.f, floorPos[1],
                    got ? hit.point[1] : 0.f, soles, got ? soles - hit.point[1] : 0.f,
                    onFloor ? 1 : 0, normal[0], normal[1], normal[2], vel[1]);
        }
    }
}


}  // namespace painful
