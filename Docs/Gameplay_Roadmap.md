# Gameplay roadmap

Where the port stands on *playing the game*, what the scripts are still asking
for, and the order to close the gap. Measured, not guessed: every number below
comes from the host's own call report.

## The measurement

```
PainfulEngine lua D:/Dev/PKRE/Data 400 C1L1_Cathedral
```

boots the shipped scripts, loads Cathedral, runs `Game:OnPlay` and ticks 400
frames, then prints every native the scripts called that we have not written.
Current reading, after Stages 7 and 8:

```
boot: 962 files loaded, 0 missing, 0 script errors
entities: 678 created, 108 released, 618 live
unimplemented natives hit: 134 distinct, 50095 calls
```

against a starting point of 153 distinct / 82,843 calls and 634 entities.
Zero script errors throughout: the game's own logic runs end to end, and what
it could not do was *act*, because the natives that carry action were stubs.
Re-run this after every stage; the report is the progress bar.

## The gap as it stood at the start, ranked by what the scripts asked for

| calls | native | system |
|------:|--------|--------|
| 15238 | `ENTITY.PO_Move` | monster locomotion |
| 11192 | `HUD.DrawQuad` | HUD |
|  8000 | `ENTITY.RemoveRagdollFromIntersectionSolver` | hit detection |
|  7600 | `ENTITY.AddRagdollToIntersectionSolver` | hit detection |
|  7186 | `ENTITY.PO_IsActionState` | player + actor actions |
|  4076 | `PMENU.LoadingProgress` | menus |
|  2800 | `INP.Action` | input |
|  2521 | `HUD.DrawQuadRGBA` | HUD |
|  2400 | `MDL.ApplyJointRotation` | animation |
|  2398 | `INP.UIAction` | input |
|  1271 | `ENTITY.SeesEntity` | AI |
|  1200 | `ENTITY.PO_IsOnFloor` | AI |
|   402 | `CAM.SetPos` / `CAM.SetAng` | camera |
|   400 | `PLAYER.ExecAction` / `PLAYER.FloorCheck` | player |
|   400 | `SOUND.SetPlayerPos` / `SetPlayerOrientation` | audio |
|   502 | `LIGHT.Setup` / `LIGHT.SetFalloff` | dynamic lights |

Plus the whole `SOUND` / `SOUND2D` / `SOUND3D` family, `WORLD.LineTrace` and
`LineTraceFixedGeom` (not in the report only because nothing fires yet), and
`MESH.SetNormalMap` / `SetDetailMap` / `SetCubeMap`.

## Two findings that set the order

**1. The player's controls are a script path, not a C++ path.** The original
divides the work exactly at `CPlayer:Tick`:

```lua
local action = INP.GetActionStatus(self._Entity)   -- bitmask of Actions.*
... script overrides for weapon-select, switched fire, rocket jump ...
ENTITY.PO_SetAction(self._Entity, self.CurAction)
PLAYER.ExecAction(self._Entity, 0, fv.X,fv.Y,fv.Z, rv.X,rv.Y,rv.Z)
```

`PLAYER.ExecAction` *is* the entry to `PhysicsObject::PlayerAction`
(0x10192260) already recovered in [`PlayerMovement.md`](PlayerMovement.md).
`PlayerPawn` used to bypass this and derive its own wish-direction from the
C++ camera, which works for walking around and can never produce firing,
weapon switching, rocket jumps or the bunny-hop windows, because all of those
live in the bits the script never gets to set. Closed in Stage 7.

Supporting facts recovered from the scripts:

- `Actions` (`Main/Definitions.lua:190`) is a 32-bit mask:
  `Forward=2, Backward=4, Left=8, Right=16, Jump=32, Fire=64, AltFire=128,
  NextWeapon=256, PrevWeapon=512, Weapon1..14 = 1024<<n,
  FireBestWeapon1=0x1000000, FireBestWeapon2=0x2000000, RocketJump=0x4000000,
  ForwardRocketJump=0x8000000, UseCards=0x10000000, ComboFire=0x20000000,
  SelectBestWeapon1=0x40000000, SelectBestWeapon2=0x80000000`.
- `Keys` (`Definitions.lua:9`) are **Windows virtual-key codes** verbatim
  (`Space=32`, `A=65`, `MouseButtonLeft=1`), so `INP.Key` is a direct map off
  SDL.
- `INP.Key(k)` is tri-state, not boolean: call sites pair `==1` for the press
  edge with `==2` for held (`INP.Key(Keys.G)==1 and INP.Key(Keys.RightShift)==2`,
  `Game.lua:568`).
- The movement basis is derived in **pure Lua** from `CAM.GetAngRad()`:
  `CPlayer:SetupAction` (`CPlayer.lua:1672`) packs the angles to 16 bits,
  unpacks them, and builds forward/right through `Quaternion:New_FromEuler`.
  So `CAM.GetAngRad` must return the engine's own convention or the player
  walks sideways. Free check: the script's derived forward vector must equal
  what our C++ `CAM.GetForwardVector` returns from the same camera.
- `CAM.GetRightVector` is **not implemented** and is reached only under
  `Game._loonyProc`; the ordinary path goes through `SetupAction` above.

**2. Animation is a gameplay dependency, not polish.** `CActor:Tick`
(`CActor.lua:300`) opens its animation-event loop with

```lua
local animSpeed = MDL.GetAnimTimeScale(self._Entity, self._CurAnimIndex)
if animSpeed > 0 then ... while self._AnimationEvents[i] do ... end
```

We return `0`, so the loop never runs. Those events are how melee damage
lands, how footsteps and attack sounds fire, and how actors sequence their
state machine against `_CurAnimTime` / `_CurAnimLength`. Monsters can spawn
and chase and still never hurt anything until the animation clock is real.

## The stages

### Stage 7 - the player acts — DONE

Turned the pawn from a camera-driven debug body into the game's own control
path, all six steps. The report is down to **139 distinct natives / 66,894
calls** from 153 / 82,843, the movement measures against `Tweak.PlayerMove`
(7.9999 m/s against `PlayerSpeed` 8.0), and the scripts now steer the view
through `Game:Tick2`. Air control, pawn height, the step ladder and what the
player collides with were all corrected against the binary along the way —
[`PlayerMovement.md`](PlayerMovement.md) and [`LuaHost.md`](LuaHost.md) carry
the recovered rules.

1. **Input state** - an `Input` service fed from SDL, exposing virtual-key
   state with press-edge tracking. Natives: `INP.Key` (tri-state),
   `INP.GetTime`, `INP.Reset`, `INP.ResetTimer`.
2. **Bindings** - `INP.LoadBindings()` takes no arguments; the original reads
   them engine-side. Ship Painkiller's defaults in C++ (WASD, Space, LMB
   fire, RMB alt-fire, wheel and 1..7 for weapons, E use) until the menu can
   rebind them.
3. **Action mask** - `INP.GetActionStatus(e)` folds the bindings into the
   `Actions` bitmask; `INP.Action(mask)` and `INP.UIAction(mask)` answer the
   global queries; `INP.IsFireSwitched` from config.
4. **Action state store** - `ENTITY.PO_SetAction` / `PO_AddAction` /
   `PO_IsActionState` / `PO_JumpedInLastAction` against a per-entity mask.
5. **`PLAYER.ExecAction(e, 0, fwd, right)`** drives `PlayerPawn` from the
   action bits and the passed basis instead of the camera, per
   [`PlayerMovement.md`](PlayerMovement.md). `PLAYER.FloorCheck` reports the
   ground test.
6. **Camera handover** - `CAM.SetPos` / `SetAng` / `SetPositionDisplacement` /
   `EnableInterpolation`, `MOUSE.GetDelta` fed with real motion and
   `MOUSE.SetSensitivity`, so `Game:Tick2` steers the view as the original
   does. Verify against the free check above before trusting it.

### Stage 8 - weapons fire — DONE

Traces, the intersection solver, the view model and the hit reaction have
landed (report down to **134 distinct / 50,095 calls** from 139 / 66,894).

**Damage needed no work at all, which the roadmap got wrong.** It is entirely
script-side: a weapon traces, takes the entity the trace reports, looks it up
with `EntityToObject[e]` and calls `obj:OnDamage(...)`. Once the trace
resolved to the right entity handle, the whole chain was already live.
Measured on Cathedral: aim a shotgun at a spawned `EvilMonkV2` with `Health
= 9`, fire, and it goes to `Health = 0`, `_died = true`. Worth remembering as
a general lesson here - before building a system, check whether the scripts
already are it.

What was genuinely missing was the REACTION: a shot landed and nothing moved.
`ENTITY.PO_Hit` and `WORLD.HitPhysicObject` are an impulse at the point of
impact, applied through a new `PhysicsWorld::AddScriptBodyImpulse`. The one
real bug was ordering - a body has to be WOKEN before the impulse, because
Jolt drops an impulse applied to a sleeping body and props are asleep the
moment a level finishes loading, so every shot at a standing barrel did
nothing.

Verified three ways: a single pellet-sized impulse (188 against the barrel's
declared mass of 200) nudges it about 0.05 and friction settles it; the same
total in one call throws it five units; and every pellet of a shotgun volley
resolves to the right body with the right magnitude. One thing NOT explained:
fifteen pellet impulses spread over a second move the barrel markedly less
than the same total delivered at once. That is plausible contact-and-friction
behaviour on a heavy resting body rather than a demonstrated defect, and no
defect could be found - recorded here rather than quietly assumed away.

Still stubs, deliberately: `ENTITY.PO_AccumulateRotation` (the knockback spin
- "accumulate" suggests it buffers for the ragdoll, and guessing at that is
how earlier convention bugs happened) and `ENTITY.PO_SetPlayerShocked`.


### Stage 9 - animation and the actor clock

`MDL.LoadAnim` / `SetAnim` returning a real index, `GetAnimTime` /
`GetAnimLength` / `GetAnimTimeScale` / `SetAnimTimeScale` / `ResetFrame`,
`MDL.ApplyJointRotation` and `TransformPointByJoint`, and skinning in
`EntityRenderer` (the maths already exists in `Assets/Skeleton`; the renderer
has no skinned path and no `vs_model` shader yet). Unblocks the animation
event loop, and with it melee damage, footsteps and every actor state that
waits on a track to finish. Also ends the bind-pose look.

### Stage 10 - monsters move

`ENTITY.PO_Move` (15 238 calls, currently always `(e,0,0,0)` because nothing
upstream has a destination), `PO_IsOnFloor`, `SeesEntity`, `WPT.Load` for the
waypoint graph, and the `PO_SetMonsterType` / `MovementConst` / `SightParams`
chain that already fires on spawn.

**Started.** Two pieces are in:

- **Root motion.** `GetAnimMovement` is real, read out of Engine.dll rather
  than inferred; see Docs/Animation.md. This is how an attack carries a monster
  forward, and it works without any AI at all.
- **`VectorRotate`.** A bare global that was returning nothing, so `mvx` came
  back nil and the arithmetic in `CActor:Update` aborted the tick. `CAiBrain`,
  `farattack` and `jumpUp` all build their movement directions with it, so
  nothing in the AI movement path could have worked until it existed.

The two agree on which way an actor faces, from independent sources: `CActor`
builds facing as `VectorRotate(1,0,0, 0, -angle + pi/2, 0)`, which at angle 0
is **+Z** - the same axis the walk animation's own root travel runs along.
That is the `+pi/2` engine-turn offset the camera work already established,
turning up again in the AI.

Still to do: `PO_IsOnFloor`, `SeesEntity` (a line trace - `PhysicsWorld::RayCast`
already exists), `WPT.Load`, and `PO_Move` itself.

### Stage 11 - HUD

`HUD.DrawQuad` / `DrawQuadRGBA` / `DrawQuadRotated`, `GetTransparency` /
`SetTransparency`, and a real `MATERIAL.Create` / `Replace` / `Size`. Note the
report shows `HUD.DrawQuad(nil, ...)` - the material handle is nil, so the
2D material pipeline is absent entirely and the current 256x256 `MATERIAL.Size`
stub is holding up a wall that is not there.

### Stage 12 - sound

The `SOUND` / `SOUND2D` / `SOUND3D` families, ~2 000 calls a run. Deliberately
after animation, because most of the 3D triggers are animation events.

Menus (`PMENU`), dynamic lights (`LIGHT.Setup`), material extras
(`MESH.SetNormalMap` / `SetDetailMap` / `SetCubeMap`) and save/load sit
outside this line and can be picked up whenever they block something.

## Pre-existing faults found by sweeping every level

Testing four levels hid these. Running all 56 headless for 200 frames each is
cheap and should be the standard check before calling a stage done.

**Fixed here.** Thirteen levels threw 188 errors each and Alastor 198 - one
per tick, every tick, aborting `Game_Tick` and with it the whole object update:

- `CItem.lua:965`, `attempt to compare number with nil` - `GetVelocitiesFromJoint`
  returned nothing, and an object with a `RagdollCreakSound` compares its
  fourth return against a threshold every pass. The guard above it only prints
  when the joint is missing; it does not stop the timer. Now returns eight
  zeros, which is the true velocity of a joint no ragdoll is driving.
- `CActor.lua:1086`, `arithmetic on local 'mvx' (a nil value)` - `VectorRotate`,
  as above.

**Still open.**

- **Three levels crash outright** at the same point every time, immediately
  after `R3D.KeepDecals(false)` during level start, before the tick loop:
  `C3L3_Military_Base`, `C6L1_Orphanage`, `C6L4_City`. Process exit 127, no
  Lua error, so this is native. In a working level the next calls are
  `WORLD.EnablePortal(...)`. Not investigated - its own task.
- **`C3L5_Ruins` and `C6L10_Shadowland`** each throw one error at load, in
  `Thor.lua:78`, `attempt to concatenate local 'count'`. Once per level rather
  than per tick, so it costs one monster rather than the update.

## Monsters are moved, not simulated

Reported from play: monks stood walking inside a wall, and the player could
bowl them across the level like barrels. One cause behind both - an actor was
an ordinary **dynamic rigid body**.

Engine.dll says it should not be. `PO_SetMonsterType` (0x101313C0) sets one
flag bit at `PhysicsObject+0x74` and nothing else, and `PO_Move` (0x10130D50)
**moves nothing at all**: it writes three floats to `PhysicsObject+0x34..0x3c`
and returns. It is a setter, like `PO_SetAction`, and the physics step spends
what it stored. A monster is a body the engine *carries*, not one it
simulates.

So the body becomes kinematic the moment the monster flag arrives (it cannot
be done at `PO_Create`, which is called before the flag), and `TickMonsters`
walks it with the same swept sphere the player moves with. Nothing can push
it, it cannot tumble, and it is stopped by the geometry that stops the player.
`PO_Move`'s vector is a VELOCITY - `CActor` passes `mv * (1/delta)`.

Recovered layout, worth keeping:

| offset | field |
|---|---|
| +0x34..0x3c | `PO_Move`'s wish vector |
| +0x68, +0x71 | floor normal / on-floor, what `PO_IsOnFloor` returns |
| +0x6c, +0x70 | `PO_SetMonsterMovementConst`'s two arguments (0.5, false) |
| +0x74 | flags; bit 2 = monster |
| +0x78 | the action mask |

`PO_Create` also writes 22.0, pi/2, 6.0, pi/2 to +0x24..+0x30 immediately
after creating the object - the SIGHT parameters, seeded before any script
sets them; see below.

### The shape was the actual bug

`CreateScriptBody` sizes a sphere by the **largest** half-extent, which is
right for a barrel and wrong for a character: `evilmonkv2` is 14.4 model units
across the ARMS against a body 2.9 deep, so a monk was a sphere wider than it
was tall and could not approach a wall. Monsters now take the smaller
horizontal half-extent - 0.35 world units for a monk.

And a `.pkmdl`'s origin is the **middle of the model, not the ground under
it**: `evilmonkv2` measures `y[-12.80..10.11]`, so its feet are 12.8 units
below the position the scripts set. Assuming a foot origin and lifting the
sphere by a radius made monks climb out of the world at exactly one radius per
tick, which is how the mistake was caught - the offset now comes from the
model's own bounds. Measured after: a monk holds y = -2.92 for 900 frames,
on the floor, not drifting.

The engine's own rule for `BodyTypes.Fatter` lives inside
`Entity::CreatePhysicsObject` and has NOT been recovered; the horizontal
half-extent is a shape argument, not the original's constant.

### Sight, and the mover proved

`SeesEntity` (0x101335E0) hands off to `CalculatePawnToEntityVisibility` when
the looker has a physics object and otherwise line-traces between the two
entity POSITIONS. Worth copying: it turns the looker's own ragdoll off for the
duration of the trace and back on after - a monster's own body sits on the
line and would blind it.

`PO_SetSightParams` (0x10131210) writes the four floats at +0x24..+0x30, which
is the same block `PO_Create` seeds - so those are SIGHT parameters, not the
movement limits guessed at above. The templates name them, and the names give
the model away: `viewDistance360` is how far the actor sees in EVERY
direction, `viewDistance` how far inside its cone. Shipped values run
`viewAngle = 170, viewDistance360 = 6`: aware of anything within six units,
and beyond that only what is in front. The angles arrive as a full spread in
degrees and are stored as a half-angle in radians, which is what makes the
engine's 180 default come out as the pi/2 PO_Create writes.

**The mover is verified.** Driven at 4 units/s, a monk walks 5.97 units per 90
ticks - 4 x 1.5 s = 6.00, the commanded speed exactly - holds y = -2.92 the
whole way, and is stopped dead by a wall in the other direction.

One trap on the way: a monster sweeps its own shape through a world its own
body is standing in, so it was wedged inside itself and could not move at all.
`SlideSphere` and `Depenetrate` now take a body slot to pass through.

Monsters still will not come at you in a HEADLESS run, and that is correct
rather than broken: nothing walks toward a player it cannot see, and at the
spawn the nearest monk is 68 units away against a sight range of 10.

### The orientation sign

Reported from play: monsters following the player faced the wrong way, but
close. That is what a NEGATED yaw looks like - right at 0 and 180 degrees,
backwards at 90.

`SetOrientation` built the quaternion for +A when the scripts mean -A, and the
shipped code says so in two independent places. `BindPoint` (Utils.lua) rotates
an offset by `-ENTITY.GetOrientation(e)`, and `CActor:MoveWithAnimation`
rotates the animation's own motion by `cos(-angle)/sin(-angle)`. Both reduce to
the same transform:

    world = ( cos A * mx + sin A * mz,  my,  -sin A * mx + cos A * mz )

which sends the model's forward - +Z, the axis the walk animations travel
along - to (sin A, 0, cos A). Measured after the fix: model +Z lands exactly on
the AI's intended facing at 0, 45, 90, 180 and -90 degrees, and Set/Get round
trips to float precision across [0, 2pi), which is the range CActor keeps its
angle in.

This was wrong for every script-driven orientation, not just monsters - a
weapon or effect bound through `BindPoint` was mirrored the same way.

### The same sign, found again in the viewmodel

Reported from play: the weapon models had holes - parts of the stakegun were
see-through, the dark receiver body missing against the floor.

It was the negated turn again, in the one path that had not been fixed.
`ENTITY.SetPosAndRotRelativeToCamera` - the viewmodel transform, and the only
caller is `CWeapon:Apply` - passed its Euler angles to `EngineEulerToQuat`
raw, where `SetOrientation` negates the turn. `StakeGunGL` asks for a yaw of
**-1.57**, so with the wrong sign the gun sat in exactly the right place while
presenting its far side. The gaps between its parts then read as holes punched
through a solid model.

Nothing was missing. Worth listing what had to be eliminated first, because
every one of these looked plausible and each was disproved by a measurement
rather than by eye:

| suspected | ruled out by |
|---|---|
| unused material slots | raw bytes: `u32 8, "Models/\0"` is a real empty placeholder in the file, and `materials[0]` is the valid diffuse |
| missing geometry | a build-time probe: `KGR: 16 parts, 3137 tris`, every mesh submitted |
| backface culling | `PAINFUL_ECULL=2`, pixel-identical |
| near-plane clipping | `PAINFUL_NEAR=0.01`, pixel-identical |
| the alpha test (`palskinned` tests at ref 128) | `PAINFUL_NOATEST`, **3 of 24300 pixels** differed |
| skinning collapse | `pose`: bind 57.99 against posed 58.09, all 77 bones driven |

The alpha test is the one worth dwelling on. Eyeballing two screenshots of it
said "identical" and so did the numbers - but only the numbers were worth
anything, because two of the earlier comparisons had *also* looked identical
by eye when the thing being tested was simply not the cause. A 3-pixel
difference is an answer; "looks the same to me" is not.

The lesson for the rest of the port: this convention has now cost three
separate bugs - monster facing, `BindPoint` offsets, and the viewmodel. Any
new path that turns engine Euler angles into a quaternion negates the turn.
