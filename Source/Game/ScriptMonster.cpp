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

// The monster's BODY: three stacked spheres. k is the sizer's working unit
// (0.2 * bodyScale) and rootOffset where the stack's centre sits relative to
// the entity position.
bool ScriptEngine::MonsterBodyScale(Entity& e, float& k, float rootOffset[3]) {
    // PhysicsWorld::CreatePhysicsObject (0x101999F0) for a scale argument <= 0:
    //
    //     pivot     = centre of the entity's local box (FUN_1000B250 = (min+max)/2),
    //                 X and Z then replaced by the ROOOT joint's (0x10199B56..B64)
    //     bodyScale = (pivot.y - box.min.y) * 10/11       = half the height * 10/11
    //
    // So k = 0.2 * bodyScale = height / 11, the stack is centred at the
    // model's mid-height, its floor point (-5.5k) is exactly the soles and its
    // top (+5.5k) exactly the head. Every rig gets a body proportional to its
    // height. Docs/Reference/MonsterMovement.md, "The body".
    for (int c = 0; c < 3; ++c) rootOffset[c] = 0.f;
    // A template's own BodyScale (Bat, Tank, Panzer, Winged Demon, Alastor
    // King) goes to the sizer as given, pivot zero.
    if (e.bodyArgScale > 0.f) {
        k = 0.2f * e.bodyArgScale;
        return true;
    }
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel || skel->bones.empty()) return false;

    // The box is the POSED model's: the idle pose's first frame here
    // (SkeletonCache::Entry::poseLo/poseHi), the bind pose when there is none.
    const float height = (skel->poseHi - skel->poseLo) * e.scale;
    if (height <= 0.f) return false;
    k = 0.2f * (0.5f * height) * (10.f / 11.f);
    rootOffset[0] = 0.5f * (skel->lo[0] + skel->hi[0]) * e.scale;
    rootOffset[1] = 0.5f * (skel->poseLo + skel->poseHi) * e.scale;
    rootOffset[2] = 0.5f * (skel->lo[2] + skel->hi[2]) * e.scale;
    // Model::GetJointIndex("ROOOT") - that spelling only; a rig with "root"
    // and no "ROOOT" keeps the box centre sideways too.
    for (size_t i = 0; i < skel->bones.size(); ++i) {
        if (!EqualsCI(skel->bones[i].name, "ROOOT") || i >= skel->bindWorld.size()) continue;
        float rootPos[3];
        skel->bindWorld[i].TransformPoint(0.f, 0.f, 0.f, rootPos);
        rootOffset[0] = rootPos[0] * e.scale;
        rootOffset[2] = rootPos[2] * e.scale;
        break;
    }
    {
        static std::set<std::string> logged;
        if (logged.insert(e.source).second)
            LogInfo("monster body %-14s height %.2f  centre %+.2f above origin  k %.3f  "
                    "radii %.2f/%.2f/%.2f  soles %+.2f, head %+.2f above origin",
                    e.source.c_str(), height, rootOffset[1], k, 2.6f * k, 3.0f * k, 1.5f * k,
                    skel->poseLo * e.scale, skel->poseHi * e.scale);
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
