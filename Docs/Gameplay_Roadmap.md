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
