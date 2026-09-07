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

**Still open, and it is the bigger half:** `MDL.SetRagdollCollisionGroup` (37
live sites) and `MDL.EnableRagdoll`'s ignored third argument. 53 monster scripts
enable their ragdoll as `RagdollNonColliding`; a simulated ragdoll here is
`Layers::kMoving` unconditionally, so **every corpse collides like an ordinary
prop.** The native's own rule is recovered (a group in [10,19] is replaced by a
rotating per-corpse value); what is not is the group-pair filter that says what
those groups may touch. Same page, "A corpse's collision group".

### 2. Death zones

`ENTITY.EnableDeathZoneTest` (10 sites: `Game:CreatePlayer`, `CActor`, `CItem`,
`CPlayer`) and `WORLD.EnableDeathZone` (1, `CAction`). 31 calls in the combat
run. Nothing that falls out of the world dies — not the player into a pit, not a
monster, not a dropped item. Every actor asks for this at spawn, so it is a
shared path, not a per-level feature.

### 3. Breakable glass

`WORLD.CheckStartGlass` — **443 calls in 900 combat frames** through a single
funnel, `CheckStartGlass()` in `Main/Utils.lua:662`, which on a true return
calls `Game:OnBrokenGlass`. One native; every window in the game.

### 4. Mesh-group toggling — how levels change shape

| Native | Live sites | Where |
|---|---|---|
| `WORLD.EnableDrawMeshGroup` | 39 | Levels:30, Monsters:9 |
| `MESH.SetMeshGroup` | 18 | Levels:15 |
| `PHYSICS.StaticMeshGroupEnable` | 14 | Levels:14 |
| `WORLD.SetCollisionGroupMeshGroup` | 13 | Monsters:13 |
| `WORLD.SetTimeToDeleteMeshGroup` | 9 | Monsters:9 |

This is how a level opens a gate, drops a bridge, reveals an arena or clears
debris. A silent no-op leaves geometry in its authored state — including solid,
drawn walls that were meant to vanish, which is a progression blocker rather
than a cosmetic one. `Levels/C4L4_Alastor/C4L4_Alastor.lua` toggles ten groups
in its opening alone.

### 5. Collision plumbing

`ENTITY.PO_Activate` (9 sites in `CObject` / `CActor` / `CAction`, **218 calls a
run** — the wake-up after every physics apply), `EnableCollisionsToAll` (5),
`EnableGunPass` (2), `PO_Impulse` (5), `PO_GetMass` (5), `SetLocalBBox` (2, in
`CEnvironment`). Cheap, and they decide what a shot and a body may touch.

## Tier 2 — scoped progression blockers

### 6. Riding things

`ENTITY.PO_MaintainVelocity` (28), `PO_MaintainPosition` (13),
`PO_MaintainLinearMovement` (9) and `PO_EnableSpeedDamping` (11) have their live
sites in the same six templates: `C2L1_Drezynka` (the handcar), `C3L3_Truck`,
`C5L2_Winda` (the lift), `MovingBar`, `MovingBoulder`, `klocKiller`. With
`ENTITY.PO_SetAsTransporter` (4 — the Factory conveyor belts) and
`PLAYER.AttachToUnderBody` / `DetachFromUnderBody`, this family is "the player
rides a moving thing", and the levels that need it cannot be finished without
it. Havok actions in the original; a per-step velocity/position servo in
`StepCharacters` here.

### 7. Flying monsters

`ENTITY.PO_SetPlayerFlying` (10, Monsters:9). `PO_SetFlying` is done; this is
the player-relative variant the ravens and Alastor use.

### 8. Scripted ragdoll moves

`MDL.ApplyVelocitiesToJoint` (16), `ApplyRotationToJoint` (15, Monsters:14),
`ApplyPositionToJoint` (6), `GetRagdollJointPos` / `GetRagdollJointRotation`
(12, Monsters), `MoveAllJoints`, `CopyMatrixFromJointToJoint`,
`SetJointPositionLowLevel`, `ApplyVelocitiesToJointLinked`. This is how the
Leper carries a brain, how Vamp_Big throws a body, how bosses pose. The read
half (`Get*`) is the dangerous kind: it answers `nil` and the caller does
arithmetic on it.

### 9. Boss attacks

`WORLD.ExplosionUp` and `WORLD.ExplosionParabolic` — one live site each (Thor,
the Panzer Demon). Small, but they gate two boss fights.

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

- **Dynamic lights** — `LIGHT.Setup` / `SetFalloff` (1002 each), the five flag
  setters (102 each), `ENVIRONMENT.SetAmbient` / `SetFog` / `SetDirLight` /
  `RemoveLights` / `SetWater` / `ResetReflectList` (~350). The single biggest
  visual gap.
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
3. Death zones, then glass — two small natives on shared paths.
4. Mesh groups — unblocks level scripting across the campaign.
5. Maintain\* movers + transporters — unblocks C2L1, C3L2, C3L3, C5L2.
6. Scripted ragdoll joints, flying, boss explosions.
7. Lights and materials, once the game plays and only looks wrong.

## How these numbers were produced

- Surface: `PK_NATIVE` / `PK_GLOBAL` rows in `Source/Script/NativeList.inc`.
- Implemented: `{"MODULE", "Name", fn}` rows across `Source/Game/Script*.cpp`
  and `Source/Script/Natives.cpp`. A native registered in some other shape would
  be misfiled as a stub; spot-checked against the runtime report, which agrees.
- Static sites: one awk pass over `LScripts`, `Items` and `Levels` matching
  `MODULE.Fn(`, flagging any match with a `--` earlier on the line. 8,446 call
  sites, 673 of them commented out.
- Runtime: the two `PainfulTools lua` runs above.
