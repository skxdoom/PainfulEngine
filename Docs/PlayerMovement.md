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

## How the mover is reached

`PlayerAction` is not called by the engine loop; the scripts call it, once
per tick, from `CPlayer:Tick`:

```lua
local action = INP.GetActionStatus(self._Entity)
ENTITY.PO_SetAction(self._Entity, self.CurAction)
PLAYER.ExecAction(self._Entity, 0, fv.X,fv.Y,fv.Z, rv.X,rv.Y,rv.Z)
```

The signature is `PlayerAction(Vector& param_1, Vector& param_2)` — forward
and right. **Only `param_2` shapes the ground direction.** The accumulation
reads that vector's x and z and builds

| bit | contribution to (x, z) |
|---|---|
| Forward `&2` | `+(right.z, -right.x)` |
| Backward `&4` | `-(right.z, -right.x)` |
| Right `&0x10` | `+(right.x, right.z)` |
| Left `&8` | `-(right.x, right.z)` |

then normalises. Forward is right rotated a quarter turn, so the direction
stays in the ground plane whatever the pitch — which is why looking up or
down neither slows walking nor drives the player into the floor. The forward
vector it is also handed carries a Y component and is not used here.

The action mask lives on the physics object at `this+0x78`, and the mover
consumes only `0x3e` — Forward, Backward, Left, Right, Jump. Everything else
in the mask is the scripts talking to themselves through
`ENTITY.PO_IsActionState`.

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

## The bug the measurement caught

Running the mover on the scripts' mask made the movement measurable against
the tweak values, and it did not match. One cause, in the port rather than in
the recovered rules: **gravity was integrated into the slide even while
grounded.**

Two symptoms, which looked unrelated until the fix killed both:

- A standing player sank about 0.1 units a second, without limit — each
  frame pushed the sphere a little into the floor and `SlideSphere`'s skin
  lift did not quite give it back.
- The player walked at 6.80 m/s where `PlayerSpeed` is 8.0. The downward
  component made the slide contact the floor on every single frame, and the
  contact charged its 0.02 skin to the motion budget: 0.02 of a 0.1333 step
  is exactly the 15% that was missing.

Gravity now applies only off the ground, so a grounded step is purely
horizontal and never touches the floor mid-slide. Standing is exact, and
walking measures **7.9999 m/s**.

A note on how that was established, because the first diagnosis was wrong.
The obvious reading was that `SlideSphere`'s skin back-off was the culprit,
so it was changed to lift along the contact normal instead. Speed went to
8.0 — but stashing that change and rebuilding gave 8.0 as well, because the
gravity fix was in the tree too and was doing all the work. With gravity off
the ground there is no floor contact during a grounded slide, so the skin
never comes into it. The `SlideSphere` change was measured neutral on the
camera-push diagnostic (6.82 / 7.16 / 7.43 / 0.07 either way) and reverted:
shared collision code should not move on a hypothesis that measurement does
not support.

Jump rise measures 0.753 m rather than the 0.799 m the closed form gives.
That is the semi-implicit step, not an error: velocity is decremented before
the move, so the rise is `dt·Σ(v₀ − i·g·dt)` over the 17 rising steps, which
is 0.753 exactly.

Jump rise measures 0.753 m rather than the 0.799 m the closed form gives.
That is the semi-implicit step, not an error: velocity is decremented before
the move, so the rise is `dt·Σ(v₀ − i·g·dt)` over the 17 rising steps, which
is 0.753 exactly.

## Not yet ported

Ice, ladders, moving platforms, underwater (`UnderwaterSpeed` family),
double-jump (`AbsoluteVerticalVelocityBelowWhichDoubleJumpHappens`), the
`PLAYER_HIT_GROUND` message, and MP movement (`MultiPlayerAction` has its own
tweak block with air acceleration).
