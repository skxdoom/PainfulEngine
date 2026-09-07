#include "PlayerPawn.h"

#include "../Core/Debug.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace painful {

void PlayerPawn::Spawn(const float headPos[3]) {
    SetHeadPos(headPos);
    speed_ = 0.f;
}

void PlayerPawn::SetHeadPos(const float p[3]) {
    for (int i = 0; i < 3; ++i) head_[i] = p[i];
    velX_ = velY_ = velZ_ = 0.f;
    onGround_ = false;
    resting_ = false;
    stepping_ = false;
    groundedTime_ = 0.f;
    jumpLatched_ = false;
    haveLast_ = false;   // a teleport is not a move to trace back along
}

// PhysicsObject::StepCheck (0x1018eb90): a ladder of line traces from the
// body centre along the wish direction, highest rung first, returning the
// first blocked rung. Heights and reaches are the binary's doubles at
// 0x102c8570..0x102c85f0, for the float argument PlayerAction passes (1.0).
// PlayerMovement.md, "The step ladder".
int PlayerPawn::StepCheck(const PhysicsWorld& physics, const float centre[3],
                          const float wish[2]) const {
    struct Rung { float height, reach; int result; };
    static const Rung kRungs[] = {
        {-0.075f, 0.76f, 4}, {-0.24f, 0.76f, 4}, {0.f, 0.76f, 4}, {0.24f, 0.76f, 4},
        {0.48f, 0.76f, 4},   {0.72f, 0.76f, 4},  {0.96f, 0.76f, 4},
        {-0.68f, 0.646f, 3}, {-0.96f, 0.532f, 2}, {-1.096f, 0.42f, 1},
    };
    // Against the world and pinned meshes only: a loose prop in the way is
    // pushed by the body's contact, not answered as a wall - the engine's
    // trace walks the line-trace collidables, which a prop is not.
    for (const Rung& r : kRungs) {
        const float from[3] = {centre[0], centre[1] + r.height, centre[2]};
        const float to[3] = {from[0] + wish[0] * r.reach, from[1], from[2] + wish[1] * r.reach};
        PhysicsWorld::RayHit hit;
        if (physics.RayCast(from, to, hit, true)) return r.result;
    }
    return 0;
}

void PlayerPawn::Move(PhysicsWorld& physics, const Tweaks& tweaks,
                      uint32_t action, const float right[3], float dt) {
    if (dt <= 0.f) return;
    jumpedThisMove_ = false;
    dt = std::min(dt, 0.05f);   // a hitch must not become a teleport

    // Two movers, two tweak blocks. PlayerAction (0x10192260) reads the
    // PlayerMove block at tweaks+0x00..0x50; MultiPlayerAction (0x10194580)
    // reads MultiPlayerMove at +0x5c..+0xac and nothing else.
    // Docs/Reference/PlayerMovement.md, "The two movers"
    const std::string blk = mp_ ? "MultiPlayerMove." : "PlayerMove.";
    auto tw = [&](const char* name, double fallback) {
        return float(tweaks.Number(blk + name, fallback));
    };

    const float playerSpeed = tw("PlayerSpeed", mp_ ? 11.0 : 8.0);
    const float jumpStrength = tw("JumpStrength", mp_ ? 0.85 : 1.0);
    const float maxHopSpeed = tw("MaximalBunnyHopSpeed", mp_ ? 28.0 : 15.0);
    const float hopAccel = tw("BunnyHopAcceleration", mp_ ? 0.062 : 0.3);
    const float hopAfter = tw("SecondsWhenYouCanBunnyHopAfterLanding", 0.2);
    // 20 single player, 9999 multiplayer - and the two are spent differently.
    const float slowdown = tw("SlowdownDuringJump", mp_ ? 9999.0 : 20.0);
    const float strongAir = tw("StrongAirControl", 0.2);
    const float weakAir = tw("WeakAirControl", 0.002);
    const float gravity = physics.settings().gravity;

    if (speed_ <= 0.f) speed_ = playerSpeed;

    // PlayerAction moves the body by an impulse of mass * f * (target - v),
    // once per frame with no dt in it. The port runs at any frame rate, so
    // the factor is spent per 60 Hz frame: 1 - (1 - f)^(dt * 60).
    // PlayerMovement.md, "The mover is an impulse toward a target"
    auto blend = [&](float f) { return 1.f - std::pow(1.f - f, dt * 60.f); };

    // The wish direction, accumulated exactly as PlayerAction does: the
    // right vector is the strafe axis and forward is it rotated a quarter
    // turn, so both stay in the ground plane whatever the pitch.
    const float rx = right[0], rz = right[2];
    float wish[2] = {0.f, 0.f};
    if (action & Act::Forward)  { wish[0] += rz; wish[1] += -rx; }
    if (action & Act::Backward) { wish[0] -= rz; wish[1] -= -rx; }
    if (action & Act::Right)    { wish[0] += rx; wish[1] += rz; }
    if (action & Act::Left)     { wish[0] -= rx; wish[1] -= rz; }
    const float wl = std::sqrt(wish[0] * wish[0] + wish[1] * wish[1]);
    const bool hasInput = wl > 1e-4f;
    if (hasInput) {
        wish[0] /= wl;
        wish[1] /= wl;
    }
    const bool jump = (action & Act::Jump) != 0;

    // THE LATCH, not an input edge. PlayerAction tests the jump bit at its
    // LEVEL and gates it on a one-byte latch: released clears it, rising
    // sets it. So holding jump does not bounce, but a press made in the air
    // and still held fires on landing. PlayerMovement.md, "Jump is a LATCH"
    if (!jump) jumpLatched_ = false;

    // The body centre: eye - 0.9 (GetPawnHeadPos). The four-sphere stack is
    // swept about it and rests on its bottom, 0.96 below; the floor point
    // the scripts read (centre - 1.1) is 0.14 under the ground.
    // PlayerMovement.md, "The pawn".
    float centre[3] = {head_[0], head_[1] - kEyeAboveCentre, head_[2]};
    const float startX = centre[0], startY = centre[1], startZ = centre[2];

    // The step response, PlayerAction's switch on StepCheck, and it runs
    // in EVERY branch - cases 1..3 never test the grounded flag, so shins
    // meeting a ledge in flight get the same kick. Half the gap to 0.3 of
    // the wish and to a vertical target of 0.4/0.5/0.8 of the speed
    // (0x102c862c, 0x102ae5b0, 0x102b24ac); a wall halves the velocity,
    // forgets the direction and drops the hop bonus.
    auto applyRung = [&](int rung, float dx, float dz) {
        float hScale = 1.f, lift = -1.f;
        switch (rung) {
        case 1: lift = 0.4f; break;
        case 2: hScale = 0.3f; lift = 0.5f; break;
        case 3: hScale = 0.3f; lift = 0.8f; break;
        case 4: hScale = 0.f; break;
        default: return;
        }
        const float b = blend(0.5f);
        velX_ += b * (dx * speed_ * hScale - velX_);
        velZ_ += b * (dz * speed_ * hScale - velZ_);
        if (lift >= 0.f) velY_ += b * (lift * speed_ - velY_);
        stepping_ = rung <= 3;
        if (rung == 4) {
            airDir_[0] = airDir_[1] = 0.f;
            speed_ = playerSpeed;
        }
    };

    // Grounded is the previous frame's floor ray or step (PlayerAction's
    // flag bit 1, cleared when neither held) - the branch the mover takes.
    // A floor steeper than SlopeAngleToSlide for more than five frames is
    // walked as air (the counter at helper +0x70): weak control, sliding.
    const bool sliding = slopeCount_ > 5;
    if (onGround_ && !sliding) {
        groundedTime_ += dt;
        // While grounded, PlayerAction stores BOTH the travel direction and
        // the movement bits on the physics object, every frame - including
        // when nothing is held. Whatever is current when the ground is left
        // is what the airborne branch works from.
        airDir_[0] = hasInput ? wish[0] : 0.f;
        airDir_[1] = hasInput ? wish[1] : 0.f;
        takeoffMask_ = action & (Act::Forward | Act::Backward | Act::Left | Act::Right);

        const float targetX = hasInput ? wish[0] * speed_ : 0.f;
        const float targetZ = hasInput ? wish[1] * speed_ : 0.f;
        // StepCheck answers for the wish direction; nothing asked, nothing
        // blocked.
        const int rung = hasInput ? StepCheck(physics, centre, wish) : 0;
        // A 0.42 rung takes the step response even with jump held:
        // PlayerAction's case 3 never tests the jump flag.
        const bool wantsJump = jump && !jumpLatched_ && rung != 3;
        // The hop window still decides the SPEED bonus, as it does in the engine.
        const bool wantsHop = wantsJump && groundedTime_ <= hopAfter;

        if (wantsJump) {
            // JumpStrength * PlayerSpeed * 0.7 (the 0.7 at 0x102c8648). The
            // standing scale is a play-test STAND-IN, not a recovered rule; a
            // hop from the floor ray's window above the ground drops it.
            // Docs/Reference/PlayerMovement.md, "The jump height that does not add up"
            static const float kStandScale = DebugFloat("PAINFUL_JUMPSCALE", 1.16f);
            velY_ = jumpStrength * playerSpeed * 0.7f * (resting_ ? kStandScale : 1.f);
            // The scripts' jump sound hangs off this, so it must mean an actual
            // jump and not merely leaving the ground - a step-up does that too.
            jumpedThisMove_ = true;
            // Rising now, so the latch closes until the key is released.
            jumpLatched_ = true;
            if (wantsHop) {
                // A timely hop grows the speed toward the cap; a plain jump
                // from standing keeps whatever speed stood.
                if (speed_ < maxHopSpeed)
                    speed_ += (maxHopSpeed - speed_) * hopAccel;
                speed_ = std::min(speed_, maxHopSpeed);
            }
            groundedTime_ = 0.f;
            // A jump is the full difference (f = 1): the horizontal target
            // outright, 0.3 of it on the 0.14 rung, nothing against a wall.
            const float scale = rung == 4 ? 0.f : rung == 2 ? 0.3f : 1.f;
            velX_ = airDir_[0] * speed_ * scale;
            velZ_ = airDir_[1] * speed_ * scale;
            stepping_ = rung == 1 || rung == 2;
            if (rung == 4) {
                airDir_[0] = airDir_[1] = 0.f;
                speed_ = playerSpeed;
            }
        } else {
            // Grounded is never slower than walking: PlayerAction floors the
            // speed at PlayerSpeed on every grounded frame, and past the hop
            // window gives the bunny-hop bonus back.
            speed_ = std::max(speed_, playerSpeed);
            if (groundedTime_ > hopAfter) speed_ = playerSpeed;

            if (rung) {
                applyRung(rung, wish[0], wish[1]);
            } else {
                // The walk closes a fifth of the gap to the wish per frame
                // (0x102b3b80); the vertical is left alone. The binary's 0.2
                // is DOUBLED here by play-test choice - the original's Havok
                // friction on the body made stops read as instant, which the
                // impulse alone does not give, and 0.4 is what felt right
                // without it - except on a slope steep enough to slide, where
                // the creep is balanced against the recovered 0.2.
                // PAINFUL_WALK_FACTOR overrides. PlayerMovement.md, "The port".
                static const float kWalkFactor = DebugFloat("PAINFUL_WALK_FACTOR", 0.4f);
                const float nh = std::sqrt(floorNormal_[0] * floorNormal_[0] +
                                           floorNormal_[2] * floorNormal_[2]);
                const bool steep = nh > physics.settings().meshFriction * floorNormal_[1];
                const float b = blend(steep ? 0.2f : kWalkFactor);
                velX_ += b * (targetX - velX_);
                velZ_ += b * (targetZ - velZ_);
                stepping_ = false;
            }
        }
    } else {
        stepping_ = false;
        // Airborne. What freezes at takeoff is the INPUT MASK, not the
        // direction: PlayerAction rebuilds a direction from the stored bits
        // on the right vector it is handed this call, so the mouse steers a
        // jump. PlayerMovement.md, "Movement rules", Air.
        float air[2] = {0.f, 0.f};
        if (takeoffMask_ & Act::Forward)  { air[0] += rz; air[1] += -rx; }
        if (takeoffMask_ & Act::Backward) { air[0] -= rz; air[1] -= -rx; }
        if (takeoffMask_ & Act::Right)    { air[0] += rx; air[1] += rz; }
        if (takeoffMask_ & Act::Left)     { air[0] -= rx; air[1] -= rz; }
        const float al = std::sqrt(air[0] * air[0] + air[1] * air[1]);
        if (al > 1e-4f) {
            air[0] /= al;
            air[1] /= al;
        } else {
            // No movement key was held at takeoff: the stored direction.
            air[0] = airDir_[0];
            air[1] = airDir_[1];
        }

        // Cancelling is a KEY against the takeoff KEY: both vectors come from
        // the same camera basis, so the dot product depends only on the two
        // masks, and only an opposite key drains the speed.
        const float opposition = -(wish[0] * air[0] + wish[1] * air[1]);
        bool cut = false;
        if (hasInput && opposition > 0.f) {
            cut = true;
            if (mp_) {
                // No speed factor, and the cut is NOT halved to fit: a cut it
                // cannot afford drops the player to 1.0 outright.
                const float c = slowdown * opposition * dt;
                speed_ = c < speed_ ? speed_ - c : 1.f;
            } else {
                float c = slowdown * speed_ * opposition * dt;
                while (c > speed_) c *= 0.5f;
                speed_ -= c;
            }
        }
        // The air factor is StrongAirControl with Forward held or a cut
        // taken this frame, WeakAirControl otherwise (PlayerAction's byte at
        // ESP+0x6f, tweaks +0x34/+0x38). Vertical is left to gravity.
        // Sliding down a too-steep floor takes the weak factor outright.
        const bool strong = !sliding && ((action & Act::Forward) != 0 || cut);
        const bool hasAir = air[0] * air[0] + air[1] * air[1] > 1e-8f;
        const int rung = hasAir ? StepCheck(physics, centre, air) : 0;
        if (rung) {
            applyRung(rung, air[0], air[1]);
        } else {
            const float b = blend(strong ? strongAir : weakAir);
            velX_ += b * (air[0] * speed_ - velX_);
            velZ_ += b * (air[1] * speed_ - velZ_);
        }
    }

    // Gravity. Resting on the floor holds the body at its hover instead of
    // integrating it into the mesh a little every frame.
    if (resting_ && velY_ <= 0.f) velY_ = 0.f;
    else velY_ -= gravity * dt;
    velY_ = std::max(velY_, -60.f);

    // Position CORRECTIONS are not motion: the depenetration push (along a
    // slope's or a corner's tilted normal, every frame the body sits within
    // its gap) and the unstick are kept out of the velocity read back below.
    // Counting them turned a 10-degree slope into a steady slide and pushed
    // the body off ledge corners.
    float corr[3] = {0.f, 0.f, 0.f};
    {
        const float pre[3] = {centre[0], centre[1], centre[2]};
        physics.Depenetrate(centre, -1.f, 4, true);
        for (int c = 0; c < 3; ++c) corr[c] = centre[c] - pre[c];
    }
    const float delta[3] = {velX_ * dt, velY_ * dt, velZ_ * dt};
    // What the scripts read back is the COMMANDED velocity, before the
    // sweep's contacts take their share: a kerb's kick still commands 0.3 of
    // the walk, so CPlayer's "moving faster than 2" holds through the climb
    // and the weapon's walk animation is not restarted at every kerb; a wall
    // halves the command itself, so pressing into one still reads as
    // standing. PlayerMovement.md, "The port".
    const float commandedX = velX_, commandedZ = velZ_;
    physics.SlidePlayer(centre, delta, true);
    // PAINFUL_PAWN_TRACE=1: one line per move with the sweep's result.
    static const bool kTrace = DebugFlag("PAINFUL_PAWN_TRACE");
    const float sweptX = centre[0], sweptY = centre[1], sweptZ = centre[2];
    // The fall speed a touchdown this frame reports, before resting zeroes it.
    const float fallSpeed = velY_ < 0.f ? -velY_ : 0.f;

    // A monster in the way is pushed, a little, and a prop takes the
    // rigid-body contact of the 80 kg body walking into it, by mass.
    const float wantX = delta[0], wantZ = delta[2];
    const float want2 = wantX * wantX + wantZ * wantZ;
    if (want2 > 1e-8f) {
        const float gotX = centre[0] - startX, gotZ = centre[2] - startZ;
        if (gotX * gotX + gotZ * gotZ < want2 * 0.81f) {
            const float push[3] = {wantX, 0.f, wantZ};
            const float from[3] = {startX, startY, startZ};
            physics.ShoveCharacters(from, kRadius, push, speed_, kPlayerMass);
            physics.PushProps(from, push, speed_, kPlayerMass);
        }
    }

    // Resting: a short probe down finds the floor under the stack, and the
    // body is set on it - the sweep's own 0.02 skin between them, as a body
    // at rest keeps. Never while rising: a jump's first frame lifts the
    // body less than the probe's reach.
    float probe[3] = {centre[0], centre[1], centre[2]};
    const float down[3] = {0.f, -(kHover + 0.06f), 0.f};
    // The support's normal: a slope's face, or a ledge's corner under a
    // sphere hanging over it, which a body slides off just the same.
    // A plain cast (one iteration): letting it slide along the contact took
    // the probe down a slope's face and set the body into the slope, which
    // the next depenetration pushed back out - a creep of 0.3 m/s at 30°.
    float support[3] = {0.f, 0.f, 0.f};
    physics.SlidePlayer(probe, down, true, support, 1);
    const float dropped = centre[1] - probe[1];
    resting_ = velY_ <= 0.f && dropped < kHover + 0.045f;
    if (resting_) {
        if (velY_ < 0.f) velY_ = 0.f;
        centre[1] = probe[1] + kHover;
        // Standing on a dynamic body - a bridge plank - presses the player's
        // weight on it. Physics.md, "Ragdoll items: the Catacombs bridge".
        const float feetSphere[3] = {centre[0], centre[1] - kFloorBelowCentre + kRadius, centre[2]};
        physics.PressGround(feetSphere, kRadius, kPlayerMass * gravity);
    }
    // A ceiling stops upward motion: the head sphere is in the sweep now.
    if (velY_ > 0.f) {
        const float risen = centre[1] - startY;
        if (risen < velY_ * dt * 0.5f) velY_ = 0.f;
    }

    // MovePlayerOutOfWall (0x10190890): a ray from where the body was to
    // where it is now; if the move crossed the world, the body goes back to
    // the hit, pushed out along the normal. Gates: the move under 3.0 (a
    // longer one is a teleport) and the crossing past 0.4 of it.
    // PlayerMovement.md, "The three helpers around the body".
    if (haveLast_) {
        const float move[3] = {centre[0] - lastCentre_[0], centre[1] - lastCentre_[1],
                               centre[2] - lastCentre_[2]};
        const float len = std::sqrt(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
        if (len > 1e-4f && len < 3.f) {
            PhysicsWorld::RayHit hit;
            if (physics.RayCast(lastCentre_, centre, hit, true) && hit.distance > 0.4f * len) {
                for (int c = 0; c < 3; ++c) {
                    const float fixed = hit.point[c] + hit.normal[c] * (kRadius + 0.02f);
                    corr[c] += fixed - centre[c];
                    centre[c] = fixed;
                }
            }
        }
    }
    for (int c = 0; c < 3; ++c) lastCentre_[c] = centre[c];
    haveLast_ = true;

    // The body's velocity is what the contacts left of it: a wall takes the
    // component into it, as Havok's contact did, and the next frame's
    // impulse rebuilds from there.
    if (kTrace) {
        std::printf("pawn: start %.4f %.4f %.4f delta %.4f %.4f %.4f swept %.4f %.4f %.4f "
                    "rest=%d dropped %.4f corr %.4f %.4f %.4f support %.2f %.2f %.2f "
                    "ray %.2f %.2f %.2f axis=%d mu %.2f final %.4f %.4f %.4f\n",
                    startX, startY, startZ, delta[0], delta[1], delta[2], sweptX, sweptY, sweptZ,
                    int(resting_), dropped, corr[0], corr[1], corr[2],
                    support[0], support[1], support[2],
                    floorNormal_[0], floorNormal_[1], floorNormal_[2], int(axisFloor_),
                    physics.settings().meshFriction, centre[0], centre[1], centre[2]);
    }
    velX_ = (centre[0] - startX - corr[0]) / dt;
    velZ_ = (centre[2] - startZ - corr[2]) / dt;

    // On a slope the original's body is held by Havok's contact friction
    // and slides once gravity along the slope beats it. A STAND-IN for that
    // contact: Coulomb friction at the level's DefaultMeshFriction (the
    // body's own coefficient is not recovered), the excess spent as
    // horizontal acceleration downhill. PlayerMovement.md, "Slopes".
    if (resting_) {
        // The floor ray's face normal where the axis ray hit last frame - a
        // sweep's contact normal over a seam between coplanar triangles is a
        // direction to the seam, not the floor, and slid the body on flat
        // ground - and the probe's contact normal otherwise, which is the
        // corner or the slope beside the axis.
        const float* n = axisFloor_ ? floorNormal_ : (support[1] > 0.01f ? support : floorNormal_);
        const float ny = n[1];
        const float nh = std::sqrt(n[0] * n[0] + n[2] * n[2]);
        if (ny > 0.01f && nh > 1e-3f) {
            const float mu = physics.settings().meshFriction;
            const float excess = gravity * (nh - mu * ny);   // sin - mu cos
            if (excess > 0.f) {
                velX_ += excess * ny * dt * (n[0] / nh);
                velZ_ += excess * ny * dt * (n[2] / nh);
            }
        }
    }

    // FloorCheck's ray: from the head to 0.3 past the floor contact
    // (0x102c85f4 = -1.4 off the centre), which is how far above the ground
    // the engine still counts as standing - a step's hop, a hop before
    // landing. When it misses, FloorCheckRandom(-7.0, 4.5): the same ray at
    // a random offset within the body's 0.4 in x and z (rand/32767 - 0.5,
    // times 4.0 times 0.2), which is what finds a steep slope beside the
    // axis. PlayerMovement.md, "The three helpers around the body".
    {
        PhysicsWorld::RayHit hit;
        float from[3] = {centre[0], centre[1] + kEyeAboveCentre, centre[2]};
        float to[3] = {centre[0], centre[1] - kFloorReach, centre[2]};
        bool floorHit = physics.RayCast(from, to, hit);
        axisFloor_ = floorHit;
        if (!floorHit) {
            auto unit = [&] {          // xorshift32 -> [-0.5, 0.5]
                rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                return float(rng_ & 0xffff) / 65535.f - 0.5f;
            };
            const float ox = unit() * 4.f * 0.2f, oz = unit() * 4.f * 0.2f;
            from[0] += ox; from[2] += oz;
            to[0] += ox; to[2] += oz;
            floorHit = physics.RayCast(from, to, hit);
        }
        if (floorHit) {
            for (int c = 0; c < 3; ++c) floorNormal_[c] = hit.normal[c];
            // cos(SlopeAngleToSlide) against the normal's y (0x10192d06).
            const float maxSlope = tw("SlopeAngleToSlide", 60.0);
            if (std::cos(maxSlope * 0.017453292f) <= hit.normal[1]) slopeCount_ = 0;
            else if (slopeCount_ < 10) ++slopeCount_;
        } else if (slopeCount_ > 0) {
            --slopeCount_;
        }
        // PLAYER.FloorCheck's own ray: FloorCheck(-8.5, 4.5), the axis from
        // the head to centre - 1.7, and no random retry (0x10138da0).
        {
            const float sFrom[3] = {centre[0], centre[1] + kEyeAboveCentre, centre[2]};
            const float sTo[3] = {centre[0], centre[1] - 8.5f * 0.2f, centre[2]};
            PhysicsWorld::RayHit sHit;
            scriptFloor_ = physics.RayCast(sFrom, sTo, sHit);
        }
        const bool grounded = floorHit || stepping_;
        if (grounded && !onGround_) {
            groundedTime_ = 0.f;             // touchdown
            landingImpact_ = fallSpeed;      // fall speed at impact
        }
        onGround_ = grounded;
    }

    // What the scripts read back through ENTITY.GetVelocity. CPlayer decides
    // it is WALKING from this - "moving faster than 2" - and gates the head
    // bob and the footstep sounds on it.
    velocity_[0] = commandedX;
    velocity_[1] = velY_;
    velocity_[2] = commandedZ;

    head_[0] = centre[0];
    head_[1] = centre[1] + kEyeAboveCentre;
    head_[2] = centre[2];
}

} // namespace painful
