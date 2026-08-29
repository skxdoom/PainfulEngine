#pragma once
#include "../World/PhysicsWorld.h"

namespace painful {

// The player's locomotion, rebuilt from the engine's own mover:
// PhysicsObject::PlayerAction (Engine.dll 0x10192260), the function that
// consumes the Tweak.PlayerMove block. The scripts only read the results
// (CPlayer keeps health and weapons, not velocity).
//
// Recovered facts, each from the decompile:
//  - The pawn is the "player_box" model at scale 0.155: 14.90 units tall
//    -> 2.31 m, 0.82 m wide (EngineGame::CreatePlayer, 0x1001cef0).
//  - Positions are head-anchored: head = centre + 0.9h, floor = centre -
//    1.1h with h the half-height (GetPawnHeadPos / GetPawnFloorPos, the
//    0.9/1.1 at 0x102c8510/0x102c7c04). SetPawnHeadPos takes the EYE
//    position - Lev.Pos is eye level.
//  - Ground velocity is wishDir * currentSpeed, an instant snap - there is
//    no walk acceleration ramp.
//  - jumpVelocity = JumpStrength * PlayerSpeed * 0.7 (the 0.7 at
//    0x102c8648) = 5.6 m/s stock, an 0.8 m hop at gravity 19.62.
//  - Bunny-hop: a jump pressed within SecondsWhenYouCanBunnyHopBeforeLanding
//    of touchdown, or re-pressed within ...AfterLanding of it, is a hop:
//    currentSpeed += (MaximalBunnyHopSpeed - currentSpeed) *
//    BunnyHopAcceleration, clamped at the maximum. Standing on the ground
//    past the AfterLanding window resets currentSpeed to PlayerSpeed.
//  - In the air the horizontal direction is FROZEN at takeoff; input that
//    opposes it bleeds speed by SlowdownDuringJump * speed * opposition *
//    dt (halved while it exceeds the speed), which is the jump's only
//    steering.
class PlayerPawn {
public:
    void Spawn(const float headPos[3]);

    // One movement step. wishDir is the camera-relative ground-plane
    // direction (unnormalised is fine; zero means no input).
    void Move(const PhysicsWorld& physics, const Tweaks& tweaks,
              const float wishDir[3], bool jump, float dt);

    const float* headPos() const { return head_; }
    void SetHeadPos(const float p[3]);
    // The feet - ENTITY.PO_GetPawnFloorPos, the scripts' _groundx/y/z.
    void FloorPos(float out[3]) const {
        out[0] = head_[0];
        out[1] = head_[1] - kEyeAboveFloor;
        out[2] = head_[2];
    }
    bool onGround() const { return onGround_; }
    float currentSpeed() const { return speed_; }

    // The engine's own landing test, and the only authority for it.
    // PlayerAction queues PLAYER_HIT_GROUND when the touchdown speed scaled
    // by the world time multiplier passes kHitGroundSpeed; fall damage
    // itself is script-side, in OnHitGround.
    //
    // worldTimeScale is the double at GEngine+0x100 - the world speed the
    // engine multiplies frame time by, 1.0 normally (PlayerAction has a fast
    // path testing it against exactly 1.0) and retuned for slow motion.
    // WORLD.SetWorldSpeed is still a stub here, so the default stands in for
    // it; pass the real value once that native lands.
    //
    // Returns the fall speed to report, or 0 for a soft landing. Clears the
    // recorded impact either way, so call it once per frame.
    float TakeGroundHit(float worldTimeScale = 1.f) {
        const float impact = landingImpact_;
        landingImpact_ = 0.f;
        return impact * worldTimeScale > kHitGroundSpeed ? impact : 0.f;
    }

    // 0x102c8690, the constant PlayerAction compares the scaled fall speed
    // against.
    static constexpr float kHitGroundSpeed = 20.f;

private:
    // player_box at 0.155: half-height h = 1.155, eye-to-floor = 2h = 2.31.
    // The collision sphere sits at the feet (radius from the 0.82 m box
    // width), so steps and floors resolve where the body actually is.
    static constexpr float kEyeAboveFloor = 2.31f;
    static constexpr float kRadius = 0.41f;

    float head_[3] = {0, 0, 0};
    float velY_ = 0.f;
    bool onGround_ = false;

    // The bunny-hop state PlayerAction keeps on the physics object.
    float speed_ = 0.f;              // current target speed; 0 = uninitialised
    float groundedTime_ = 0.f;       // seconds since touchdown
    float jumpQueuedFor_ = 0.f;      // before-landing buffer countdown
    bool jumpHeld_ = false;          // edge detection for hop presses
    float takeoffDir_[2] = {0, 0};   // frozen air direction (x, z)
    float landingImpact_ = 0.f;      // fall speed at the last touchdown
};

} // namespace painful
