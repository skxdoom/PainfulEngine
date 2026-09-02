// ScriptEngine: monster body sizing and the per-frame monster bookkeeping.
//
// The walking itself is PhysicsWorld::StepCharacters: a monster is a dynamic
// body the physics step re-commands every tick, exactly as PhysicsObject::Tick
// does in Engine.dll. What is left here is what the frame needs to know about
// each actor - which bodies the limb boxes shadow, the test hooks - and the
// rule that sizes the body. Docs/Reference/MonsterMovement.md.

#include "ScriptEngineInternal.h"
#include <set>

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

    // THE ENGINE'S RULE, NOW THAT ITS REFERENCE IS IDENTIFIED.
    //
    // Entity+0x54 is the entity's LOCAL BoundingBox (RenderLocalBoundingBox
    // 0x101D0A90 draws it), so the (root.y - Entity+0x58) in
    // CreatePhysicsObject is the hip's height above the model's lowest
    // point. bodyScale = that * 10/11, and the sizer's unit is 0.2 * bodyScale.
    // The stack is built about the hip: CreatePhysicsObject places the body at
    // entity + root and keeps -root as the pivot.
    //
    // This is NOT a body that spans the model. On a monk the lowest sphere
    // ends 0.19 above the soles and the top one clears the head; on a rig
    // whose root sits high (banshee, 0.70 of height) the stack is 1.3x the
    // model, on one whose root sits low (vamp, 0.245) it is half - and that
    // is what the player walks into in the original. An earlier version here
    // sized k = height / 10.3 and stood the stack on the soles, which made
    // every rig the same relative size and put its lowest sphere 0.19 lower.
    // Per-rig numbers: Docs/Reference/MonsterMovement.md.
    k = 0.2f * hipAboveSoles * (10.f / 11.f);
    rootOffsetY = rootPos[1] * e.scale;
    {
        static std::set<std::string> logged;
        if (logged.insert(e.source).second) {
            const float height = (skel->hi[1] - skel->lo[1]) * e.scale;
            LogInfo("monster body %-14s height %.2f hip %.2f  k %.3f (was %.3f)  radii %.2f/%.2f/%.2f  "
                    "bottom %+.2f above soles, top %+.2f above head",
                    e.source.c_str(), height, hipAboveSoles, k, height / 10.3f, 2.6f * k, 3.0f * k,
                    1.5f * k, hipAboveSoles - 4.8f * k,
                    (hipAboveSoles + 5.5f * k) - height);
        }
    }
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

    // PAINFUL_ACTIVE_TRACE=<frame>: every active mesh that has left the spot
    // it was built at by more than 0.05, once, at that frame.
    {
        const char* activeAt = std::getenv("PAINFUL_ACTIVE_TRACE");
        if (activeAt && traceTick == std::atoi(activeAt)) {
            size_t moved = 0, total = 0;
            for (const auto& kv : entities_) {
                const Entity& e = kv.second;
                if (e.activeMesh < 0) continue;
                ++total;
                float d = 0.f;
                for (int c = 0; c < 3; ++c)
                    d += (e.pos[c] - e.activeOrigin[c]) * (e.pos[c] - e.activeOrigin[c]);
                if (d > 0.05f * 0.05f) {
                    ++moved;
                    if (moved <= 40)
                        LogInfo("ACTIVE %-40s moved %.3f (dy %+.3f) from %.2f %.2f %.2f",
                                e.name.c_str(), std::sqrt(d), e.pos[1] - e.activeOrigin[1],
                                e.activeOrigin[0], e.activeOrigin[1], e.activeOrigin[2]);
                }
            }
            LogInfo("ACTIVE %zu of %zu moved by frame %d", moved, total, traceTick);
        }
    }

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
