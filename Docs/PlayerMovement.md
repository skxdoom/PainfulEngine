# Player movement — recovered from PhysicsObject::PlayerAction

The original moves the player in native code; the scripts only read the
results. The mover is `PhysicsObject::PlayerAction` (Engine.dll
**0x10192260** — found as the only gameplay referrer of the pawn-geometry
constants), with `MultiPlayerAction` (0x10194580) as the MP variant. The
port's `Source/Game/PlayerPawn` implements what follows.

## The pawn

`EngineGame::CreatePlayer` (0x1001cef0) creates the entity as the
`player_box` model at scale **0.155**. The model measures 5.32 × 14.90 ×
3.62 units, so the drawn pawn is **0.82 m wide and 2.31 m tall**. The
collision body it moves with is slightly shorter — see below.

Positions are head-anchored, off the body scale at `this+0x20`:

| function | formula | address of constant |
|---|---|---|
| `GetPawnHeadPos` | centre + **0.9**·bodyScale | 0x102c8510 |
| `GetPawnFloorPos` | centre − **1.1**·bodyScale | 0x102c7c04 |

`EngineGame::CreatePlayer` asks for `BodyTypes.Player` at **bodyScale 1.0**,
so the eye sits exactly **2.0 above the floor contact** and
`SetPawnHeadPos` takes that eye position — `Lev.Pos` is eye level.

Not 2.31. That figure came from scaling the player_box MODEL and treating its
half-height (1.155) as the multiplier, which makes the player a noticeable
15% too tall. The collision shape is the authority and it agrees with 2.0:
`FUN_101b3e20` builds a four-sphere stack at centres −0.63/−0.10/+0.50/+0.90
with radii 0.33/0.40/0.40/0.20, spanning −0.96 to +1.10 — just over two
units. Its widest radius, 0.40, is the body's half-width, and the model's
0.82 width matches that far better than a scaled 0.92 would. The mass
settles it independently: 80 = 0.2³ × 10000 at bodyScale 1.

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
- **Air**: what freezes is the **input mask, not the direction** — and that
  distinction is the whole of air control here. While grounded, PlayerAction
  stores both the travel direction and `action & 0x1e` on the physics
  object, refreshed every frame. Airborne, it rebuilds a direction from
  those stored bits through the *same* accumulation the ground branch uses,
  on **the right vector it was handed this call**. So the keys you were
  holding at takeoff keep applying, but relative to where you are looking
  now: hold a strafe and swing the mouse and the velocity comes with you,
  CPMA-style. Turning the motion back on itself bleeds speed by
  `SlowdownDuringJump × speed × opposition`, halved while the cut exceeds
  the speed, measured against the previous frame's direction — so a smooth
  turn is nearly free and a reversal is not.

  Reading this as a frozen world-space direction (as this port first did)
  removes air steering altogether and pins the player to any wall they
  jumped alongside, because nothing can turn the motion away from it.

  (An earlier note here wondered whether that bleed's last factor was the
  wish-normalisation scale rather than `dt`. **It is `dt`** — the same stack
  slot is reassigned at the top of the frame to the frame time, clamped by
  `if (1.0 < dt) dt = 0.05`. Resolved; the port already matched.)
- **Stairs**: see the step ladder below.
  Ice replaces the snap with a log-lerp steer
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

## The step ladder — `PhysicsObject::StepCheck` (0x1018eb90)

Steps are not a tweak, they are a **table of constants in the binary**, and
the engine grades a step by how tall it is rather than treating every
obstacle alike.

`StepCheck` fires a ladder of forward line traces at fixed heights off the
pawn centre and returns which rung first comes back blocked. The heights are
**doubles** at `0x102c8580`–`0x102c85f0` (read them with `ReadFloats.java`;
the float column is garbage, the values are 8 bytes). With the floor at
`centre − 1.1`, a rung at `centre + r` sits `1.1 + r` above the floor:

| rung | above floor | returns | PlayerAction's response |
|---:|---:|:--:|---|
| −1.096 | 0.00 | 1 | full speed over it |
| −0.96 | 0.14 | 2 | velocity × 0.3 |
| −0.68 | 0.42 | 3 | horizontal × 0.3 |
| −0.24 and up | 0.86+ | 4 | a wall: direction cleared, speed reset |

So the engine climbs **anything up to 0.86 above the floor** and charges for
it by height. That is what a step feels like in the original: the pawn rises
in a single frame while that frame's travel is cut to 30%, which reads as a
small hop whose size varies with the step — not an input jump, just the
step response. The 0.3 is `0x102af83c`.

The function's float argument scales the trace's FORWARD reach (0.42 / 0.532
/ 0.646 of it below a threshold, a flat 0.76 above), not the heights — so
`Tweak.PlayerMove.MaxStepHeight` (0.7) does not appear to reach here at all.
Left unresolved rather than assumed.

Measured on Cathedral after porting the ladder, walking a second in eight
directions: a climbing direction that was fully blocked (0.03 units) now
covers 7.33 and gains 1.42 height; another goes 5.46 → 7.23. Flat directions
are untouched at 7.98–8.00, and a descending one pays the penalty, 7.99 →
7.22.

## What the player collides with

`Tweak.PlayerMove.MaximalItemPushMass` (2500) is the line between what can be
**shoved** and what stops you. It is not a line between what is solid and
what is not — the player stands on a barrel. The port had the pawn's queries
inheriting the free camera's filter, which passes straight through any body
lighter than that mass so the camera can press into a prop and let its
kinematic probe shove it. The visible result was walking through barrels
while still standing on the heavier, pinned coffins. The pawn now asks for
solid props on every query; the camera keeps the affordance. Measured on
Cathedral: standing on `BarrelBig_007` settles the eye at 10.643 against
9.749 on the floor beside it.

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
