# Stubs — what is still hollow, and the order to fill it

A census of the instrumented stubs as of 2026-09-07, ranked by what each one
costs the game. Companion to [`Plan.md`](Plan.md) (stage-shaped, and partly
stale) — this file is stub-shaped and measured.

## The headline

| | |
|---|---|
| Names in the generated surface (`NativeList.inc`) | 840 |
| Implemented | 424 |
| Stubs | 416 |
| Natives the shipped scripts actually reference | 694 |
| ...implemented | 368 |
| ...still stubs | 326 |
| ...stubs after removing `NET` / `MPSTATS` / `GAMESPY` / `PMENU` | **235** |
| ...of those, reachable from live (uncommented) script code | **~225** |

235 is the real remaining surface. Roughly half of it is presentation.
(Census taken 2026-09-07; the counts moved by 7 when Tier 0 landed.)

## Strategy: implement families, do not hunt bugs

For anything the stub report names, implementing beats bug-hunting:

- **Stubs fail silently.** A stub returns no values, so every `Is*`/`Get*`/
  `Check*` reads as `false`/`nil` and the script takes the "no" branch without
  complaining. A bug hunt therefore costs a full reproduce-and-bisect to arrive
  at a name the report already prints for free.
- **Bugs cluster.** Ten families below account for essentially every
  non-presentation stub call in a combat run. Fixing a family closes many
  symptoms at once; fixing symptoms closes one each.
- **The instrument already exists** (below) and runs in a minute with no repro.

Keep a bug-hunting loop only for the other half of the problem: the **361
implemented natives**. Those do not fail by absence, they fail by *convention* —
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
pinning and glass are exercised at all. It read 72 distinct / 9,977 calls at the
census, and 71 / 5,624 once Tier 0 landed. It is not bit-identical between runs
(the scripts use `math.random`, so a fight ends slightly differently and the
rarer pin natives come and go); the volumes are stable. Static counts below are
**live** call sites,
comment lines excluded: `WORLD.SetWorldSpeed` looks like 11 sites and is 3, the
rest being `debugMarek` debris.

## Tier 0 — the singleplayer no-ops — DONE

Five names were 43% of every stub call in a combat run and none of them has work
to do in singleplayer. Each is now implemented as what the binary does rather
than as a placeholder; the recovered shapes and the two that are *not* free
no-ops are in [`LuaHost.md`](Reference/LuaHost.md), "The singleplayer no-ops".

| Native | Calls / 900 frames | Now |
|---|---|---|
| `CONSOLE.DemoIsPlaying` | 2250 | `false` — no recorder |
| `NET.IsPlayingRecording` | 900 | `false`, which is the shipped thunk's own constant |
| `PLAYER.SetMPByte` / `GetMPByte` | 900 | a byte on the entity, read back |
| `ENTITY.EnableNetworkSynchronization` | 242 | validates and returns |
| `ENTITY.SetSynchroString` / `GetSynchroString` | 29 | stored; the getter answers `""`, not nil |

Measured: the combat run went from **72 distinct / 9,977 calls** to **71 /
5,624**, a 43.6% drop, with no gameplay change — that was the point. `GetMPByte`
and `GetSynchroString` are new implementations rather than no-ops because both
are read back, and `nil ~= ""` is the inverted-test shape.

## Tier 1 — silent breakage on shared paths

### 1. Corpse pinning and ragdoll collision groups — MOSTLY DONE

**Correction to the census: pinning already worked.** `Stake:Tick`'s whole nail
path — `GetHavokBodyPosition`, `LineTrace`, `IsFixedMesh`,
`SetHavokBodyPosition`, `PinHavokBody` — was already implemented, so a stake
does pin a body to a wall in play. What was missing was everything asked about
a pinned corpse *afterwards*, and it is now in: `MDL.SetPinned` / `IsPinned` /
`SetPinnedJoint` / `IsPinnedJoint`, `PHYSICS.IsHavokBodyPinned` /
`SetHavokBodyVelocity`, `MDL.GetRagdollJointPos` / `GetRagdollJointRotation`,
and `MDL.ApplyVelocitiesToJoint` / `ApplyVelocitiesToJointLinked` — the shapes,
addresses and the constraint-graph rule behind "Linked" are in
[`Physics.md`](Reference/Physics.md), "Pinning a CORPSE".

That closes three inverted tests (`CActor:Electrize`, `meat.lua`,
`PainHead:Tick`), the Painkiller's corpse throw, and the Executioner, Loki,
Vamp_Big and Apoc_zombie ragdoll moves.

`MDL.SetRagdollCollisionGroup` / `GetRagdollCollisionGroup` and
`MDL.EnableRagdoll`'s third argument are in too, so a corpse carries its group
(a Cathedral corpse reads back 10). **What the group MEANS is still unrecovered**
— the Havok group-pair filter — so only `Noncolliding` (7) changes the layer and
the rest collide as before. A reading of the [10,19] band was tried, appeared to
measure worse, and turned out to be an uncontrolled A/B against a
non-deterministic fight; it was withdrawn. Same page, "A corpse's collision
group", including how to test it properly.

### 2. Death zones — DONE

`ENTITY.EnableDeathZoneTest` (10 sites: `Game:CreatePlayer`, `CActor`, `CItem`,
`CPlayer`) and `WORLD.EnableDeathZone` (1, `CAction`). Nothing that fell out of
the world died — not the player into a pit, not a monster, not a dropped item.
Every actor asks for the test at spawn, so it was a shared path rather than a
per-level feature.

A death zone turned out to be a **map object named `deathzone*`**, found the
same way water is; Cathedral has six. The zones, the `IN_DEATH_ZONE` message
they post and the one deviation (an AABB where the original picks between two
volume tests) are in [`Physics.md`](Reference/Physics.md), "Death zones".

### 3. Breakable glass — DONE

`WORLD.CheckStartGlass` — **443 calls in 900 combat frames** through a single
funnel, `CheckStartGlass()` in `Main/Utils.lua:662`, which on a true return
calls `Game:OnBrokenGlass`. One native; every window in the game.

A pane is a map object named `*glass*` (Prison has 279, Cemetery one, Cathedral
none — the call count is per impact, not per pane), and each is its own static
body now, so breaking one removes it from collision and from the drawn world.
The rule, the recovered attachment lookup and the two things not carried —
shards, and a savegame remembering what is already broken — are in
[`Physics.md`](Reference/Physics.md), "Glass".

### 4. Mesh-group toggling — DONE

| Native | Live sites | Where |
|---|---|---|
| `WORLD.EnableDrawMeshGroup` | 39 | Levels:30, Monsters:9 |
| `MESH.SetMeshGroup` | 18 | Levels:15 |
| `PHYSICS.StaticMeshGroupEnable` | 14 | Levels:14 |
| `WORLD.SetCollisionGroupMeshGroup` | 13 | Monsters:13 |
| `WORLD.SetTimeToDeleteMeshGroup` | 9 | Monsters:9 |

**Correction to the census: this is the two boss arenas, not campaign-wide level
scripting.** Gates and bridges work by other means; the site counts above are
concentrated in `C4L4_Alastor.lua`, `C5L4_Hell.lua` and `Alastor.lua`, plus a
few props that use `MESH.SetMeshGroup` to join a group they were not authored
into. What the family actually does is let an arena morph: `4x04_Alastor.mpk`
carries 1,990 `actgrp` objects and shows a few groups at a time.

All five are in. The engine calls behind them, the confirmed 0.0 delete
sentinel and the measurements are in [`Physics.md`](Reference/Physics.md),
"Mesh groups".

### 5. Collision plumbing — DONE

`ENTITY.PO_Activate` (9 sites, **218 calls a run**), `EnableCollisionsToAll`
(5), `EnableGunPass` (2), `PO_Impulse` (5), `PO_GetMass` (5), `SetLocalBBox`
(2). All in; shapes, the two impulse constants and the contact lottery are in
[`Physics.md`](Reference/Physics.md), "The body natives".

`PO_Activate`'s default turned out to be **false** — a bare call sleeps a body —
and `EnableCollisionsToAll` is not per entity at all: it arms a mass-filtered
share of one active-mesh group and returns the count.

## Tier 2 — scoped progression blockers

### 6. Riding things

`ENTITY.PO_MaintainVelocity` (28), `PO_MaintainPosition` (13),
`PO_MaintainLinearMovement` (9) and `PO_EnableSpeedDamping` (11) have their live
sites in the same six templates: `C2L1_Drezynka` (the handcar), `C3L3_Truck`,
`C5L2_Winda` (the lift), `MovingBar`, `MovingBoulder`, `klocKiller`. With
`ENTITY.PO_SetAsTransporter` (4 — the Factory conveyor belts) and
`ENTITY.PO_SetAsTransporter` (4 — the Factory conveyor belts), this family is
"the player rides a moving thing".

**The five movers are in**, as a per-step servo (`StepMovers`) standing in for
the Havok actions, which cannot be recovered further — the laws are a
reconstruction from the argument lists and are marked as such in
[`Physics.md`](Reference/Physics.md), "The scripted movers", along with the
measurements and the `stopBelow / gain` deadband.

Riding one works — the pawn takes its floor body's velocity, confirmed in play
on the Factory belts. **Left:** `PLAYER.AttachToUnderBody`, the handcar's
explicit attach.

### 7. Being thrown by a monster — DONE

**Correction to the census: `PO_SetPlayerFlying` has nothing to do with flying.**
It calls `PhysicsObject::SetPlayerShocked(float)` — seconds of no control — and
every caller follows it with `SetVelocity` on the player. It is a monster
throwing you: the Giant 0.5, Deto and the Executioner 0.33, ordinary melee 0.3.
Flying monsters were never this native's business; `PO_SetFlying` /
`PO_IsFlying` were already done, which is why the crows work.

The timer, why stripping the input alone was not enough, and the measurement
(a grounded throw carries 7.400 against 4.641) are in
[`PlayerMovement.md`](Reference/PlayerMovement.md), "Being thrown" — together
with a TestFloor fight showing that the ORDINARY melee site passes a nil handle
and is a no-op in the shipped game too, so what little shove it gives comes from
`SetVelocity` and is spent by the walk factor.

`ENTITY.PO_SetPlayerShocked` is a different function despite the name and stays
stubbed.

### 8. Scripted ragdoll moves — DONE

`MDL.ApplyVelocitiesToJoint` (16), `ApplyRotationToJoint` (15, Monsters:14),
`ApplyPositionToJoint` (6), `GetRagdollJointPos` / `GetRagdollJointRotation`
(12, Monsters), `MoveAllJoints`, `CopyMatrixFromJointToJoint`,
`SetJointPositionLowLevel`, `ApplyVelocitiesToJointLinked`. This is how a
monster carries something: it poses one joint every tick.

**Done except `CopyMatrixFromJointToJoint`** (one live site). The
argument-count branch in `ApplyRotationToJoint` — three numbers are an Euler,
four a quaternion — the exact round trips, and a mirrored-rotation bug this
caught in `GetRagdollJointRotation` are in
[`Physics.md`](Reference/Physics.md), "Holding a body by one joint". The users
are Leper_monk, Preacher, Skull, Pinokio and Apoc_zombie_V2; Leper and Vamp_Big
have their own calls commented out with *"chyba to nie dziala...?"*, so the
original's authors thought it did not work.

### 9. Boss attacks — DONE

`WORLD.ExplosionUp` and `WORLD.ExplosionParabolic` — one live site each, both
Thor's (the hammer slam and the fists). The laws, the constants and the reason
**both are velocities rather than impulses** — Thor's own `stren = 80` and
`flightTime = 8` make no sense read the other way — are in
[`Physics.md`](Reference/Physics.md), "The boss explosions". Measured: a
parabolic throw at a target 10 away lands 12.23 out against the predicted 12.5.

Left: the unidentified body field both functions filter on.

## Tier 3 — feel and signature effects

| Native(s) | Live sites | What is missing |
|---|---|---|
| `ENTITY.AttachTrailToBones` | 26 | weapon and monster motion trails (hit at runtime) |
| `CAM.SetRotationDisplacement` | 8 | camera kick on hits |
| `CAM.LookAt` | 6 | scripted camera moves |
| `ENTITY.PO_SetPlayerShocked` | 8 | the electrodriver stun |
| `ENTITY.EnableDemonic`, `WORLD.EnableDemonFX` / `DemonFXParams` / `DemonFXWarp` / `EnableSuperDemonFX` | 12 | Demon Morph |
| `PHYSICS.SetBunnyHopAcceleration` (7), `PHYSICS.SetGravity` (3) | 10 | movement knobs the scripts read out of `Tweak` |
| `INP.GetTimeFromTimerReset`, `INP.ResetTimer` | 12 | script-side timers |
| `ENTITY.TransformLocalPointToWorld` (7), `GetCenter` (3), `SeesPoint` | 11 | small geometry queries whose returns are consumed |
| `ENTITY.PO_SetMissile` | 10 sites, 23 calls | **check first**: `Physics.md` records it as netcode-only |
| `WORLD.SetWorldSpeed` | 3 | slow motion. Almost every call site is commented-out debug, so this is not the mechanic it appears to be |

## The stubs that RAISE rather than fail quietly

Most stubs fail silently, but a stub whose return feeds arithmetic or a table
index raises instead — and the error unwinds out of whatever tick called it,
taking the rest of that frame's work with it (this is how `Hud:Quad` used to
kill everything after `Hud:Render`). These are consumed at **every** live call
site, so any one of them that is reached is a hard stop, not a degradation:

`R3D.VectorToScreen` (25), `MDL.GetRagdollJointRotation` (8),
`INP.GetTimeFromTimerReset` (8), `CAM.GetRightVector` (8),
`ENTITY.TransformLocalPointToWorld` (7), `ENTITY.PO_GetMass` (6),
`MDL.GetRagdollJointPos` (4), `R3D.DistToCamera` (3), `CAM.GetUpVector` (3),
`ENTITY.GetCenter` (3), `MESH.GetRandomPoint` (2), `INP.GetTimeDelta` (2),
`ENTITY.GetFileName` (2), `ENTITY.Exist` (2), `PHYSICS.GetHavokBodyActiveGroup`
(2), `WORLD.GetEntityList`, `WORLD.GetAmbientColor`, `PLAYER.GetPitch`,
`PHYSICS.IsHavokBodyPinned`, `MDL.IsPinnedJoint`, `ENTITY.SeesPoint`.

When a nil-arithmetic error turns up in play, look here first. It is also the
cheapest possible fix list — most of them are one query each.

## Tier 4 — presentation (most of the call volume, none of the behaviour)

Ranked by calls per 900 combat frames:

- ~~**Dynamic lights**~~ — done. `LIGHT.Setup` / `SetFalloff` (1002 each), the
  five flag setters (102 each) and `ENVIRONMENT.RemoveLight(s)` are
  implemented ([`Lighting.md`](Reference/Lighting.md)). Still stubs in the same
  area: `WORLD.SetDirLight`, `ENVIRONMENT.SetFog` / `SetWater` /
  `ResetReflectList`, `ENTITY.AddLight`.
- **Model and mesh materials** — `MESH.SetDetailMap` / `SetNormalMap` /
  `SetCubeMap` (245 each), `SetSpecular` / `AddSpecularLight` /
  `ResetSpecularLights` (~110), `MDL.CreateShadowMap` (38), `MATERIAL.Replace`
  (44 sites), `MDL.SetTexture` (32), `MDL.EnableNormalMaps` (7).
- **Acoustics** — `WORLD.FindEnvironmentAtPoint` (450), `SOUND3D.SetObstructed`
  / `SetIntensity` (135 each), `SOUND.PreloadFile` (214), `SOUND.SetRoomType`
  (31).
- **Visibility switching** — `WORLD.UseSwitchZones`, `EnablePortal`, the
  antiportal family, `EnableOcclude`. Correctness of what is drawn, and the
  frame cost of drawing it.
- **Glass shards.** A pane breaks and vanishes correctly; the original
  fractures it into pieces and this does not. Cosmetic, and the largest single
  item in this tier — generated geometry has no draw path yet
  ([`Physics.md`](Reference/Physics.md), "Glass").
- **Debug and HUD draw** — `R3D.RenderLine` (34), `RenderBox` (29),
  `VectorToScreen` (23), `DrawSphere` (14). `VectorToScreen`'s return is
  consumed at 22 of its 23 sites, so those raise rather than no-op if reached.

## Tier 5 — deprioritized

- **Multiplayer**: `NET` 39, `MPSTATS` 15, `GAMESPY` 4 = 58 stubs. Only the
  Tier 0 no-ops are worth touching.
- **Menus**: `PMENU`, 56 stubs (49 referenced). Needed for a front end, not for
  gameplay; [`Menu.md`](Reference/Menu.md) covers what already works.
- **Editor-only**: `MOUSE.RB` (16 sites, all Editor), the `WPT` selection
  family, `ENTITY.Pick`, `ENTITY.RenderBBox`.

## The queue

1. ~~Tier 0 no-ops~~ — done; the report is 43.6% quieter.
2. Pinning + ragdoll collision groups — the stakegun.
3. ~~Death zones, then glass~~ — both done.
4. ~~Mesh groups~~ done - the two boss arenas, not the campaign.
5. Maintain\* movers + transporters — unblocks C2L1, C3L2, C3L3, C5L2.
6. Scripted ragdoll joints, flying, boss explosions.
7. ~~Lights~~ done. Materials next, once the game plays and only looks wrong.

## How these numbers were produced

- Surface: `PK_NATIVE` / `PK_GLOBAL` rows in `Source/Script/NativeList.inc`.
- Implemented: `{"MODULE", "Name", fn}` rows across `Source/Game/Script*.cpp`
  and `Source/Script/Natives.cpp`. A native registered in some other shape would
  be misfiled as a stub; spot-checked against the runtime report, which agrees.
- Static sites: one awk pass over `LScripts`, `Items` and `Levels` matching
  `MODULE.Fn(`, flagging any match with a `--` earlier on the line. 8,446 call
  sites, 673 of them commented out.
- Runtime: the two `PainfulTools lua` runs above.
