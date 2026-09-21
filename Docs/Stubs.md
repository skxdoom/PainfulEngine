# Stubs — what is still hollow, and the order to fill it

A census of the instrumented stubs, ranked by what each one costs the game.
This is the work queue: [`Plan.md`](Plan.md) holds the strategy and the open
questions, [`Status.md`](Status.md) what works. A family that lands is deleted
from here — its rules live in [`Reference/`](Reference).

## The headline

Census of 2026-09-21. Module natives only; the 50 globals in the surface are
not counted.

| | |
|---|---|
| Module natives in the generated surface (`NativeList.inc`) | 791 |
| Implemented | 498 |
| Stubs | 293 |
| Natives the shipped scripts reference from live code | 674 |
| ...implemented | 479 |
| ...still stubs | 195 |
| ...stubs after removing `NET` / `MPSTATS` / `GAMESPY` / `PMENU` | **119** |

A combat run now hits **37 distinct / 1,670 calls** in 900 frames (the first
census read 72 / 9,977), and almost all of what is left is presentation.

**Of those 119, a third are not game code at all.** Sorting the remainder by
where it is *called from* rather than by how many call sites it has moved a
lot of weight out of the queue:

- **Editor only** — every site in a `.editor` class or `Editor/*.lua`:
  `R3D.VectorToScreen` (23), `R3D.RenderLine` (34), `RenderBox` (29),
  `DrawSphere` (14), `DistToCamera` (3), `DrawDirLight` (6), `DistToLine2D` (4),
  `MOUSE.LB` / `RB` / `MB` (45), the `WPT` selection family including
  `FastPickCurrentSet` and `EnableDisableSet`, `WORLD.GetEntityList`,
  `ENTITY.Pick`, `ENTITY.RenderBBox`, `MDL.DrawJointNames`.
- **Multiplayer only** — every site in `GameMP.lua` or behind a
  `GMode ~= SingleGame` branch: `PHYSICS.SetGravity` (3),
  `PHYSICS.SetBunnyHopAcceleration` (7), `PLAYER.ExecMultiPlayerAction`,
  `PLAYER.FloorCheckMP`, `PLAYER.TestMovement`, `PLAYER.RecordMovement`,
  `ENTITY.PO_HideFromPrediction`.

Both groups are Tier 4 below. `R3D.VectorToScreen` in particular used to head
the "raises rather than fails quietly" list on 23 consumed sites; not one of
them is reachable from a game.

## Strategy: implement families, do not hunt bugs

For anything the stub report names, implementing beats bug-hunting:

- **Stubs fail silently.** A stub returns no values, so every `Is*`/`Get*`/
  `Check*` reads as `false`/`nil` and the script takes the "no" branch without
  complaining. A bug hunt therefore costs a full reproduce-and-bisect to arrive
  at a name the report already prints for free.
- **Bugs cluster.** Fixing a family closes many symptoms at once; fixing
  symptoms closes one each.
- **The instrument already exists** (below) and runs in a minute with no repro.

Keep a bug-hunting loop only for the other half of the problem: the
implemented natives. Those do not fail by absence, they fail by *convention* —
a rotation composed in the wrong order, a scale, a sign, a unit — and no counter
shows that. See [`Vectors.md`](Reference/Vectors.md) for the ones already gotten
wrong twice. Play-testing is the only instrument for that class.

## The instrument

Idle — what the world does with nobody playing:

```
PainfulTools lua D:/Dev/PKRE/Data 400 C1L1_Cathedral
```

Combat — the same harness with the player firing. `_waitingForFirstFire`
(`CPlayer.lua:515`) needs one frame with `Fire` clear before it will ever shoot,
so the override must PULSE the bit, not hold it. As one line:

```
PainfulTools lua D:/Dev/PKRE/Data 900 C1L1_Cathedral "for k,v in Player.Ammo do Player.Ammo[k]=999 end for i=1,7 do Player:AddWeapon(i) end local f=0 INP.GetActionStatus=function(e) f=f+1 local a=0 if math.mod(f,20)<10 then a=a+Actions.Fire end if math.mod(f,240)<2 then a=a+Actions.NextWeapon end if math.mod(f,20)>=15 then a=a+Actions.AltFire end return a end"
```

Combat is the run that matters — it is the only one in which weapons, ragdolls,
pinning and glass are exercised at all. It is not bit-identical between runs
(the scripts use `math.random`, so a fight ends slightly differently and the
rarer natives come and go); the volumes are stable. Static counts below are
**live** call sites, comment lines excluded, and a native whose every site is
commented out is not listed at all: `INP.GetTimeFromTimerReset` looks like 8
sites and is none, `MDL.SetRagdollRestitution` likewise.

**The static census does not see every native.** `WORLD.GetFileName` and
`R3D.ClearScreen` are called by the scripts and are not in `NativeList.inc` at
all, so they never appear as stubs in the table above — only in the runtime
report, through the module tables' auto-stub `__index`. Trust the runtime
report over the static one when they disagree.

## Tier 1 — gameplay leftovers

| Native(s) | Live sites | What is missing |
|---|---|---|
| `ENTITY.PO_SetPlayerShocked` | 8 | the electrodriver stun. `PhysicsObject::SetPlayerShocked` (`0x1019 67D0`) writes **a float to the player controller at +0x3C** and nothing else, so it is a timer. Two things are still unknown: the duration (the native passes a literal the decompiler dropped — disassemble `0x101367B0` for the `fld`) and what reads `+0x3C` |
| `MDL.SetRagdollHardDeactivator` | 4 | `Ragdoll::SetHardDeactivator` (`0x1019CBE0`), no arguments: the corpse settles and stops. Hangman and the Wozek want hanging bodies that do not twitch. Needs sleep control on the ragdoll's Jolt bodies, which `PhysicsWorld` does not expose yet |
| `MDL.SetRagdollBreakablesThreshold` | 2 | `Ragdoll::SetBreakablesThreshold` (`0x1019CB90`), default `1e7`; C5L3's cross passes 4000. Needs the breakables system — `Hke` already parses `IS_BREAKABLE` and its `strength` per joint and the port ignores both |
| `MDL.CopyMatrixFromJointToJoint` | 1 | `PBindJointToJoint` |
| `ENTITY.PO_SetMissile` | 10 sites, 23 calls | **check first**: [`Physics.md`](Reference/Physics.md) records it as netcode-only |
| `ENTITY.Tick`, `EnableTick`, `SetAmbient`, `AddLight`, `ResetLights` | 8 | the menu's character preview, which ticks and lights one model by hand outside any world |
| `ENTITY.PO_GetType`, `PO_IsOnLadder`, `PO_SetEntitySteered`, `PO_SetHardDeactivator`, `EnableCollisionsToMesh`, `UpdatePart`, `UnregisterChild` | 9 | the tail of the body API, one site each |
| `WORLD.GetFileName` | 2 | the loaded map's name, which `CLevel:Apply` compares against `self.Map` to skip a reload. Not in the generated surface. **Open first:** implementing it takes `WORLD.Release(false)` on a level restart, and whether `World::ReleaseWithoutMap` (`0x1005DC80`) also resets broken glass and spent destructibles is unrecovered — if it does not, a restart would inherit the last run's damage |

Not a stub but the same queue: **what a corpse's collision group means** — the
Havok group-pair filter — is unrecovered, so only `Noncolliding` (7) changes the
layer ([`Physics.md`](Reference/Physics.md), "A corpse's collision group",
including how to test a reading properly).

## Tier 2 — feel and signature effects

| Native(s) | Live sites | What is missing |
|---|---|---|
| `ENTITY.AttachTrailToBones` | 26 | motion trails: every dart, nail, shuriken and grenade, plus the monsters' weapons. The biggest single item left, and scoped — see below |
| `CAM.LookAt` (6), `SetAdditionalRotation` (2), `EnableInterpolation` (2), `UpdateViewport` (2), `SetTransform`, `SetAngRad` | 14 | scripted camera moves |
| `WORLD.EnableGhostsFX` (5), `FlashSkyTexture` (4), `MDL.SetBlendAlpha` (5) | 14 | level set pieces: C5L4's ghosts, C3L5 and C6L4's lightning, the Stone Golem and Thor's hammer fading in |
| `INP.RemoveAction` | 5 | consuming a game action so it does not repeat while the key is held — the UI twin is in |
| `MDL.SetHeadTrackRot`, `SetWaterImpact`, `UpdateWaterImpact`, `GetAnim` | 4 | small model queries |

### The trail system, scoped

The definitions are trivial — 18 files in `Scripts/Trails/`, four or five keys:

```
[General]
Texture = trailkolek.tga
BlendMode = alpha
LifeTime = 0.5
Width = 0.15
ConstLen = 5      -- only trail_ghost
```

The script side is always the same three calls (`BindTrailToEntity`,
`CActor`'s `Trails` table, and each projectile's `OnCreateEntity`):

```lua
local e = ENTITY.Create(ETypes.Trail, "trail_kolek", "trailName")
ENTITY.AttachTrailToBones(owner, e [, joint names...])
WORLD.AddEntity(e)
```

`Trail::AttachToBones` (`0x101EA930`) is recovered: it registers the trail as a
child of the owner (joint −1, dies with the parent), then `Init(2000, n)` where
`n` is the joint count, floored at 2 — and **0 for a non-model owner**, which is
the common case, every projectile being a mesh. So a bladed monster gets a
ribbon strung between two named bones and a dart gets one built from its own
transform.

**What is not recovered:** the per-frame update — how the rails are sampled,
what the two rails are when there are no joints, and how `LifeTime` fades the
tail. `Trail::Tick` is not in `PainfulEngineHelpers/Decompiled`, so this needs a
Ghidra pass before any of it is written.

## Tier 3 — presentation (most of the call volume, none of the behaviour)

Ranked by calls per 900 combat frames:

- **Acoustics** — `WORLD.FindEnvironmentAtPoint` (450), `SOUND.PreloadFile`
  (214), `SOUND3D.SetObstructed` / `SetIntensity` (136 each),
  `SOUND.SetRoomType` (31), and the streams' low-pass
  (`SOUND.GlobalSetLowPass`). The largest block of call volume left by a wide
  margin.
- **Model and mesh materials** — `MESH.SetDetailMap` (210), `MATERIAL.Replace`
  (44 sites), `MDL.SetTexture` (32 sites), and the `MESH.Get*` readers.
- **The environment setters** — `ENVIRONMENT.SetAmbient` / `SetDirLight` /
  `SetFog` / `SetWater` / `ResetReflectList` (50 each), `WORLD.SetDirLight`.
  The boxes themselves already light and fog
  ([`Lighting.md`](Reference/Lighting.md), "Environment boxes"); these are the
  script-side writes to them.
- **`WORLD.SetRenderTarget`** (176 in this run) — the full-screen frame blend
  `TPlayerHit` runs on every hit, which is why the count follows the fight.
- **Visibility switching** — `WORLD.UseSwitchZones`, `EnablePortal`, the
  antiportal family, `EnableOcclude`. Correctness of what is drawn, and the
  frame cost of drawing it.
- **`MESH.SetDefaultNormalMaps`** — despite the name this is the level-wide
  **water** normal map (every level passes `ripples_00` or `waves2`), so it
  belongs with [`Water.md`](Reference/Water.md), not with bump mapping.
- **Glass shards.** A pane breaks and vanishes correctly; the original
  fractures it into pieces and this does not. Generated geometry has no draw
  path yet, and a savegame does not remember what is broken
  ([`Physics.md`](Reference/Physics.md), "Glass").

## Tier 4 — deprioritized

- **Multiplayer**: `NET` 38, `MPSTATS` 15, `GAMESPY` 4, plus the seven
  MP-only natives named in the headline.
- **Menus**: `PMENU`. [`Menu.md`](Reference/Menu.md) covers what already works
  and what is left.
- **Demo recorder**: `CONSOLE.Demo*`.
- **Editor-only**: the whole list in the headline. `R3D.Draw*Light`, the `WPT`
  selection family and the debug-draw natives are all reached from `.editor`
  classes and `Editor/*.lua` and from nothing else.

## How these numbers were produced

- Surface: `PK_NATIVE` rows in `Source/Script/NativeList.inc`.
- Implemented: `{"MODULE", "Name", fn}` rows across `Source/Game/Script*.cpp`
  and `Source/Script/Natives.cpp`. A native registered in some other shape would
  be misfiled as a stub; spot-checked against the runtime report, which agrees
  except for the two names missing from the surface entirely (above).
- Static sites: one pass over `LScripts`, `Items` and `Levels`, every file,
  with everything after a `--` on the line dropped, matching `MODULE.Fn(`.
- Where a native is called *from* — the editor and multiplayer splits — is by
  the file the site is in: `.editor` and `Editor/*.lua` for the first,
  `GameMP.lua` and `GMode ~= SingleGame` branches for the second.
- Runtime: the two `PainfulTools lua` runs above.
