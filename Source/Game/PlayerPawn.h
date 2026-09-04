#pragma once
#include "../World/PhysicsWorld.h"
#include "Input.h"

#include <cmath>

namespace painful {

// The player's locomotion, rebuilt from the engine's own mover:
// PhysicsObject::PlayerAction (Engine.dll 0x10192260), the function that
// consumes the Tweak.PlayerMove block. The scripts only read the results
// (CPlayer keeps health and weapons, not velocity).
//
// Recovered facts, each from the decompile:
//  - The pawn is the "player_box" model at scale 0.155: 14.90 units tall
//    -> 2.31 m, 0.82 m wide (EngineGame::CreatePlayer, 0x1001cef0). The
//    collision body is its own shape and slightly shorter - see
//    kEyeAboveFloor.
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
//  - Air control is CPMA-style: the MOUSE steers a jump, the keys do not. The
//    input mask freezes at takeoff and is re-accumulated each frame against
//    the current camera basis, so turning reverses the travel at full speed.
//    Live input only cancels: the opposition is the live mask against the
//    takeoff mask - camera-independent, since both use the same basis - and an
//    opposite key bleeds speed by SlowdownDuringJump * speed * opposition
//    (halved while it exceeds the speed) until the player drops in place.
//  - MULTIPLAYER IS A SECOND MOVER, MultiPlayerAction (0x10194580), with its
//    own tweak block; its reversal drops speed to 1.0 outright.
//    Docs/Reference/PlayerMovement.md, "The two movers"
class PlayerPawn {
public:
    void Spawn(const float headPos[3]);

    // One movement step, with PlayerAction's own arguments: the action
    // bitmask the scripts set through ENTITY.PO_SetAction, and the camera
    // basis PLAYER.ExecAction passes.
    //
    // Only `right` shapes the ground direction. PlayerAction reads its
    // second Vector argument alone and derives forward by rotating it
    // (forward = (right.z, -right.x)), which is why looking up or down
    // neither slows walking nor drives the player into the floor - the
    // forward vector it is also handed carries a Y component and is not used
    // for this. Of the mask it consumes only Act::MoveMask.
    // Not const: a blocked pawn shoves the character in its way.
    void Move(PhysicsWorld& physics, const Tweaks& tweaks, uint32_t action,
              const float right[3], float dt);

    const float* headPos() const { return head_; }
    void SetHeadPos(const float p[3]);
    // A teleport addressed to the ENTITY, whose origin is the feet.
    void SetFloorPos(const float p[3]) {
        const float head[3] = {p[0], p[1] + kEyeAboveFloor, p[2]};
        SetHeadPos(head);
    }
    // The feet - ENTITY.PO_GetPawnFloorPos, the scripts' _groundx/y/z.
    void FloorPos(float out[3]) const {
        out[0] = head_[0];
        out[1] = head_[1] - kEyeAboveFloor;
        out[2] = head_[2];
    }
    bool onGround() const { return onGround_; }
    // The body's widest half-width, for the region overlap in TickTriggers.
    static constexpr float radius() { return kRadius; }
    // Which mover to be: the engine picks MultiPlayerAction for a multiplayer
    // session. Our port has no such session yet, so the -mp launch flag stands
    // in. Docs/Reference/PlayerMovement.md, "The two movers"
    void SetMultiplayer(bool on) { mp_ = on; }
    // Whether the LAST Move actually performed a jump. ENTITY.PO_JumpedInLastAction
    // answers with this: CPlayer:Tick plays hero_jump on it, and inferring it
    // from "left the ground" made every stair play the sound.
    bool jumpedLastMove() const { return jumpedThisMove_; }
    float currentSpeed() const { return speed_; }

    // The pawn's actual world velocity this frame, which is what
    // ENTITY.GetVelocity reports for the player.
    void Velocity(float out[3]) const {
        for (int c = 0; c < 3; ++c) out[c] = velocity_[c];
    }

    // ENTITY.SetVelocity on the player, which is a jump pad or a knockback.
    // It has to reach the mover's own state - writing the entity store only
    // fed the reader - so it takes off the way a jump does: vertical into
    // velY_, horizontal into the air direction and speed the air branch
    // steers with. jumpedThisMove_ stays false: a pad is not an input jump,
    // and the scripts' jump sound hangs off that.
    void SetVelocity(const float v[3]) {
        velY_ = v[1];
        const float h = std::sqrt(v[0] * v[0] + v[2] * v[2]);
        speed_ = h;
        airDir_[0] = h > 1e-4f ? v[0] / h : 0.f;
        airDir_[1] = h > 1e-4f ? v[2] / h : 0.f;
        takeoffMask_ = 0;   // the pad chose the direction, not held keys
        if (v[1] > 0.f) {
            onGround_ = false;
            groundedTime_ = 0.f;
        }
    }

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
    // The pawn's own geometry, from the shape factory rather than the model.
    // GetPawnHeadPos is `(this+0x20) * 0.9 + centre` and GetPawnFloorPos
    // `(this+0x20) * -1.1 + centre`, where that field is the body scale -
    // 1.0 for the player, which the mass confirms (80 = 0.2^3 * 10000 at
    // bodyScale 1). So the eye sits exactly 2.0 above the floor contact.
    //
    // Not 2.31: that came from scaling the player_box MODEL (14.90 units at
    // 0.155) and treating its half-height as the multiplier, which made the
    // player a noticeable 15% too tall. The four-sphere collision stack is
    // the authority, and it agrees - centres -0.63/-0.10/+0.50/+0.90 with
    // radii 0.33/0.40/0.40/0.20 span -0.96 to +1.10, just over 2 units.
    // Its widest radius, 0.40, is the body's, and the model's 0.82 width
    // matches it far better than a scaled 0.92 would.
    static constexpr float kEyeAboveFloor = 2.0f;
    static constexpr float kRadius = 0.40f;
    static constexpr float kPlayerMass = 80.f;     // (0.2)^3 * 10000

    // PhysicsObject::StepCheck (0x1018eb90) is a ladder of forward line
    // traces at fixed heights, and it returns which rung first came back
    // blocked. The heights are DOUBLES in Engine.dll at 0x102c8580..0x102c85f0,
    // measured from the pawn centre; the floor is centre - 1.1, so a rung at
    // centre + r sits 1.1 + r above the floor:
    //
    //   rung -1.096 -> 0.00 above floor -> returns 1, climbed at full speed
    //   rung -0.96  -> 0.14             -> returns 2, velocity x 0.3
    //   rung -0.68  -> 0.42             -> returns 3, horizontal x 0.3
    //   rung -0.24  -> 0.86             -> returns 4, a wall: stop dead
    //
    // So the engine climbs anything up to 0.86 above the floor and charges
    // for it by height, which is why a step in the original reads as a small
    // hop that differs with the step: the pawn rises in one frame while that
    // frame's travel is cut to 30%. A bare sphere only rolls over what its
    // own radius clears, which is why steps felt too low here.
    // Note these are hardcoded in the binary, not read from the tweaks.
    // StepCheck's only tweak-ish input is its float argument, and that scales
    // the FORWARD trace distance (0.42/0.532/0.646 of it below a threshold,
    // a flat 0.76 above), not the heights - so Tweak.PlayerMove.MaxStepHeight
    // (0.7) does not appear to reach here. Left unresolved rather than
    // assumed.
    static constexpr float kStepFree = 0.14f;      // free below this
    static constexpr float kStepMax = 0.86f;       // a wall above this
    static constexpr float kStepPenalty = 0.3f;    // 0x102af83c

    float head_[3] = {0, 0, 0};
    float velY_ = 0.f;
    bool onGround_ = false;

    // The bunny-hop state PlayerAction keeps on the physics object.
    float speed_ = 0.f;              // current target speed; 0 = uninitialised
    bool mp_ = false;                // MultiPlayerAction rather than PlayerAction
    float velocity_[3] = {0, 0, 0};  // last frame's actual travel, per second
    float groundedTime_ = 0.f;       // seconds since touchdown
    bool jumpLatched_ = false;       // PlayerAction's +0x1e: cleared on release
    bool jumpedThisMove_ = false;    // an actual jump, not just airborne
    uint32_t takeoffMask_ = 0;       // movement bits frozen at takeoff
    float airDir_[2] = {0, 0};       // last frame's travel direction (x, z)
    float landingImpact_ = 0.f;      // fall speed at the last touchdown
};

} // namespace painful
