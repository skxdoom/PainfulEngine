# Stubs — what is still hollow, and the order to fill it

A census of the instrumented stubs, ranked by what each one costs the game.
This is the work queue: [`Plan.md`](Plan.md) holds the strategy and the open
questions, [`Status.md`](Status.md) what works. A family that lands is deleted
from here — its rules live in [`Reference/`](Reference).

## The headline

Census of 2026-09-19. Module natives only; the 50 globals in the surface are
not counted.

| | |
|---|---|
| Module natives in the generated surface (`NativeList.inc`) | 791 |
| Implemented | 474 |
| Stubs | 317 |
| Natives the shipped scripts actually reference | 695 |
| ...implemented | 458 |
| ...still stubs | 237 |
| ...stubs after removing `NET` / `MPSTATS` / `GAMESPY` / `PMENU` | **154** |
| ...of those, reachable from live (uncommented) script code | **140** |

154 is the real remaining surface, and almost all of what a combat run still
hits is presentation: **40 distinct / 1,648 calls** in 900 frames (the first
census read 72 / 9,977), 37 / 1,569 idle.

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

## Tier 1 — gameplay leftovers

Every shared-path family (pinning, death zones, glass, mesh groups, collision
plumbing, the movers, throws, ragdoll joint moves, boss explosions) is in. What
is left is small and scoped:

| Native(s) | Live sites | What is missing |
|---|---|---|
| `WORLD.RemoveEntity` (10), `DeleteDyingEntities`, `DeleteDelayedEntities`, `AdvanceFrameCounter` | 13 | the engine-side lifetime sweep |
| `ENTITY.PO_SetPlayerShocked` | 8 | the electrodriver stun. Not `PO_SetPlayerFlying`'s timer ([`PlayerMovement.md`](Reference/PlayerMovement.md), "Being thrown"); stubbed until what it clears is known |
| `PHYSICS.SetBunnyHopAcceleration` (7), `PHYSICS.SetGravity` (3) | 10 | movement knobs the scripts read out of `Tweak` |
| `MDL.SetRagdollHardDeactivator` (4), `SetRagdollBreakablesThreshold` (2), `CopyMatrixFromJointToJoint`, `ENTITY.RecreateRagdollIfNone` | 8 | the rest of the ragdoll API |
| `PLAYER.AttachToUnderBody` / `DetachFromUnderBody` | 2 | the handcar's explicit attach; riding already works through the floor body |
| `PARTICLE.Restart`, `SetImmortal` | 4 | effect lifetime |
| `WORLD.MakeUnderwater` | 1 | the underwater world |
| `WPT.FastPickCurrentSet`, `EnableDisableSet` | 2 | waypoint sets |
| `ENTITY.PO_SetMissile` | 10 sites, 26 calls | **check first**: [`Physics.md`](Reference/Physics.md) records it as netcode-only |

Not a stub but the same queue: **what a corpse's collision group means** — the
Havok group-pair filter — is unrecovered, so only `Noncolliding` (7) changes the
layer ([`Physics.md`](Reference/Physics.md), "A corpse's collision group",
including how to test a reading properly).

## Tier 2 — feel and signature effects

| Native(s) | Live sites | What is missing |
|---|---|---|
| `ENTITY.AttachTrailToBones` | 26 | weapon and monster motion trails (hit at runtime) |
| `CAM.SetRotationDisplacement` | 8, 61 calls a run | camera kick on hits |
| `CAM.LookAt` (6), `SetAdditionalRotation`, `EnableInterpolation` | 10 | scripted camera moves |
| `INP.ResetTimer` (4), `GetTimeDelta` | 5 | script-side timers |
| `ENTITY.TransformLocalPointToWorld` (7), `GetCenter` (3), `ComputeLocalPoint` | 11 | small geometry queries whose returns are consumed |
| `WORLD.EnableGhostsFX` (5), `FlashSkyTexture` (4), `MDL.SetBlendAlpha` (5) | 14 | level set pieces |

## The stubs that RAISE rather than fail quietly

Most stubs fail silently, but a stub whose return feeds arithmetic or a table
index raises instead — and the error unwinds out of whatever tick called it,
taking the rest of that frame's work with it. These are consumed at **every**
live call site, so any one of them that is reached is a hard stop, not a
degradation:

`R3D.VectorToScreen` (23),
`ENTITY.TransformLocalPointToWorld` (7), `R3D.DistToCamera` (3),
`ENTITY.GetCenter` (3), `MESH.GetRandomPoint` (2), `INP.GetTimeDelta`,
`ENTITY.GetFileName` (2), `ENTITY.Exist` (2), `PHYSICS.GetHavokBodyActiveGroup`
(2), `WORLD.GetEntityList`, `WORLD.GetAmbientColor`, `PLAYER.GetPitch`.

When a nil-arithmetic error turns up in play, look here first. It is also the
cheapest possible fix list — most of them are one query each.

## Tier 3 — presentation (most of the call volume, none of the behaviour)

Ranked by calls per 900 combat frames:

- **Acoustics** — `WORLD.FindEnvironmentAtPoint` (450), `SOUND.PreloadFile`
  (214), `SOUND3D.SetObstructed` / `SetIntensity` (135 each),
  `SOUND.SetRoomType` (31), and the streams' low-pass
  (`SOUND.GlobalSetLowPass`).
- **Model and mesh materials** — `MESH.SetDetailMap` (239), `MATERIAL.Replace`
  (44 sites), `MDL.SetTexture` (32 sites).
- **The environment setters** — `ENVIRONMENT.SetAmbient` / `SetDirLight` /
  `SetFog` / `SetWater` / `ResetReflectList` (50 each), `WORLD.SetDirLight`,
  `ENTITY.AddLight`. The boxes themselves already light and fog
  ([`Lighting.md`](Reference/Lighting.md), "Environment boxes"); these are the
  script-side writes to them.
- **Visibility switching** — `WORLD.UseSwitchZones`, `EnablePortal`, the
  antiportal family, `EnableOcclude`. Correctness of what is drawn, and the
  frame cost of drawing it.
- **`WORLD.SetRenderTarget`** (61).
- **Glass shards.** A pane breaks and vanishes correctly; the original
  fractures it into pieces and this does not. Generated geometry has no draw
  path yet, and a savegame does not remember what is broken
  ([`Physics.md`](Reference/Physics.md), "Glass").
- **Debug and HUD draw** — `R3D.RenderLine` (34), `RenderBox` (29),
  `VectorToScreen` (23), `DrawSphere` (14). `VectorToScreen`'s return is
  consumed at 22 of its 23 sites, so those raise rather than no-op if reached.

## Tier 4 — deprioritized

- **Multiplayer**: `NET` 38, `MPSTATS` 15, `GAMESPY` 4 = 57 stubs.
- **Menus**: `PMENU`, 47 stubs. [`Menu.md`](Reference/Menu.md) covers what
  already works and what is left.
- **Demo recorder**: `CONSOLE.Demo*`.
- **Editor-only**: `MOUSE.LB` / `RB` (43 sites, all Editor), the `WPT`
  selection family, `ENTITY.Pick`, `ENTITY.RenderBBox`, `R3D.Draw*Light`.

## How these numbers were produced

- Surface: `PK_NATIVE` rows in `Source/Script/NativeList.inc`.
- Implemented: `{"MODULE", "Name", fn}` rows across `Source/Game/Script*.cpp`
  and `Source/Script/Natives.cpp`. A native registered in some other shape would
  be misfiled as a stub; spot-checked against the runtime report, which agrees.
- Static sites: one pass over `LScripts`, `Items` and `Levels` matching
  `MODULE.Fn`, with everything after a `--` on the line dropped.
- Runtime: the two `PainfulTools lua` runs above.
