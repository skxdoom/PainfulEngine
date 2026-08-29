#include "PlayerPawn.h"

#include <algorithm>
#include <cmath>

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
    jumpQueuedFor_ = 0.f;
}

void PlayerPawn::Move(const PhysicsWorld& physics, const Tweaks& tweaks,
                      const float wishDir[3], bool jump, float dt) {
    if (dt <= 0.f) return;
    dt = std::min(dt, 0.05f);   // a hitch must not become a teleport

    const float playerSpeed = float(tweaks.Number("PlayerMove.PlayerSpeed", 8.0));
    const float jumpStrength = float(tweaks.Number("PlayerMove.JumpStrength", 1.0));
    const float maxHopSpeed =
        float(tweaks.Number("PlayerMove.MaximalBunnyHopSpeed", 15.0));
    const float hopAccel =
        float(tweaks.Number("PlayerMove.BunnyHopAcceleration", 0.3));
    const float hopAfter =
        float(tweaks.Number("PlayerMove.SecondsWhenYouCanBunnyHopAfterLanding", 0.2));
    const float hopBefore =
        float(tweaks.Number("PlayerMove.SecondsWhenYouCanBunnyHopBeforeLanding", 0.2));
    const float slowdown =
        float(tweaks.Number("PlayerMove.SlowdownDuringJump", 20.0));
    const float gravity = physics.settings().gravity;

    if (speed_ <= 0.f) speed_ = playerSpeed;

    // Normalised wish direction on the ground plane.
    float wish[2] = {wishDir[0], wishDir[2]};
    const float wl = std::sqrt(wish[0] * wish[0] + wish[1] * wish[1]);
    const bool hasInput = wl > 1e-4f;
    if (hasInput) {
        wish[0] /= wl;
        wish[1] /= wl;
    }

    // The before-landing buffer: a jump pressed in the air counts if the
    // ground arrives within the window (PlayerAction's pfVar2[7] timer).
    const bool jumpPressed = jump && !jumpHeld_;
    jumpHeld_ = jump;
    if (jumpPressed && !onGround_) jumpQueuedFor_ = hopBefore;
    else if (jumpQueuedFor_ > 0.f) jumpQueuedFor_ = std::max(0.f, jumpQueuedFor_ - dt);

    float vx = 0.f, vz = 0.f;
    if (onGround_) {
        groundedTime_ += dt;
        const bool wantsHop =
            (jumpPressed || jumpQueuedFor_ > 0.f) && groundedTime_ <= hopAfter;
        const bool wantsJump = jumpPressed || jumpQueuedFor_ > 0.f;

        if (wantsJump) {
            // The engine's jump: JumpStrength * PlayerSpeed * 0.7.
            velY_ = jumpStrength * playerSpeed * 0.7f;
            if (wantsHop) {
                // A timely hop grows the speed toward the cap; a plain jump
                // from standing keeps whatever speed stood.
                if (speed_ < maxHopSpeed)
                    speed_ += (maxHopSpeed - speed_) * hopAccel;
                speed_ = std::min(speed_, maxHopSpeed);
            }
            // The air direction freezes at takeoff.
            takeoffDir_[0] = hasInput ? wish[0] : 0.f;
            takeoffDir_[1] = hasInput ? wish[1] : 0.f;
            onGround_ = false;
            groundedTime_ = 0.f;
            jumpQueuedFor_ = 0.f;
            vx = takeoffDir_[0] * speed_;
            vz = takeoffDir_[1] * speed_;
        } else {
            // Grounded past the hop window: back to walking speed. Ground
            // movement is an instant snap to wishDir * speed - the original
            // has no walk acceleration ramp.
            if (groundedTime_ > hopAfter) speed_ = playerSpeed;
            vx = hasInput ? wish[0] * speed_ : 0.f;
            vz = hasInput ? wish[1] * speed_ : 0.f;
        }
    } else {
        // Airborne: the direction is the takeoff's. Opposing input bleeds
        // speed - PlayerAction's SlowdownDuringJump term, halved while it
        // exceeds the speed - and that is the only air steering.
        if (hasInput) {
            const float opposition =
                -(wish[0] * takeoffDir_[0] + wish[1] * takeoffDir_[1]);
            if (opposition > 0.f) {
                float cut = slowdown * speed_ * opposition * dt;
                while (cut > speed_) cut *= 0.5f;
                speed_ -= cut;
            }
        }
        vx = takeoffDir_[0] * speed_;
        vz = takeoffDir_[1] * speed_;
    }

    velY_ -= gravity * dt;
    velY_ = std::max(velY_, -60.f);

    // Slide the feet sphere; the eye rides 2.31 m above the floor contact.
    float feet[3] = {head_[0], head_[1] - kEyeAboveFloor + kRadius, head_[2]};
    const float delta[3] = {vx * dt, velY_ * dt, vz * dt};
    physics.SlideSphere(feet, delta, kRadius);

    // Grounded when a small downward probe barely moves.
    float probe[3] = {feet[0], feet[1], feet[2]};
    const float down[3] = {0.f, -0.06f, 0.f};
    physics.SlideSphere(probe, down, kRadius);
    const bool grounded = (feet[1] - probe[1]) < 0.045f;
    if (grounded && !onGround_) {
        groundedTime_ = 0.f;             // touchdown
        landingImpact_ = -velY_;         // fall speed at impact
    }
    onGround_ = grounded;
    if (onGround_ && velY_ < 0.f) velY_ = 0.f;
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
