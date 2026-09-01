// ScriptEngine: monster body sizing and the movement tick.
// How wide and how tall a character body is - neither is what the engine
// documents - and the per-frame walk that PO_SetMonsterType drives.

#include "ScriptEngineInternal.h"

namespace painful {

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
// constant - flagged in Docs/Reference/MonsterMovement.md.
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

// The monster's BODY: a single full-height capsule, standing on its soles.
//
// Not the same question as MonsterRadius, which sizes the sphere TickMonsters
// sweeps to move a monster through the world. This is what everything else
// collides with - what you bump into, shove, and cannot stand inside - and it
// wants to be the whole creature rather than a ball at hip height.
//
// Radius is the smaller horizontal half-extent, for the reason MonsterRadius
// gives: the larger one is arms. The cylinder is whatever height is left once
// the two hemispheres have taken their radius, so a squat creature degenerates
// to a sphere on its own rather than by special case.
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

    // PAINFUL_MONSTER_TRACE=<frame> dumps, once, the numbers that decide
    // whether a model stands ON the floor or IN it: the mesh bounds, the
    // sphere the mover sweeps, and where its soles land against the geometry
    // below. None of it is reachable from Lua, which is why a script-side
    // trace cannot answer "is this monster sunk".
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
        // A corpse is not walking anywhere. Once the solver owns the actor,
        // TickMonsters must stop dragging its movement body around or the two
        // fight over where it is.
        if (e.ragdollSlot >= 0) continue;

        // An actor whose .rde gives it limbs is shot at through those, so its
        // walking body stops answering traces. One that has none keeps it -
        // otherwise a monster with no ragdoll definition becomes unshootable.
        {
            const std::vector<LimbBounds>* shootable = Hitboxes(e.source);
            if (shootable && !shootable->empty()) limbShadowed_.push_back(e.physicsBody);
        }

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
        // A step shorter than the skin is CARRIED, not spent - but the sphere
        // still has to be pushed out of anything it is inside, and the floor
        // still has to be re-tested. Skipping the sweep entirely left an actor
        // embedded in geometry embedded for good: with onFloor latched true its
        // residual never grows enough to sweep again. SlideSphere with a zero
        // delta is exactly the missing half - it depenetrates, then finds
        // nothing to advance.
        const bool spend = carried >= kSweepSkin;
        float step[3] = {0.f, 0.f, 0.f};
        if (spend) {
            for (int c = 0; c < 3; ++c) {
                step[c] = e.moveResidual[c];
                e.moveResidual[c] = 0.f;
            }
        }

        // The SAME swept sphere the player moves with, so a monster is stopped
        // by exactly the geometry the player is stopped by. solidProps=true
        // because a monster should no more walk through a coffin than the
        // player should.
        //
        // The sphere is carried to where the model's soles are and put back on
        // the origin afterwards; MonsterRadius works that offset out from the
        // model's own bounds.
        float pos[3] = {e.pos[0], e.pos[1] + lift, e.pos[2]};
        const float before[3] = {pos[0], pos[1], pos[2]};
        // collideWithPlayer: a monster feels the player, so the player can
        // shoulder it aside and it cannot walk through them.
        bool pushedByCharacter = false;
        physics_->SlideSphere(pos, step, radius, true, e.physicsBody, true,
                              &pushedByCharacter);
        if (!spend && !pushedByCharacter) {
            // Depenetration only. Adopt it when it is a real extraction, not the
            // hairline overlap a resting sphere reports every frame: taking that
            // walks a standing actor upward a fraction at a time, which is this
            // same bug with its sign flipped (measured 0.0007/frame before the
            // guard, 0.19 over 250 frames).
            float moved = 0.f;
            for (int c = 0; c < 3; ++c)
                moved += (pos[c] - before[c]) * (pos[c] - before[c]);
            if (std::sqrt(moved) < kSweepSkin)
                for (int c = 0; c < 3; ++c) pos[c] = before[c];
        }

        // Floor state, for PO_IsOnFloor. A short probe straight down: far
        // enough to survive the gap a slide leaves, short enough not to claim
        // ground the actor is falling towards.
        const float below[3] = {0.f, -(radius * 0.25f), 0.f};
        float probe[3] = {pos[0], pos[1], pos[2]};
        physics_->SlideSphere(probe, below, radius, true, e.physicsBody, true);
        e.onFloor = (probe[1] - pos[1]) > below[1] * 0.5f;
        if (e.onFloor) e.fallSpeed = 0.f;
        pos[1] -= lift;
        // The normal is not measured yet; upright is the answer that keeps
        // CAiBrain's arithmetic honest until a real contact normal is read.
        e.floorNormal[0] = 0.f;
        e.floorNormal[1] = 1.f;
        e.floorNormal[2] = 0.f;

        if (dumpGround) {
            const SkeletonCache::Entry* s = skeletons_.Get(e.source);
            const float soles = e.pos[1] + (s ? s->lo[1] * e.scale : 0.f);
            PhysicsWorld::RayHit hit;
            const float from[3] = {e.pos[0], e.pos[1] + lift, e.pos[2]};
            const float to[3] = {e.pos[0], e.pos[1] + lift - 30.f, e.pos[2]};
            const bool got = physics_->RayCast(from, to, hit, true);
            LogInfo("MONSTER %-12s pos=%8.3f scale=%.3f lo=%7.2f hi=%7.2f r=%.3f lift=%+.3f "
                    "sphereBottom=%8.3f floor=%8.3f soles=%8.3f soleGap=%+.3f onFloor=%d",
                    e.source.c_str(), e.pos[1], e.scale, s ? s->lo[1] : 0.f,
                    s ? s->hi[1] : 0.f, radius, lift, e.pos[1] + lift - radius,
                    got ? hit.point[1] : 0.f, soles,
                    got ? soles - hit.point[1] : 0.f, e.onFloor ? 1 : 0);
        }

        for (int c = 0; c < 3; ++c) e.pos[c] = pos[c];
        // The body is kinematic, so it does not move itself - it is carried,
        // and it exists so that everything sweeping against the world finds a
        // monster in the way.
        //
        // AT THE ENTITY POSITION, with no lift.
        //
        // The offset lives in the SHAPE, which is built about -root exactly as
        // PhysicsWorld::CreatePhysicsObject does it. Adding `lift` here as
        // well applied a second offset - and one derived from the mesh bounds,
        // which vary per rig by where its author left the origin. That is why
        // the bodies detached from their monsters by a different amount each.
        physics_->SetScriptBodyPose(e.physicsBody, e.pos, e.rotWXYZ);
        if (renderer_ && e.rendererInstance >= 0)
            renderer_->SetScriptPose(e.rendererInstance, e.pos, e.rotWXYZ);
    }
}


}  // namespace painful
