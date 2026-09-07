#pragma once
#include "../World/PhysicsWorld.h"
#include "../Core/Vectors.h"
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
//  - The mover is an impulse toward a target velocity, mass * f * (target -
//    v) once per frame: f = 0.2 walking, 0.5 on a step rung (with a vertical
//    target of 0.4/0.5/0.8 of the speed), 1 for a jump, StrongAirControl /
//    WeakAirControl in the air. So there IS a walk ramp, about ten frames.
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
	void Spawn(const Vec3& headPos);

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
			const Vec3& right, float dt);

	const float* headPos() const { return head_; }
	void SetHeadPos(const Vec3& p);
	// A teleport addressed to the ENTITY, whose origin is the feet.
	void SetFloorPos(const Vec3& p) {
		const Vec3 head{p[0], p[1] + kEyeAboveFloor, p[2]};
		SetHeadPos(head);
	}
	// The feet - ENTITY.PO_GetPawnFloorPos, the scripts' _groundx/y/z.
	void FloorPos(Vec3& out) const {
		out[0] = head_[0];
		out[1] = head_[1] - kEyeAboveFloor;
		out[2] = head_[2];
	}
	bool onGround() const { return onGround_; }
	// PLAYER.FloorCheck's own ray: FloorCheck(-8.5, 4.5) (0x10138da0), the
	// axis from the head to centre - 1.7, no random retry.
	bool floorCheck() const { return scriptFloor_; }
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
	void Velocity(Vec3& out) const {
		for (int c = 0; c < 3; ++c) out[c] = velocity_[c];
	}

	// A blast's impulse over the player's mass, added to what the pawn is
	// already doing: the rocket jump. Goes through SetVelocity so it reaches
	// the mover's own state.
	// The shape and mass, for the traces and blasts that treat the pawn as
	// the original's player body.
	static float Radius() { return kRadius; }
	static float EyeAboveFloor() { return kEyeAboveFloor; }
	static float Mass() { return kPlayerMass; }

	void AddVelocity(const Vec3& dv) {
		Vec3 v;
		Velocity(v);
		SetVelocity(v + dv);
	}

	// ENTITY.SetVelocity on the player, which is a jump pad or a knockback.
	// It has to reach the mover's own state - writing the entity store only
	// fed the reader - so it takes off the way a jump does: vertical into
	// velY_, horizontal into the air direction and speed the air branch
	// steers with. jumpedThisMove_ stays false: a pad is not an input jump,
	// and the scripts' jump sound hangs off that.
	void SetVelocity(const Vec3& v) {
		velX_ = v[0];
		velY_ = v[1];
		velZ_ = v[2];
		const float h = std::sqrt(v[0] * v[0] + v[2] * v[2]);
		speed_ = h;
		airDir_[0] = h > 1e-4f ? v[0] / h : 0.f;
		airDir_[1] = h > 1e-4f ? v[2] / h : 0.f;
		takeoffMask_ = 0; // the pad chose the direction, not held keys
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
	// GetPawnHeadPos = centre + 0.9, GetPawnFloorPos = centre - 1.1. The
	// stack's bottom is 0.96 below the centre and the body RESTS ON IT - a
	// dynamic body does not hover - so the floor point the scripts read sits
	// 0.14 under the mesh and the eye 1.86 over it. A 0.14 hover was tried
	// and put every rung 0.14 too high. PlayerMovement.md, "The pawn".
	static constexpr float kEyeAboveCentre = 0.9f;
	static constexpr float kFloorBelowCentre = 1.1f;
	static constexpr float kHover = 0.f;
	static constexpr float kRadius = 0.40f;
	static constexpr float kPlayerMass = 80.f; // (0.2)^3 * 10000
	// FloorCheck's ray ends 1.4 below the centre (0x102c85f4): 0.3 past the
	// floor contact, the window in which the engine still counts as standing.
	static constexpr float kFloorReach = 1.4f;

	// StepCheck's rung ladder for a wish direction: 0 clear, 1..3 a step at
	// the floor / 0.14 / 0.42 above it, 4 a wall. Rung table in the .cpp.
	int StepCheck(const PhysicsWorld& physics, const Vec3& centre,
			const float wish[2]) const;

	Vec3 head_;
	// The body's velocity, PlayerAction's `v` - persistent, since every
	// frame's impulse is measured against it.
	float velX_ = 0.f, velY_ = 0.f, velZ_ = 0.f;
	bool onGround_ = false; // floor ray or step: the grounded branch
	bool resting_ = false; // set on the floor by the probe
	bool stepping_ = false; // a rung answered 1..3 (flag bit 0)
	bool scriptFloor_ = false; // PLAYER.FloorCheck's ray
	// The floor's normal from the last floor ray that hit (PhysicsObject
	// +0x60), the too-steep counter on it (helper +0x70, 0..10; over 5 the
	// frame is treated as airborne), and FloorCheckRandom's generator.
	Vec3 floorNormal_{0.f, 1.f, 0.f};
	bool axisFloor_ = false; // the axis ray itself hit last frame
	int slopeCount_ = 0;
	uint32_t rng_ = 0x9e3779b9u;

	// The bunny-hop state PlayerAction keeps on the physics object.
	float speed_ = 0.f; // current target speed; 0 = uninitialised
	bool mp_ = false; // MultiPlayerAction rather than PlayerAction
	Vec3 velocity_; // last frame's actual travel, per second
	float groundedTime_ = 0.f; // seconds since touchdown
	bool jumpLatched_ = false; // PlayerAction's +0x1e: cleared on release
	bool jumpedThisMove_ = false; // an actual jump, not just airborne
	uint32_t takeoffMask_ = 0; // movement bits frozen at takeoff
	float airDir_[2] = {0, 0}; // last frame's travel direction (x, z)
	float landingImpact_ = 0.f; // fall speed at the last touchdown
	// Where the body centre was after the last move: MovePlayerOutOfWall's
	// stored position (PhysicsObject helper +0x54). A teleport clears it.
	Vec3 lastCentre_;
	bool haveLast_ = false;
};

} // namespace painful
