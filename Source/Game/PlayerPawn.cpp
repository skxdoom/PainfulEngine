#include "PlayerPawn.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace painful {

void PlayerPawn::Spawn(const float headPos[3]) {
    SetHeadPos(headPos);
    speed_ = 0.f;
}

void PlayerPawn::SetHeadPos(const float p[3]) {
    for (int i = 0; i < 3; ++i) head_[i] = p[i];
    velY_ = 0.f;
    onGround_ = false;
    groundedTime_ = 0.f;
    jumpLatched_ = false;
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
    const float hopBefore = tw("SecondsWhenYouCanBunnyHopBeforeLanding", 0.2);
    // 20 single player, 9999 multiplayer - and the two are spent differently.
    const float slowdown = tw("SlowdownDuringJump", mp_ ? 9999.0 : 20.0);
    const float gravity = physics.settings().gravity;

    if (speed_ <= 0.f) speed_ = playerSpeed;

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

    // THE LATCH, not an input edge. PlayerAction (0x10192260) tests the jump
    // bit at its LEVEL and gates it on a one-byte latch:
    //
    //     bStack_fb = (action >> 5) & 1;            // the Jump bit, level
    //     if (!bStack_fb) latch = 0;                // released -> cleared
    //     ...
    //     jump = bStack_fb && grounded && latch == 0;
    //     if (verticalVel > tweak[+8]) latch = 1;   // rising -> set
    //
    // So holding jump does not bounce you, but a press made in the AIR and
    // still held fires the moment you land, however long that takes. An input
    // edge plus a fixed before-landing buffer drops exactly that case: press
    // early, keep holding, land after the window, and nothing happens. That is
    // the "space does not always jump" from play.
    if (!jump) jumpLatched_ = false;

    // A bunny hop fires while still ABOVE the floor: that lift is what lets a
    // smaller launch reach the standing jump's apex, so a chain tops out at one
    // height. Probe for floor within kHopLift while falling.
    bool airHop = false;
    if (!onGround_ && jump && !jumpLatched_ && velY_ <= 0.f) {
        float probe[3] = {head_[0], head_[1] - kEyeAboveFloor + kRadius, head_[2]};
        const float startY = probe[1];
        const float down[3] = {0.f, -kHopLift, 0.f};
        physics.SlideSphere(probe, down, kRadius, true);
        airHop = (startY - probe[1]) < kHopLift - 1e-3f;   // something stopped it
    }

    float vx = 0.f, vz = 0.f;
    if (onGround_ || airHop) {
        if (onGround_) groundedTime_ += dt;
        // While grounded, PlayerAction stores BOTH the travel direction and
        // the movement bits on the physics object, every frame. Whatever is
        // held at the moment the ground is left is what the airborne branch
        // then works from - so this has to track continuously, not only be
        // captured on a jump. Set it only at takeoff and walking off a lip
        // leaves an empty mask, which reads as no air direction at all and
        // drops the player straight down with no forward motion.
        // Including when nothing is held. Keeping the last direction instead
        // makes a standing jump launch at full walking speed the way the
        // player last WALKED, seconds ago - the takeoff mask is empty, the air
        // branch falls back to this, and you sail off sideways having pressed
        // only jump.
        airDir_[0] = hasInput ? wish[0] : 0.f;
        airDir_[1] = hasInput ? wish[1] : 0.f;
        takeoffMask_ = action & (Act::Forward | Act::Backward | Act::Left | Act::Right);
        const bool wantsJump = jump && !jumpLatched_;
        // The hop window still decides the SPEED bonus, as it does in the engine.
        const bool wantsHop = wantsJump && groundedTime_ <= hopAfter;

        if (wantsJump) {
            // JumpStrength * PlayerSpeed * 0.7 (the 0.7 at 0x102c8648). The
            // standing scale is a play-test STAND-IN, not a recovered rule; a
            // hop drops it and makes the height up with kHopLift instead.
            // Docs/Reference/PlayerMovement.md, "The jump height that does not add up"
            static const float kStandScale = [] {
                const char* e = std::getenv("PAINFUL_JUMPSCALE");
                return e ? float(std::atof(e)) : 1.16f;
            }();
            velY_ = jumpStrength * playerSpeed * 0.7f * (airHop ? 1.f : kStandScale);
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
            onGround_ = false;
            groundedTime_ = 0.f;
            vx = airDir_[0] * speed_;
            vz = airDir_[1] * speed_;
        } else {
            // Grounded is never slower than walking: PlayerAction floors the
            // speed at PlayerSpeed on every grounded frame (`if (speed <
            // Tweak+0x0c) speed = Tweak+0x0c`). This is what gives a cancelled
            // jump its legs back the instant it lands - without it the player
            // kept the 0.1 the cancel had left and could not move.
            speed_ = std::max(speed_, playerSpeed);
            // Past the hop window the bunny-hop BONUS is given back too, which
            // is the ceiling to that floor. Ground movement is an instant snap
            // to wishDir * speed - the original has no walk acceleration ramp.
            if (groundedTime_ > hopAfter) speed_ = playerSpeed;
            vx = hasInput ? wish[0] * speed_ : 0.f;
            vz = hasInput ? wish[1] * speed_ : 0.f;
        }
    } else {
        // Airborne. What freezes at takeoff is the INPUT MASK, not the
        // direction: PlayerAction stores `action & 0x1e` and then, every
        // airborne frame, rebuilds a direction from those stored bits using
        // the RIGHT VECTOR IT WAS JUST HANDED - the same accumulation the
        // ground branch runs, on the current camera basis. So turning the
        // mouse in the air turns your motion with it, which is the whole of
        // air control here: hold a strafe key, swing the mouse, and the
        // velocity follows. Freezing the world-space direction instead (as
        // this did) removes air steering entirely and pins you to a wall
        // you jumped alongside, because nothing can turn the motion away
        // from it.
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
            // No movement key was held at takeoff: keep drifting the way we
            // already were rather than stopping dead in mid-air.
            air[0] = airDir_[0];
            air[1] = airDir_[1];
        }

        // Cancelling is a KEY against the takeoff KEY, not a heading against a
        // heading. Both vectors are built from the same camera basis, so the
        // dot product depends only on the two masks: forward vs backward is -1
        // and forward vs left is 0 whatever the camera is doing. That keeps the
        // mouse free to steer the jump - the CPMA-style air control this game
        // has - while only an opposite key drains the speed.
        //
        // Measuring the live input against the world-space takeoff direction
        // instead made a 180-degree mouse turn read as a reversal and stopped
        // the player in mid-air.
        const float opposition = -(wish[0] * air[0] + wish[1] * air[1]);
        if (hasInput && opposition > 0.f) {
            if (mp_) {
                // No speed factor, and the cut is NOT halved to fit: a cut it
                // cannot afford drops the player to 1.0 outright. At 9999 that
                // is every reversal, which is the dead stop in multiplayer.
                const float cut = slowdown * opposition * dt;
                speed_ = cut < speed_ ? speed_ - cut : 1.f;
            } else {
                float cut = slowdown * speed_ * opposition * dt;
                while (cut > speed_) cut *= 0.5f;
                speed_ -= cut;
            }
        }
        // airDir_ is NOT refreshed here: the engine writes its reference
        // direction (pfVar2[10..0xc]) only at takeoff, so the opposition is
        // measured against the direction the jump began in for the whole
        // flight. Updating it per frame let a reversal bite for one frame and
        // then agree with itself, which bled 8.0 to 6.6 and no further.
        vx = air[0] * speed_;
        vz = air[1] * speed_;
    }

    // Gravity applies only off the ground. Integrating it while standing
    // pushes the sphere a little into the floor every frame, and the slide's
    // skin lift does not quite give it all back - the player sank about 0.1
    // units a second, forever, and would eventually be under the level.
    // Standing still means standing still; leaving the ground is detected by
    // the probe below, and the jump path has already cleared onGround_.
    if (onGround_ && velY_ <= 0.f) velY_ = 0.f;
    else velY_ -= gravity * dt;
    velY_ = std::max(velY_, -60.f);

    // Slide the feet sphere; the eye rides kEyeAboveFloor above the contact.
    const bool wasGrounded = onGround_;
    float feet[3] = {head_[0], head_[1] - kEyeAboveFloor + kRadius, head_[2]};
    const float startX = feet[0], startY = feet[1], startZ = feet[2];
    // What the scripts read back through ENTITY.GetVelocity. CPlayer decides
    // it is WALKING from this - "moving faster than 2" - and gates the head
    // bob and the footstep sounds on it, so a pawn that reports nothing never
    // bobs and never makes a footfall.
    velocity_[0] = vx;
    velocity_[1] = velY_;
    velocity_[2] = vz;

    const float delta[3] = {vx * dt, velY_ * dt, vz * dt};
    physics.SlideSphere(feet, delta, kRadius, true);

    // StepCheck, after the engine's own ladder (see kStepMax in the header).
    // When a grounded move comes up short of what was asked, retry it from
    // the top of what the engine would still climb and settle back down onto
    // whatever is there.
    const float wantX = vx * dt, wantZ = vz * dt;
    const float want2 = wantX * wantX + wantZ * wantZ;
    // A monster in the way is pushed, a little. In the original both are
    // Havok bodies and the contact splits the closing speed by mass; here the
    // sweep stops the pawn and the character gets the player's share of the
    // speed along the wish, which its own tick then decays. Player mass 80 =
    // (0.2)^3 * 10000, the sizer's rule for BodyTypes.Player.
    if (want2 > 1e-8f) {
        const float gotX = feet[0] - startX, gotZ = feet[2] - startZ;
        if (gotX * gotX + gotZ * gotZ < want2 * 0.81f) {
            const float push[3] = {wantX, 0.f, wantZ};
            const float from[3] = {startX, startY, startZ};
            physics.ShoveCharacters(from, kRadius, push, speed_, kPlayerMass);
        }
    }
    if (wasGrounded && want2 > 1e-8f) {
        const float gotX = feet[0] - startX, gotZ = feet[2] - startZ;
        const float got2 = gotX * gotX + gotZ * gotZ;
        if (got2 < want2 * 0.81f) {           // blocked: under 90% of the ask
            // MEASURED ALONG THE WISH, NOT AS ANY MOTION AT ALL.
            //
            // The engine's top rung is a forward LINE TRACE at 0.86, and a
            // block there is a WALL - direction cleared, no vertical response.
            // A swept sphere SLIDES instead, so pressing into a wall produces
            // lateral motion, and comparing raw distances let that count as
            // "higher up there is room": the pawn climbed a fraction every
            // frame and gravity pulled it back down. Measured wedged against
            // one obstacle on Cathedral, 712 vertical moves in 800 frames -
            // a fall of ~0.15 followed by a +0.14 to +0.22 pop, forever.
            const float invWant = 1.f / std::sqrt(want2);
            const float wishX = wantX * invWant, wishZ = wantZ * invWant;
            const float gotAlong = gotX * wishX + gotZ * wishZ;

            float step[3] = {startX, startY + kStepMax, startZ};
            const float over[3] = {wantX, 0.f, wantZ};
            physics.SlideSphere(step, over, kRadius, true);
            const float sAlong = (step[0] - startX) * wishX + (step[2] - startZ) * wishZ;
            // Comfortably past SlideSphere's own 0.02 skin, so a step is taken
            // for real progress rather than for numerical noise.
            if (sAlong > gotAlong + 0.05f) {
                // Drop back onto the step.
                const float back[3] = {0.f, -kStepMax, 0.f};
                physics.SlideSphere(step, back, kRadius, true);

                // ONLY IF THE PAWN CAN STAND THERE.
                //
                // A step it cannot rest on is the other half of the bounce:
                // the pawn was placed on an edge, the ground probe below
                // reported it airborne, and gravity returned it next frame.
                float rest[3] = {step[0], step[1], step[2]};
                const float settle[3] = {0.f, -0.06f, 0.f};
                physics.SlideSphere(rest, settle, kRadius, true);
                if ((step[1] - rest[1]) < 0.045f) {
                    for (int c = 0; c < 3; ++c) feet[c] = step[c];

                    // Anything above the free rung is charged for: the engine
                    // scales that frame's velocity by 0.3, which on a fixed
                    // step is the same as scaling the distance it just
                    // covered. That cut, against a rise of up to 0.86 in one
                    // frame, is what gives a step its small hop.
                    if (feet[1] - startY > kStepFree) {
                        feet[0] = startX + (feet[0] - startX) * kStepPenalty;
                        feet[2] = startZ + (feet[2] - startZ) * kStepPenalty;
                    }
                }
            }
        }
    }
    // A downhill ground-follow was tried here - cast a step down after the
    // move and take the contact, so a descent does not drop the pawn into
    // the airborne branch. Measured across eight directions it helped
    // descents (6.74 against 5.44 over a second) and cost as much again on
    // flat and climbing ground (5.58 against 7.98), while changing which
    // route the player took rather than following the same one better. The
    // harm it was meant to undo - a flickering frame freezing the movement
    // bits - is already gone, because the grounded branch above now keeps
    // those bits current. Left out until something measures better.

    // Grounded when a small downward probe barely moves.
    float probe[3] = {feet[0], feet[1], feet[2]};
    const float down[3] = {0.f, -0.06f, 0.f};
    physics.SlideSphere(probe, down, kRadius, true);
    const bool grounded = (feet[1] - probe[1]) < 0.045f;
    if (grounded && !onGround_) {
        groundedTime_ = 0.f;             // touchdown
        landingImpact_ = -velY_;         // fall speed at impact
    }
    onGround_ = grounded;
    if (onGround_ && velY_ < 0.f) velY_ = 0.f;
    // Standing on a dynamic body - a bridge plank - presses the player's
    // weight on it. Physics.md, "Ragdoll items: the Catacombs bridge".
    if (onGround_) physics.PressGround(feet, kRadius, kPlayerMass * gravity);
    // A ceiling stops upward motion.
    if (velY_ > 0.f) {
        const float risen = feet[1] - (head_[1] - kEyeAboveFloor + kRadius);
        if (risen < velY_ * dt * 0.5f) velY_ = 0.f;
    }

    head_[0] = feet[0];
    head_[1] = feet[1] + kEyeAboveFloor - kRadius;
    head_[2] = feet[2];
}

} // namespace painful
