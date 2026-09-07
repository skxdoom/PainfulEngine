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
so the eye sits exactly **2.0 above the floor point** and
`SetPawnHeadPos` takes that eye position — `Lev.Pos` is eye level.

**The floor point is not the mesh.** The stack's lowest sphere bottoms out
at centre − 0.96, and a dynamic body rests on it, so `GetPawnFloorPos`
(centre − 1.1) reads 0.14 UNDER the ground the body stands on and the eye
stands 1.86 over it. The step ladder settles this: its rungs are fixed
offsets from the centre, and with the body resting on the mesh they sit at
−0.12 / +0.02 / +0.30 above it with the wall rung at 0.74 — the ceiling play
reports — where a 0.14 hover (this port's first reading, which kept the
floor point on the mesh) put them all 0.14 higher and let 0.86 be climbed.
Level starts place `Lev.Pos` 2.0 over the mesh; the body drops the 0.14 on
its first frame.

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

## The two movers, and the tweak blocks they read

There are two, and they are different functions with different rules:

| | |
|---|---|
| `PhysicsObject::PlayerAction` | 0x10192260 — single player |
| `PhysicsObject::MultiPlayerAction` | 0x10194580 — multiplayer |

`Game.GMode` selects between them (`GModes.MultiplayerServer` / `MultiplayerClient`
/ `DedicatedServer` versus `SingleGame`). **This port has no multiplayer session,
so `GMode` is always `SingleGame` (2) even on a DM map** — the `-mp` launch flag
stands in for the selection, and should be replaced by the real mode when
multiplayer lands.

Both read a tweak block off `*(GEngine + 0xd4)`. The blocks are two structs in
one object, and the offsets come from `PhysicsEngine::GetTweaksFromScript`
(0x10185a80), which stores each named field in order:

| offset | PlayerMove | | offset | MultiPlayerMove |
|---:|---|---|---:|---|
| +0x04 | SecondsWhenYouCanBunnyHopAfterLanding | | +0x5c | AbsoluteVerticalVelocity… |
| +0x08 | SecondsWhenYouCanBunnyHopBeforeLanding | | +0x60 | SecondsWhenYouCanBunnyHopAfterLanding |
| +0x0c | PlayerSpeed | | +0x68 | PlayerSpeed |
| +0x10 | BunnyHopAcceleration | | +0x6c | AccelerationWhenWalking |
| +0x14 | JumpStrength | | +0x70 | DecelerationWhenWalking |
| +0x18 | StairsUpSpeed | | +0x74 | BunnyHopAcceleration |
| +0x1c | StairsDownSpeed | | +0x78 | JumpStrength |
| +0x20 | MaximalItemPushMass | | +0x88 | MaximalBunnyHopSpeed |
| +0x24 | MaximalBunnyHopSpeed | | +0x90 | SlowdownDuringJump |
| +0x30 | SlowdownDuringJump | | +0x94 | StrongAirControl |
| +0x34 | StrongAirControl | | +0x98 | WeakAirControl |
| +0x38 | WeakAirControl | | +0xa0 | MinimalTimeBetweenBunnyHops |
| +0x3c…0x50 | Ice*, SlopeAngleToSlide, Shocked* | | +0xa4 | BunnyHopDifficulty |

The gaps are real — the layout is not tightly packed, and inferring it by
assuming it was gave the wrong field for `+0x30`. Read the stores, not the
Lua field order.

`StrongAirControl` and `WeakAirControl` ARE read by `PlayerAction`, as the
airborne impulse factor (0x1019420b / 0x10194221 — see the air rule below).
An earlier note here said they were `QWPhysics`-only; the decompiler had
lost the block.

### The difference that matters: reversing in mid-air

**Air control here is CPMA-style: the mouse steers a jump, the keys cannot.**
What freezes at takeoff is the **direction mask** (`action & 0x1e`), and it is
re-accumulated every airborne frame against the *current* camera basis. So
turning the mouse turns the motion — a 180° turn reverses your travel at full
speed — while no key press redirects it.

Live input reaches the airborne branch for exactly one purpose: cancelling.
Holding the opposite of the takeoff direction bleeds the speed until the player
drops in place; a perpendicular key does nothing; the takeoff key does nothing.

**The cancel is a KEY against the takeoff KEY, not a heading against a
heading.** The live wish vector and the frozen-mask vector are built from the
same camera basis, so their dot product depends only on the two masks —
`forward · backward = −(rx² + rz²) = −1` and `forward · left = −rz·rx + rx·rz =
0`, whatever the camera is doing. That is what lets the mouse steer freely
while an opposite key still drains the speed.

Two ways to get this wrong, both tried here:

- measure the live input against the **world-space** takeoff direction, and a
  180° mouse turn reads as a reversal — the player stops dead in mid-air, which
  breaks the steering the game is built around;
- refresh the frozen mask from live input, and the jump becomes key-steerable,
  which it is not.

The slowdown each mover then applies is spent differently:

```
single player   cut = SlowdownDuringJump(20) * speed * opposition * dt
                while (cut > speed) cut *= 0.5      -- never a full stop
                speed -= cut

multiplayer     cut = SlowdownDuringJump(9999) * opposition * dt   -- no speed term
                speed = cut < speed ? speed - cut : 1.0            -- dead stop
```

So both cancel a jump's direction, at different speeds. Single player bleeds
geometrically — a factor of `1 - 20·dt` per frame, so about 0.667 at 60 fps —
and multiplayer, where 9999 makes every reversal exceed the speed, clamps to
1.0 at once.

Measured in this port on C2L1_Bridge, jumping forward and then holding a key
from frame 0. The velocity's sign never flips in either case — the player slows
to a stop facing the way the jump went, rather than travelling backwards:

| frame | SP hold Back | SP hold Left | SP mouse 180° | MP hold Back |
|---:|---:|---:|---:|---:|
| 0 | 8.00 | 8.00 | 8.00 | 11.00 |
| 2 | 4.64 | 8.00 | 8.00 | 1.00 |
| 6 | 2.24 | 8.00 | 8.00 | 1.00 |
| 14 | 0.52 | 8.00 | 8.00 | 1.00 |
| 22 | 0.25 | 8.00 | 8.00 | 1.00 |

The last two columns are the checks that matter. A perpendicular press neither
steers nor slows. A 180° mouse turn keeps the full 8.00 and flips the travel
(`vz` +8 → −8) — steered, not cancelled.

## Movement rules (single player)

- **Ground**: the mover is **one impulse per frame toward a target**,
  `mass × f × (target − v)` with `target = wish × currentSpeed` and
  `f = 0.2` (0x102b3b80) on clear ground — so there IS a walk ramp, about
  ten frames to 90% and the same to stop, and it is per rendered frame with
  no `dt` in it. A step rung raises `f` to 0.5 and adds a vertical target;
  a jump is `f = 1`. The whole table is under "The step ladder" below. (An
  earlier reading here said "an instant snap, no ramp" — the decompiler had
  dropped the 0.2 block; the disassembly at 0x101943f0 has it.)
- **Jump**: vertical velocity = `JumpStrength × PlayerSpeed × 0.7` (the 0.7
  at 0x102c8648). Stock tweaks give 5.6 m/s — an 0.8 m hop at gravity 19.62.
- **Bunny-hop** (the speed state lives on the physics object):
  - A jump pressed within `SecondsWhenYouCanBunnyHopBeforeLanding` before
    touchdown, or within `...AfterLanding` after it, is a hop:
    `currentSpeed += (MaximalBunnyHopSpeed − currentSpeed) ×
    BunnyHopAcceleration`, clamped at the maximum — an asymptotic approach
    to 15 m/s at stock values.
  - Grounded past the AfterLanding window, `currentSpeed` resets to
    `PlayerSpeed`. That is the **ceiling** — it gives the hop bonus back.
  - There is also a **floor**, and it is unconditional: every grounded frame,
    `if (currentSpeed < PlayerSpeed) currentSpeed = PlayerSpeed`
    (`if (speed < Tweak+0x0c) speed = Tweak+0x0c`). Being on the ground is
    never slower than walking.

    Only the ceiling was ported at first, which was invisible until jump
    cancelling landed: a cancelled jump touches down at ~0.1 m/s, and with no
    floor the player kept that until the AfterLanding window expired and
    could not move. With it, walking speed comes back on the first grounded
    frame — measured 0.02 → 8.00 across a single frame.
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

  The air impulse has its own factor: **`StrongAirControl` (0.2) while
  Forward is held or a reversal cut was taken this frame, `WeakAirControl`
  (0.002) otherwise** — the byte at `ESP+0x6f` picks `tweak+0x34` or
  `+0x38` at 0x101941ef. Strafing alone in the air barely steers; forward
  plus the mouse does. The vertical delta is `dir.y × speed` = 0, so the
  air never touches the fall.

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

## The three helpers around the body

`PlayerAction` does not slide a shape; the rigid body does the colliding and
three helpers correct it, all of them **line traces** built through the same
ray-cast input (`FUN_101A6590` → `FUN_1020C720`) with the player's own body
taken out of the intersection solver for the duration (`FUN_101FB9A0(2,1)` /
`FUN_101FB850`, guarded by `CanLineTraceCollision`):

- **`FloorCheck(reach, ?, float* floorY, bool, bool useCached)`** (`0x1018F740`)
  — one ray straight down from the body. A hit within reach answers true,
  writes the hit height to `floorY` and stores the surface normal at
  `PhysicsObject+0x60..0x68` (the slope the movers read). With `useCached`
  and a ragdoll attached it answers the cached flag at `+0xb0` instead.
  `PlayerAction`'s own inline copy (0x10192a35) runs from the head
  (`centre + 0.9`) down to **`centre − 1.4`** (`0x102c85f4`): 0.3 past the
  floor contact. That 0.3 is the window in which the engine still counts
  the body as standing — a step's hop stays grounded, and a jump pressed
  while falling through it fires at once, which is the bunny hop.
  `FloorCheckMP` (`0x10189320`) and `MonsterFloorCheck` (`0x1018FAA0`) are
  variants of the same ray. **`FloorCheckRandom(bottom, top)`**
  (`0x1018FF60`) is the same ray at a random horizontal offset: `x` and `z`
  each `(rand / 32767 − 0.5) × 4.0 × 0.2 × bodyScale`, so within ±0.4, from
  `centre + top × 0.2` down to `centre + bottom × 0.2`. `PlayerAction` calls
  it with `(−7.0, 4.5)` — the same head-to-`centre − 1.4` span — whenever
  its own axis ray misses, which is what keeps a steep slope grounded.
- **`StepCheck(Vector, float)`** (`0x1018EB90`) — the ladder of forward rays
  at fixed heights, below.
- **`MovePlayerOutOfWall()`** (`0x10190890`) — the unstick. The object keeps
  the **last position it was in** (`+0x54..0x5c` on its helper, refreshed at
  the end of every call that finds nothing wrong). A ray from that stored
  position to where the body is now, and if it crosses geometry the body is
  put back at the hit, pushed out along the surface normal — a body that a
  step, a moving mesh or the solver's own tolerance left inside a wall walks
  back out along the path it came in by. Two thresholds gate it: the move is
  shorter than **3.0** (`0x102AEEF0`; a longer one is a teleport, not a
  wedge) and the hit fraction is past **0.4** (`0x102C862C`).

The port's pawn is a swept query instead of a corrected body, but it now
runs the same unstick at the end of every move: `PlayerPawn` keeps the
previous centre, traces from it to the new one against the static world
(`RayCast` static-only - props the player is pushing are not walls), and on
a crossing puts the centre back at the hit plus the body's widest radius
along the normal, under the same two gates; a teleport (`SetHeadPos`)
clears the stored position. `Depenetrate` (one deepest overlap per pass,
four passes) still handles the shallow overlaps a crossing test cannot see.
`FloorCheck`'s ray is ported as is (head to `centre − 1.4`, deciding the
grounded branch), a short downward sweep decides whether the body is
resting at its hover, and `StepCheck` is ported as the ladder below.

## The step ladder — `PhysicsObject::StepCheck` (0x1018eb90) and its response

Steps are not a tweak, they are a **table of constants in the binary**, and
the engine grades a step by how tall it is rather than treating every
obstacle alike.

`StepCheck(dir, arg)` fires line traces from the body centre along the
normalised wish direction at fixed heights, highest rung first, and returns
the first rung that comes back blocked. The heights are **doubles** at
`0x102c8570`–`0x102c85f0` (read them with `ReadFloats.java`; the float
column is garbage). The float argument is **1.0 at the only call**
(`PUSH 0x3f800000` at 0x10193b85), and it only picks the reach of the three
low rungs (`3.8 <= arg × 3.23 / 2.66 / 2.1` would give 0.76; at 1.0 it is
`arg × 0.646 / 0.532 / 0.42`). With the floor contact at `centre − 1.1`:

| rung (off the centre) | above the floor contact | reach | returns |
|---|---|---:|:--:|
| −0.075, −0.24, 0, +0.24, +0.48, +0.72, +0.96 | 1.03, 0.86, 1.10 … 2.06 | 0.76 | 4, a wall |
| −0.68 | 0.42 | 0.646 | 3 |
| −0.96 | 0.14 | 0.532 | 2 |
| −1.096 | 0.00 | 0.42 | 1 |
| nothing blocked | | | 0 |

**The response is an impulse, not a climb.** `PlayerAction`'s tail (the
switch at 0x10193c98, cases at 0x10193f55 / 0x10193dd3 / 0x10193d8a /
0x10193cab, the default at 0x10194095) builds `target − v` in four stack
slots, scales it, multiplies by `GetMass` (0x101888a0, `1 / invMass`) and
hands it to the body's impulse thunk (0x101889b0, vtable `+0x5c`). Every
frame, with no `dt` anywhere in it:

| rung | f | horizontal target | vertical target |
|:--:|:--:|---|---|
| 0 | 0.2 (`0x102b3b80`) | wish × speed | none — `v.y` is left alone |
| 1 | 0.5 (`0x102ae5b0`) | wish × speed | 0.4 × speed (`0x102c862c`) |
| 2 | 0.5 | 0.3 × wish × speed (`0x102af83c`) | 0.5 × speed (`0x102ae5b0`) |
| 3 | 0.5 | 0.3 × wish × speed | 0.8 × speed (`0x102b24ac`) |
| 4 | 0.5 | 0 — the velocity halves | none; the stored direction is zeroed and `currentSpeed` reset to `PlayerSpeed` |
| jump | 1 | wish × speed (× 0.3 on rung 2, 0 on rung 4) | `jumpVel` |

A jump never fires against rung 3: case 3 does not test the jump flag, so
pressing jump into a 0.42 step gives the step's kick instead. Speed here is
`currentSpeed`, so a bunny-hopper is kicked harder.

**The switch runs in every branch.** Cases 1–4 test only the jump flag
(`AL`), never the airborne one — the airborne bit only picks the default
case's factor. So shins meeting a ledge in flight get the same kick, and
the step flag it sets makes the next frame grounded: that is how a jump
that falls short of a ledge still ends on top of it. Airborne, the
direction handed to `StepCheck` is the mask-rebuilt air direction.

So a step is a **kick that lasts while the rung stays blocked**: half the
gap to 0.4 / 0.5 / 0.8 of the walking speed per frame, the walk cut to 0.3
of the wish meanwhile, and when the rung clears nothing takes the vertical
speed away — gravity brings the body down onto the step, and the hop grows
with the step. The floor ray reaches 0.3 past the floor contact, so the
frame stays grounded through the hop and the walk keeps steering.

Ghidra's decompile hides all of this — the case bodies collapse to a few
scaled stack slots — and the earlier reading here ("rises in a single frame
while that frame's travel is cut to 30%") came from that. The disassembly
at 0x10193c98..0x10194517 is the evidence
(`PainfulEngineHelpers/ghidra/steptail.txt`). The same block also shows the
pre-switch impulse at 0x10193c0a is a **moving-platform** cancel (the mesh
under the body faster than 5.0, jump not held, `v.y > 0` → `−mass × v.y`),
not a jump cut: `local_c0..b8` is `MeshUnder`'s velocity, zero on plain
ground.

### The port

`PlayerPawn::StepCheck` is the table above, against the static world only:
a loose prop in the way is pushed by the body's contact rather than answered
as a wall. (The engine's trace, `FUN_101ff410`, walks the world wrapper's
registered line-trace collidables; whether items register is not recovered.
Walking pushes props in the original, which a wall answer would forbid, so
they are assumed not to.) The velocity is state, since each frame's
impulse is measured against it, and the factors are spent per 60 Hz frame
(`1 − (1 − f)^(dt × 60)`) because the original applied them once per
rendered frame. After the sweep the body's velocity is re-read from the
displacement, so a contact removes the component into it as Havok's
contact did. `StairsUpSpeed` / `StairsDownSpeed` (1.0 in the tweaks) are not
read by `PlayerAction` and are not ported. `PAINFUL_PAWN_TRACE=1` prints one
line per move — start, delta, where the sweep ended, whether it rests and on
what normal, the final centre — which is how the corner wedge was seen.

Two port choices sit on top of the recovered law:

- **The walk factor is doubled, 0.4 instead of the binary's 0.2**, for both
  setting off and stopping. The original's stop read as instant in play,
  which the impulse alone does not give — Havok's contact friction on the
  body did the rest, and the body's coefficient is not recovered — and 0.4
  is what felt right without it: the coast after a release is about 0.3
  units instead of 0.67, and the walk reaches 90% in four frames instead of
  ten. `PAINFUL_WALK_FACTOR` overrides it. It does not touch a slope steep
  enough to slide, where the creep is still balanced against the recovered
  0.2, nor the step rungs, the jump or the air.
- **The velocity the scripts read is the COMMANDED one**, the impulse law's
  result before the sweep's contacts take their share. A kerb's kick
  commands 0.3 of the walk, so `CPlayer`'s "moving faster than 2" holds
  through the climb; read after the contact it dipped to 0.5–1.9 for two or
  three frames at every kerb, and the weapon's walk animation restarted
  each time — the jitter on the Bridge's spawn strafing across its gutters.
  A wall halves the command itself, so pressing into one still reads as
  standing. Measured strafing left 2 s and right 4 s from the Bridge spawn:
  the only sub-2 frames left are the presses into the gutter wall.

Measured on `TestFloor` (`mklevel` with steps
`0.07,0.14,0.25,0.42,0.60,0.80,0.86,1.00`, walking +X at 8):

| step | sweep ladder | impulse |
|---|---|---|
| 0.07–0.25 | placed on top in one frame | kicked at 1–2 m/s, floor pos peaks 0.12–0.14 over the top |
| 0.42 | wedged 12 frames, then placed on top | kicked at 2.9–3.3 m/s, on top in 12 frames, peaks 0.10 over |
| 0.60 | blocked for good | climbed |
| 0.68 / 0.72 (alone, on flat ground) | blocked | climbed |
| 0.76 / 0.80 (alone, on flat ground) | blocked | a wall: the 0.74 rung, the body stops dead |
| 0.80 / 0.86 / 1.00 in the row | blocked | climbed — but only because the pawn arrives FALLING off the previous box, with its rungs 0.6 higher; the box top then lies between its shin rungs and the airborne kick takes it |

(The single-step figures come from `mklevel` levels with one box each; the
row's boxes are 5 apart, too close for a grounded approach at 8 m/s.)

Walking from rest: 3.9 → 5.9 → 6.9 → 7.45 → 7.7 m/s over the first fifteen
frames, the 0.2 ramp. Pushing (`skrzynia_mala`), the standing jump (1.08
apex), explosion, throwable and fall damage all measure as before.

Airborne: walking off the 0.80 box and falling (`FloorCheck` false, 2.9 m/s
down) into the 0.86 box's face, the shins' rung kicks the body to 2.5–3.1
m/s up, the frame reads grounded through the step flag, and it lands on top
— before the air branch ran no ladder and the body just slid down the face.

### Slopes

`PlayerAction` compares the floor normal's y with `cos(SlopeAngleToSlide)`
(0x10192d06, the tweak at `+0x48`, 60° stock) on every frame the floor test
hits: a walkable floor zeroes a counter on the helper (`+0x70`), a steeper
one counts it up to 10, a miss counts it down. Over 5 the frame is walked
as **air** — the weak factor, no walking — while the body slides under
gravity. Below that the floor is ordinary ground: full control, a jump, and
whatever Havok's contact friction makes of standing there. The mountain-goat
climbing of steep slopes is the step ladder above, whose shin rungs hit a
slope like a stair.

Two things keep the floor test alive on a steep slope, where the axis ray
misses because the sphere's contact is off to the side:
`FloorCheckRandom(-7.0, 4.5)` retries the ray at a random offset within
±0.4 in x and z (see the helpers section), and the step flag counts as
grounded.

The port has the counter, the random ray and the ladder. The slide itself
is a **stand-in**: Coulomb friction at the level's `DefaultMeshFriction`
(0.7 on every shipped level checked, so a slope holds to 35°), the excess of
gravity along the slope spent as downhill acceleration; the body's own
coefficient (`hkpRigidBodyCinfo` in the sizer, `FUN_101b3e20`) is not
recovered, and Havok combines the two. Standing still, the 0.2 walk impulse
toward zero balances it at a creep — about `a / 12` m/s, the slow slide of
play. It runs on the floor ray's face normal when the axis ray hit, and on
the resting probe's contact normal otherwise (a ledge's corner).

Measured on `mklevel` ramps (`r<degrees>` in the step list; a 4-unit wedge)
after walking onto each and releasing:

| slope | standing still |
|---:|---|
| 10° / 20° / 30° | holds, 0.000 m/s |
| 40° | slides at 0.13 m/s |
| 50° | slides at 0.33 m/s |

Before two fixes every slope slid — 0.07 m/s at 10°, 0.37 at 20°, 0.70 at
30° — and a ledge edge pushed the body off, both from **position
corrections read back as velocity**. The resting probe was a full slide
(three iterations): its remaining travel was projected along the slope
and took the probe down the face, the body was set into the slope by that
much, and the next frame's depenetration pushed it out along the tilted
normal — 0.005 downhill per frame at 30°. The probe is now a single cast
(`SlidePlayer(..., iterations = 1)`), and the depenetration and unstick
displacements are subtracted before the velocity is read from the frame's
displacement: a correction is not motion.

### Jump is a LATCH, not an input edge

`PlayerAction` tests the Jump bit at its level and gates it on a one-byte latch
at `PhysicsObject+0x1e`:

```
uVar17     = *(uint *)(this + 0x78) >> 5;   // the action mask, bit 5 = Jump
bStack_fb  = uVar17 & 1;                    // LEVEL, not an edge
if ((uVar17 & 1) == 0) pfVar2[0x1e] = 0;    // released -> latch cleared
...
bVar8 = (bStack_fb && grounded && pfVar2[0x1e] == 0) || bunnyHop;
if (tweak[+8] < pfVar2[7]) pfVar2[0x1e] = 1; // rising -> latch set
```

`tweak[+8]` is `SecondsWhenYouCanBunnyHopBeforeLanding`, fixed by its
neighbours: the jump velocity is built from `tweak[+0x14] * tweak[+0x0c] * 0.7`
= `JumpStrength * PlayerSpeed * 0.7`.

So **holding jump does not bounce you** - the latch closes as soon as you are
rising and only a release opens it - but **a press made in the air and still
held fires the moment you land**, however long that takes. The port had an
input edge plus a fixed before-landing buffer, which drops exactly that case:
press early, keep holding, land after the window, nothing happens. Measured
over 460 frames: held jump gives one jump either way; press-in-air-then-hold
gave no landing jump before and jumps on landing now.

`ENTITY.PO_JumpedInLastAction` reports whether the mover actually applied the
jump velocity - not "left the ground", which a step-up also satisfies and which
made every stair play `hero_jump`.

### What the sweep ladder got wrong, and why it is gone

The first port of the ladder was a swept-shape retry: on a blocked frame, try
the move again from `startY + 0.86` and drop back onto whatever is there. It
climbed by teleporting the body onto the step in one frame — the "almost
teleports to the ledge" from play — and it had to be fenced against sliding
along walls and against resting on edges (measured then: 712 frames with
vertical movement out of 800 wedged against one obstacle on Cathedral). With
the response recovered as an impulse those fences are unnecessary: a wall
rung halves the velocity and there is no retry to misfire, and a body that
cannot rest on a step is simply not lifted onto it.

One fact from that work stands: **`PO_JumpedInLastAction` has to mean an
actual jump.** It was inferred as "was grounded, now is not", which a step-up
satisfies — and `CPlayer:Tick` plays `hero_jump_1/2` on it, which is where the
sound on a stair came from. `PlayerPawn` records whether the move really
applied the jump velocity.

## `ENTITY.SetVelocity` on the player is a launch, not a store

The player has no script physics body — it is the pawn — so the native needs
the same special case `GetVelocity` already had. Writing `Entity::velocity`
alone put the value where only the reader could see it, and the mover carried
on as if nothing had happened.

`PlayerPawn::SetVelocity` takes off the way a jump does: the vertical
component into `velY_`, the horizontal into `airDir_`/`speed_` for the air
branch to steer with, `takeoffMask_` cleared (the launcher chose the
direction, not held keys), and `onGround_` released when the launch is
upward. `jumpedThisMove_` deliberately stays false — a pad is not an input
jump, and the scripts hang `hero_jump` off that flag.

This is the whole of `JumpPad:OnEnter`, which sets `JumpStrength / 45` as the
vertical velocity. DM_Cursed's first pad declares `1800`, so 40 m/s — an
apex around 40 m at gravity 19.62, against 5.6 m/s and 0.8 m for a standing
jump. The arithmetic is the shipped script's own; whether the original felt
that high is untested here.

## The jump height that does not add up

**Confirmed from the binary**, and matching this port exactly:

- `jumpVel = JumpStrength × PlayerSpeed × 0.7 / worldTimeScale`, the `0.7` read
  at `0x102c8648` (0.699999988) and the multiply seen at `0x10193c85`.
- `PlayerSpeed` there is the **tweak constant**, not `currentSpeed`, so speed
  never raises a jump.
- Gravity is the world's `GlobalData.Gravity` (2 × 9.81). `EngineGame::CreatePlayer`
  builds the body with `CreatePhysicsObject(entity, 100, 1.0, -1, true)` — no
  per-body gravity factor.

That gives 5.6 m/s and an apex of 0.80 m, and this port measures 0.776 m. **But
the original plays higher than that**, and a play-test puts it near ×1.15.
The cause is not found. Ruled out so far:

- a per-body gravity factor (there is none at creation);
- `PromodePlayerMove` — `PlayerAction` reads only `0x00`–`0x58` and never the
  Promode block at `0xe8`–`0x11c`, and that block describes a friction-and-
  acceleration model `PlayerAction` has no terms for.

Still open. The jump is handed to Havok as a **velocity delta toward a
target** (`target − currentVelocity`, built at `0x10193cb3`) with the factor
1, which is an assignment in effect — and the `StepCheck` branches are now
traced in full (the step ladder section), so there is no hidden extra there.
One candidate the recovered floor ray offers: it reaches 0.3 past the floor
contact, so a jump pressed while still falling through that window fires
0.3 above the ground and tops out that much higher; a measurement of chained
jumps in the original would have included it.

### Consecutive jumps top out at the same height

Observed in the original, and the binary says why: a jump can only fire on a
grounded frame (`jumpHeld && !wasAirborne && !latched`), a press within
`SecondsWhenYouCanBunnyHopBeforeLanding` is queued to the touchdown frame, and
the launch speed is a constant. So every jump in a chain leaves from the floor
at the same speed and reaches the same ceiling — it reads like a spring.

### The stand-in

Until the real rule is found, this port reproduces the SHAPE of that:

| | launch | from |
|---|---|---|
| standing jump | formula × `kStandScale` (1.16) | the floor |
| bunny hop | formula, unscaled | the floor ray's window, up to 0.3 above the floor |

The hop's window is the recovered one (FloorCheck's ray to `centre − 1.4`),
which replaced a chosen 0.276 that had been set so both reached the same
apex; a hop fires on the first grounded frame while falling through it.

Measured apexes over a hop chain: **1.048, 1.000, 1.021, 0.997, 1.020** — the
first standing, the rest hops, repeatable across runs. Hops land a little under
the standing jump because the probe fires on the first frame it finds floor in
reach, which is usually short of the full lift.

**Both numbers are placeholders.** `PAINFUL_JUMPSCALE` overrides the standing
scale. When the real rule turns up, both constants come out — a magic
multiplier is a placeholder for an answer, not an answer.

One lead worth recording: play-testing settled on **1.16**, and
`PromodePlayerMove.JumpStrength` is **1.16** exactly. That block is not read by
`PlayerAction` and its other values do not match (PlayerSpeed 9.0, gravity
3×9.81), so this is not an explanation — but a play-tested multiplier landing
on a shipped constant to two decimal places is a strange coincidence, and the
next attempt should start by asking where else 1.16 could reach the campaign.

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

**The pawn slides with the original's shape.** `Engine.dll` has no Havok
character proxy (no `hkpCharacterProxy` string anywhere); the player is a
dynamic rigid body of the sizer's four spheres with a density mass of 80,
whose velocity `PlayerAction` re-commands every frame, with `StepCheck`,
`FloorCheck` and `MovePlayerOutOfWall` around it. The port runs the same
impulse law on a velocity of its own and sweeps the collision shape with it:
`PlayerPawn::Move` sweeps `BodyTypes.Player`'s four-sphere stack
(`PhysicsWorld::SlidePlayer`, the same cast as `SlideSphere` with the
compound shape) about the body centre, eye − 0.9. The stack RESTS on its
bottom sphere, the sweep's 0.02 skin over the mesh, as a dynamic body does;
a 0.14 hover that kept `GetPawnFloorPos` on the mesh was tried first and put
every rung 0.14 too high (see "The pawn"). A short probe down decides
whether the body is resting and sets it on the floor; it never grounds a
RISING body, since at 120 fps a jump's first frame lifts the stack 5 cm and
grounding it there zeroed the jump on the spot. The head sphere meets
ceilings and the shin sphere meets ledges as they did in the original,
where a single feet sphere let the camera into ceilings and wedged on
geometry.

Two query details cost real height and real wedges before they were found:

- **Jolt's shape queries take the centre-of-mass transform**, and the
  four-sphere stack's centre of mass is 0.059 above its origin. Passing the
  origin sank the stack by that much in every cast and overlap test, which
  is why the player rested 0.06 high on every floor (`RShapeCast::
  sFromWorldTransform`, `shape->GetCenterOfMass()`).
- **A shape left exactly touching casts as a hit at fraction 0 whichever
  way it goes.** A sphere settled onto a ledge's corner by a vertical probe
  was within the skin diagonally, so every later cast — even straight away
  from the corner — reported a hit and the body could never slide off.
  `Depenetrate` now reports pairs nearer than 0.01 (`mMaxSeparationDistance`)
  and pushes them out to that gap.

**Ledge corners.** The resting probe returns its contact normal (`SlidePlayer`'s
`hitNormal`), and the slope slide runs on it: a sphere hanging over a
ledge's corner sees a tilted support and slides off once the tilt beats the
friction angle, as the original's body did — it could not stand with its
axis past an edge. Measured on the 0.86 box: the axis 0.16 past the edge
holds (support tilt 29°); 0.21 past (40°) slides and drops within twenty
frames. `PLAYER.FloorCheck` meanwhile is its own axis ray to `centre − 1.7`
with no random retry (`FloorCheck(-8.5, 4.5)` at 0x10138da0), so the
scripts see the drop as the body leaves the edge, not a flicker while it
stands.

**Pushing is the rigid-body contact, per frame.** When the swept body is
held short of its ask by a dynamic prop, `PushProps` gives every prop the
stack is pressing on `dv = M / (M + m) × (speed − have)` along the walk, the
contact impulse of an 80 kg body re-commanded at `speed`: a light barrel
reaches the player's speed in a few frames, a heavy crate creeps (and floor
friction holds it below the player), and anything over
`Tweak.PlayerMove.MaximalItemPushMass` (2500) is a wall. Characters keep
`ShoveCharacters`. The solver body the player wears (`CreatePawnProbe`) is a
kinematic **sensor** in the same silhouette: it reports what strikes the
player and pushes nothing, so a thrown can passes through the player after
reporting rather than bouncing off. Two earlier models were wrong: a solid
kinematic pusher shoved a barrel at full speed whatever it weighed (infinite
mass - and before that the free camera's 1.2 pusher did the same), and a
dynamic pusher chasing the pawn pushed almost nothing, because the feet
sphere stopped the pawn at the prop's surface and the pusher arrived with no
penetration to spend. Headless (TestFloor, `push_probe.lua`): walking into a
`BarrelBig` sends it ahead at the player's pace.

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

## UI actions: some are HELD, most fire once per press

`INP.UIAction(mask)` (`0x1011C9B0`) is a plain `(mask & arg) != 0` against
`InputDevice+0x3458`; `INP.Action` is the same against `+0x3454`. Both natives
only read, so the semantics live in how that mask is maintained — and what
writes `+0x3458` is NOT recovered. The displacement search does not match its
addressing form, so this is settled from the shipped scripts and from play:

- **Held.** Holding Tab shows the scoreboard and releasing it hides it, in the
  original. `Game.lua:489` and `GameMP.lua:474` both read it as a level:
  `if UIAction(Scoreboard) then Show() else Hide() end`, and `Hud._showSPStats`
  drives a per-frame draw.
- **Once per press.** QuickSave, QuickLoad, Screenshot, SayToAll/Team and
  Flashlight all act immediately with no guard of their own. The decisive one
  is `PlayerLight.CLight`, where the shipped data has

  ```lua
  if INP.UIAction(UIActions.Flashlight) then
      if self.Type == 3 then self.Type = 0 else self.Type = 3 end
      SOUND.Play2D("misc/flashlight-button")
  --    INP.RemoveUIAction(UIActions.Flashlight)
  ```

  with the consume **commented out**. That only works if the engine already
  gives the action one press at a time; as a level it toggles the light and
  plays the click every frame the key is down.

`INP.RemoveUIAction` clears a bit early, which the MP scoreboard needs
(`GameMP.lua:471`, `487`): it reads a HELD action and must not re-fire the
toggle on the next frame while Tab is still down. A consumed bit comes back
when its key is released.

STAND-IN: which actions are held is inferred from the above, not recovered —
Scoreboard and Zoom are held, the rest are triggered. Finding the writer of
`InputDevice+0x3458` would replace this with the engine's own table.

## Reach is measured to the body's AXIS — `PLAYER.GetDistanceFromPoint`

The native (`0x101393F0`, found through the `{name, fn}` table in `.rdata` at
`0x102C2680` — Ghidra finds no code reference to the name string) forwards to
`PhysicsObject::GetDistanceFromPoint` (`0x1018CF70`), and that is **not a
point-to-point distance**. It holds a second point at `PhysicsObject+0x5c`,
projects the query onto the segment between it and the body position, clamps
to the ends, and measures to the nearest point on it. For the player that
segment is the body axis, feet to head. A handle that is not a live player
answers `1e7` — infinitely far, so every distance test fails rather than
passing on a zero.

That matters because `CItem:CheckDistFromPlayers` asks about
`self.Pos.Y - 1`, so an item lying on the floor asks about a point 0.9 BELOW
it. Against the segment the nearest end is the feet, 0.9 away, inside
`CoinG.takeDistance` of 1.6. Measured from a single point at the body centre
it is 1.8 away — and **no coin in the game can be picked up, from anywhere**,
which is exactly what the port did. Every item is reached more easily now;
the `-1` is there to meet a floor-level pickup, not to raise the bar.

## Who owns the camera

Not the pawn. `Game:Tick2` gates the script camera on three things:

```lua
if Player and self.CameraFromPlayer and MOUSE.IsLocked() then
    Game:UpdateViewFromPlayer()
```

and `UpdateViewFromPlayer` handles a disabled pawn itself:

```lua
if ENTITY.PO_IsEnabled(Player._Entity) then
    destPos = ENTITY.PO_GetPawnHeadPos(...) - PLAYER.GetCameraFix(...)
else
    destPos = ENTITY.GetPosition(Player._Entity)     -- the entity, not the eye
end
...
crx = crx + mdx ; cry = cry + mdy                    -- outside the branch
CAM.SetAng(crx, cry, 0)
```

So three states, and only one of them flies:

| | `PO_IsEnabled` | `CameraFromPlayer` | mouse | camera |
|---|---|---|---|---|
| walking | true | true | locked | follows the pawn's head |
| **dead** | false | true | **locked** | **frozen at the entity, still rotates** |
| **end of level** | false | **false** | locked | **frozen entirely — nothing writes it** |
| fly (`SwitchPlayerToPhysics`) | false | true | **unlocked** | the engine's free camera |

`CPlayer.Client_OnDeath` and `EndLevel:Update` both call
`ENTITY.PO_Enable(player, false)` and **neither touches the mouse lock**;
`EndOfLevel:OnTake` additionally sets `Game.CameraFromPlayer = false`. The only
thing that unlocks the mouse during play is `Game:SwitchPlayerToPhysics`, which
also parks the player entity at `(0, -400, 0)` and is itself reachable only
behind `not IsFinalBuild()` — from `Console.lua`, or `Keys.F` with the editor on.

**The port therefore gates its free camera on `mouseLocked()`, never on
`pawnEnabled()`.** Gating on the pawn sent the view into free flight on death
and in the end-of-level teleport, because both drop the pawn without unlocking.
Measured with the pawn disabled mid-run: the camera snaps from head height
(`ent.y + 2.0`) to the entity origin and then holds it exactly, frame after
frame, with `MOUSE.IsLocked()` still true throughout.

The engine-side noclip (the `N` key) is the developer twin of that script fly
mode and needs `-dev`, for the same reason `IsFinalBuild` guards the other one.

## `INP.Reset` consumes a press until the key is released

The original's input is **event-driven, never polled**. `InputSystem` keeps one
state per key — `0` up, `1` pressed this frame, `2` held, `3` released — and
`ProcessEvents` (`0x1003e670`) moves `0 -> 1` only on a **down event**:

```c
if (*(int *)(this + i * 8 + 0x3a68) == 0) { *(...) = 1; }   // a down event
if (*(int *)(this + i * 8 + 0x3a68) == 1) { *(...) = 2; }   // held
```

`InputSystem::Reset` (`0x1003a6c0`) walks the list of keys currently non-zero,
zeroes each one, and empties the list. A key still physically held is therefore
back at `0` and **cannot reach `1` again until it is released and pressed
afresh** — there is no polling path that could re-arm it.

That is what the scripts are relying on when they act on an action and then call
`INP.Reset()` to consume it. `EndLevel:Tick` is the clearest case:

```lua
if INP.Action(Actions.Fire) then
    INP.Reset()
    if self.statStep > 10 then ... self:LastClick()   -- second click: leave
    else self.statStep = 11 ; INP.Reset() end          -- first click: show it all
```

**We poll** — `GameApp` pushes the window's key array into `Input` every frame —
so `Reset()` alone was undone by the next `SetKeyDown`. One held click is
several frames, so the first click set `statStep = 11` and the *next frame*
took the exit: the stats crawl was skipped and left in the same motion.
`Input::Reset` now marks every key that is down as suppressed, and
`Input::SetKeyDown` clears that only on an up, which reproduces the state
machine's effect. Guarded by six checks in `PainfulTools selftest`.

## Not yet ported

Ice, ladders, moving platforms, underwater (`UnderwaterSpeed` family),
double-jump (`AbsoluteVerticalVelocityBelowWhichDoubleJumpHappens`), the
`PLAYER_HIT_GROUND` message, and MP movement (`MultiPlayerAction` has its own
tweak block with air acceleration).
