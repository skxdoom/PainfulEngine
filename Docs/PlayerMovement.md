# Player movement — recovered from PhysicsObject::PlayerAction

The original moves the player in native code; the scripts only read the
results. The mover is `PhysicsObject::PlayerAction` (Engine.dll
**0x10192260** — found as the only gameplay referrer of the pawn-geometry
constants), with `MultiPlayerAction` (0x10194580) as the MP variant. The
port's `Source/Game/PlayerPawn` implements what follows.

## The pawn

`EngineGame::CreatePlayer` (0x1001cef0) creates the entity as the
`player_box` model at scale **0.155**. The model measures 5.32 × 14.90 ×
3.62 units, so the pawn is **0.82 m wide and 2.31 m tall**.

Positions are head-anchored (`h` = half-height 1.155):

| function | formula | address of constant |
|---|---|---|
| `GetPawnHeadPos` | centre + **0.9**·h | 0x102c8510 |
| `GetPawnFloorPos` | centre − **1.1**·h | 0x102c7c04 |

`SetPawnHeadPos` takes the EYE position — `Lev.Pos` is eye level, **2.31 m
above the floor contact** (the floor probe reaches 0.1·h below the box, which
is the ground clearance).

## Movement rules (single player)

- **Ground**: velocity = normalised wish direction × `currentSpeed`, an
  instant snap. There is no walk acceleration ramp.
- **Jump**: vertical velocity = `JumpStrength × PlayerSpeed × 0.7` (the 0.7
  at 0x102c8648). Stock tweaks give 5.6 m/s — an 0.8 m hop at gravity 19.62.
- **Bunny-hop** (the speed state lives on the physics object):
  - A jump pressed within `SecondsWhenYouCanBunnyHopBeforeLanding` before
    touchdown, or within `...AfterLanding` after it, is a hop:
    `currentSpeed += (MaximalBunnyHopSpeed − currentSpeed) ×
    BunnyHopAcceleration`, clamped at the maximum — an asymptotic approach
    to 15 m/s at stock values.
  - Grounded past the AfterLanding window, `currentSpeed` resets to
    `PlayerSpeed`.
- **Air**: the horizontal direction is FROZEN at takeoff (PlayerAction
  stores the takeoff direction and input mask). Input opposing it bleeds
  speed by `SlowdownDuringJump × speed × opposition × dt`, halved while the
  cut exceeds the speed — that bleed is the only air steering in SP.
- **Stairs**: a `StepCheck` gate; a blocked step scales velocity by 0.3
  (0x102af83c). Ice replaces the snap with a log-lerp steer
  (`IceSlideModifier`/`IceSlideAngleModifier`/`PlayerSpeedIce`), not yet
  ported.
- **Landing**: a touchdown with `fallSpeed × timeMultiplier > 20`
  (0x102c8690) queues **`PLAYER_HIT_GROUND`** into `Game_GetMsg` — fall
  damage is script-side.
- Ladders divert to `PlayerActionLadder` when either the head or the floor
  position is near one; moving platforms subtract the mesh-under velocity
  (`MeshUnder` probed 0.1·h below). Neither is ported yet.

## The speed natives

`SetPlayerSpeed(speed [, jumpStrength])` and `GetPlayerSpeed() -> speed,
jumpStrength` (0x1011dea0/0x1011df50) read and write the LIVE tweak fields —
`+0x0c` and `+0x14` of the physics engine's tweak block at `GEngine+0xd4` —
which is how demon mode and powerups retune movement. The tweak block is
filled by `PhysicsEngine::GetTweaksFromScript` (0x10185a80) in the declared
order of `Tweak.PlayerMove`.

## Not yet ported

Ice, ladders, moving platforms, underwater (`UnderwaterSpeed` family),
double-jump (`AbsoluteVerticalVelocityBelowWhichDoubleJumpHappens`), the
`PLAYER_HIT_GROUND` message, and MP movement (`MultiPlayerAction` has its own
tweak block with air acceleration).
