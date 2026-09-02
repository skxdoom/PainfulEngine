# Plan — what is left, and why in this order

The forward-looking half of the project. What already works is in
[`Status.md`](Status.md); the recovered rules are in [`Reference/`](Reference).

## The strategic finding

**Painkiller's gameplay logic is not compiled.** It lives in 431 Lua 5.0 files plus
thousands of serialised property tables (`.CActor`, `.CItem`, `.CWeapon`, ...).
Weapons, monsters, AI, HUD, menus and level scripting are all script-side.

That changes what a source port *is*. You are not reimplementing Painkiller's game
design — you are implementing the **native API those scripts call**, and letting the
original scripts drive it. The porting specification is therefore concrete and
finite rather than open-ended:

| Measure | Count |
|---|---|
| Native Lua functions recovered from `Engine.dll` | 941 |
| ...excluding Lua's own standard library | ~846 |
| ...in the generated surface (`NativeList.inc`) | 839 |
| ...referenced by the shipped scripts | 638 |
| **of those 638: implemented** | **232** |
| **of those 638: still instrumented stubs** | **406** |

A call-frequency-ranked list is in [`native_priority.tsv`](Data/native_priority.tsv)
(name, call count, module). 33 further natives are implemented outside the generated
list — aliases and helpers such as `WORLD.LineTraceHitPlayerBalls` — so the whole
implemented set is 312.

Excluding the families that are not gameplay — `NET`, `MPSTATS`, `GAMESPY`, `PMENU`,
`CONSOLE`, `EDITOR`, `FS`, `MBOARD` — **246 stubs remain that shipped scripts call.**
That number is the remaining work, and the stages below are it, ordered.


## The measurement

```
PainfulTools lua <DataRoot> 400 C1L1_Cathedral
```

boots the shipped scripts, loads Cathedral, runs `Game:OnPlay` and ticks 400 frames,
then prints every native the scripts called that we have not written. Current
reading:

```
boot: 964 files loaded, 0 missing, 0 script errors
entities: 838 created, 202 released, 684 live
unimplemented natives hit: 99 distinct, 11428 calls
```

(The live count climbs in a long headless run — 779 at 1200 frames — because
the footstep effect `but` is a one-shot that only the windowed particle
reaper collects; with no particle renderer nothing finishes. A harness
artifact, not a leak in the game loop.)

against 153 distinct / 82,843 calls at the start of Stage 7. Re-run after every
stage; the report is the progress bar.

**What the report still does not see.** It is an **idle run**: nothing fires,
nothing takes damage, no monster engages a target. So the weapon path, the
explosion family, the pin family and most of the AI natives are absent from the
ranked list for lack of exercise, not because they work. The static sweep —
every `MODULE.Fn` reference across the shipped `.lua`/`.state`/property files,
intersected with the stub set — is the fuller picture, and it is what ranks the
stages below. (It used to throw every frame as well; that is I1, now fixed.)


## Immediate — DONE

All three landed together. The report now exits 0 with **0 script errors**
(was 400) and **102 distinct / 11,618 calls**.

### I1. The headless report threw on all 400 frames — fixed

`MATERIAL.Create` answered nil without a renderer, and `Hud:Quad`'s own
"material not found" diagnostic concatenates the handle, so the error rose out
of `Game:PostRender` and took everything after `Hud:Render` with it. The `lua`
command now gets the texture INDEX only — `TextureCache::Init(root, false)`,
no graphics device — and a new `TextureCache::Measure` reads image headers on
the CPU, so `MATERIAL.Size` answers real dimensions. `Hud:Render` went from
aborting on frame 1 to 5592 `DrawQuad` calls over 200 frames. Rule and
evidence: [`Hud.md`](Reference/Hud.md).

### I2. A stationary monster was never depenetrated — fixed

The sub-skin skip path in `TickMonsters` returned before `SlideSphere`, the
only caller of `Depenetrate`, and an actor with `onFloor` latched true never
accumulates enough residual to sweep again. It now sweeps a zero delta
instead, with the correction adopted only when it exceeds the sweep skin —
taking the hairline overlap a resting sphere reports walks the actor upward
instead. Measured both ways against the same probe:
[`MonsterMovement.md`](Reference/MonsterMovement.md).

### I3. Leftover scaffolding — partly done

The `TRACK` debug log is out of `TickMonsters`. Still misfiled: the physics
natives (`PO_SetMass` / `SetFriction` / `SetRestitution` / `SetLinearDamping` /
`SetAngularDamping`) and `SeesEntity` live in `Game/ScriptSound.cpp`. Moving
them is a rename-only change and wants its own commit.

## The stages

Ordered gameplay first. Presentation — decals, dynamic lights, model materials, FOV,
acoustics, portal switching — is real work and it is at the bottom, because none of
it changes what the game *does*.

### Stages 7–12 — LANDED

Player actions and the camera handover (7), weapons fire and the intersection solver
(8), the animation clock and skinning (9), monster locomotion and the `PO_Move` /
`PO_IsOnFloor` / `SeesEntity` / `WPT.Load` chain (10), the HUD quad pipeline (11) and
the `SOUND` families (12) are all in. What each of them does now is in
[`Status.md`](Status.md); the rules they obey are in [`Reference/`](Reference).

The lesson from Stage 8 is worth carrying forward: **before building a system, check
whether the scripts already are it.** Damage needed no native work at all — a weapon
traces, looks the hit entity up in `EntityToObject` and calls `obj:OnDamage`. Only
the *reaction* was missing.

### Stage 13 — explosions — DONE

`WORLD.Explosion2` is real: the falloff, the constants, the message contract
and what is still missing are in [`Physics.md`](Reference/Physics.md). Damage
is script-side as it was for the weapon traces — the engine posts one
`EXPLOSION` per entity reached and `Game_GetMsg` calls `obj:OnDamage`.
`MultiplayerExplosion` routes to the same blast, and
nTwo things reported from play landed with it: wreckage now inherits the item's
velocity (it was dropping in place), and `ENTITY.PO_SetPinned` is real, which
is what the Catacombs blockade needs — it is scenery until an ambush box runs
`Pin:...,false` and `SetImmortal:...,false`. Both in
[`Physics.md`](Reference/Physics.md).
`ENTITY.PO_SetMovedByExplosions` gates the impulse without gating the damage.

Two things carried forward rather than settled:

- **Strength is taken as an impulse.** The engine calls it a force and spends
  it once per step, which would be `force * dt` — and that moves a barrel
  0.000. Raw gives 4.012, which is a plausible rocket throw. Assumed, and the
  first number to retune if blasts feel wrong.
- **`WorldMesh::GetClosestPoint` is not ported**, so a large fixed mesh is
  measured from its body rather than its nearest surface, and the second pass
  over world meshes at `0.6 * range` does not run.

`ExplosionUp` and `ExplosionParabolic` remain stubs — boss moves for Thor and
the Panzer Demon, two of three call sites commented out in the shipped data.

### Stage 14 — monster ground contact

I2 is fixed, and so is the character pile-up that came out of testing it:
two characters now part sideways at a limited rate instead of ejecting each
other, and a monster can feel the player. Three structural problems remain,
all in `Game/ScriptMonster.cpp`:

1. **No step-up.** `PlayerPawn` runs the recovered `StepCheck` ladder and climbs to
   0.86 (see [`PlayerMovement.md`](Reference/PlayerMovement.md)). `TickMonsters`
   calls raw `SlideSphere`, so a monster stops dead at any lip the player strolls
   over. The ladder should be shared, not reimplemented.
2. **The floor normal is a literal.** `e.floorNormal` is hardcoded to `(0,1,0)` with
   a comment saying it is not measured yet. `CAiBrain.lua` reads it back out of
   `PO_IsOnFloor`, so the AI cannot tell a slope from flat ground. `SlideSphere`
   already has the contact normal in hand.
3. **Two different shapes.** Movement sweeps a sphere of the *smaller* horizontal
   half-extent placed at soles+radius — a ball at shin height — while the body
   everything else collides with is three stacked spheres spanning the model. The
   torso is swept by nothing.

Also open from Stage 10: `MonsterBodyScale`'s `k = height / 10.3` is a shape
argument, not a recovered constant. The engine's reference point (`Entity+0x58`) is
still unidentified — see [`MonsterMovement.md`](Reference/MonsterMovement.md).

### Stage 14b — active meshes

**Reported from play: the heavy stones at the Catacombs entrance ignore a crate
going off beside them.** They are not items — they are WORLD MESH objects the
`.mpk` marks as rigid bodies in the object name, 31 of them named
`pinned_phys_wejsciowy_kamienshape*`, and we build the collidable world as one
static body so nothing named that way can move on any level.

452 objects on Catacombs, 32 on Prison, 7 on Factory. The naming convention, the
group assignment (`Level_GetActiveMeshesData`, which the ENGINE calls), the
level's `ActiveMeshesData` table and the group range the scripts configure are
all in [`Physics.md`](Reference/Physics.md).

The order that matters: splitting them into bodies is straightforward, drawing
them where they moved to is not — world geometry is baked into static vertex
buffers and an active mesh needs a per-object transform. Do the draw path first
or the work is unverifiable.

### Stage 15 — pinning, and the stakegun

`ENTITY.PO_SetPinned` / `PO_IsPinned` landed with Stage 13 (props go static and
back). What is left is the CORPSE side.

The stake's pin handler dies on a nil **nine lines before** it reaches the wall test.
`Templates/Weapons/Stake.lua`:

```lua
local hx,hy,hz = PHYSICS.GetHavokBodyPosition(he)   -- stub: returns nothing
self.ox,self.oy,self.oz = x-hx,y-hy,z-hz            -- raises here
```

So `Stake:Tick` never reaches `if b and ENTITY.IsFixedMesh(e1)`, and no stake has
ever attempted to pin anything. `IsFixedMesh` and `WORLD.LineTrace` are both fine;
only the body accessors are missing.

The rest of the chain, all stubs: `PHYSICS.PinHavokBody`, `SetHavokBodyPosition`,
`IsHavokBodyPinned`, `ENTITY.GetIndex`, `ENTITY.PO_GetPhysicsBody`,
`MDL.ApplyVelocitiesToAllJoints`.

Behind that sits the corpse-pin layer the feature actually rests on —
`ENTITY.PO_SetPinned` (48 sites), `MDL.SetRagdollCollisionGroup` (47),
`MDL.SetPinnedJoint` (38), `MDL.SetPinned` (17), `MDL.IsPinned`, `MDL.IsPinnedJoint`,
`ENTITY.PO_IsPinned`. `World/PhysicsWorld.cpp` already branches
`pinned ? Static : Dynamic` when building ragdoll parts; nothing reaches it yet.

### Stage 16 — navigation queries

**Routing already works** — `PATH.Create` / `Release` / `GetShortest` /
`IsFinished` / `GetNextPoint` run over a real `.wps` graph, and `WPT.Load`
parses it. What is missing is narrower, and it is self-placement rather than
navigation:

`WPT.GetClosest` and `WPT.GetPosition` are stubs, and the recovery that uses
them —

```lua
local zn,idx = WPT.GetClosest(x,y,z)
if idx > -1 then x,y,z = WPT.GetPosition(zn,idx) end
```

— appears in `Zombie_2`, `Apoc_zombie_V2`, `StoneGolem`, `Lucifer` and
`AlastorKing`, guarded exactly like that. The stub returns nil, the guard reads
false, and the correction is **silently skipped**, so a monster that leaves the
walkable set is never put back on it.

Worth doing first, from [`MonsterMovement.md`](Reference/MonsterMovement.md)'s
own open lead: `GetShortest` snaps to the nearest waypoint by 3D distance with
the **floor index ignored**, so an actor can bind to a waypoint on the storey
above or below and be handed a route it cannot walk. The `floor` field is
parsed and unused, and it exists precisely to disambiguate that. That is the
likeliest cause of "some monsters walk in place".

Then `WPT.GetPathsNumber`, `GetWaypointByPathNumber`, `GetLength`,
`FastPickCurrentSet`, `EnableDisableSet` for the patrol paths.

### Stage 17 — the scripted and flying movers

Whole families of monster motion have no mover at all. Flying enemies (Alastor, the
ravens) currently cannot move by any path:

`ENTITY.PO_SetFlying` (24 sites), `PO_SetPlayerFlying` (11), `PO_IsFlying`,
`PO_MaintainVelocity` (29), `PO_MaintainLinearMovement` (21), `PO_MaintainPosition`
(14), `PO_EnableSpeedDamping` (12).

`Sees()` in `Game/ScriptSound.cpp` also measures between entity **origins**, where
`CalculatePawnToEntityVisibility` (0x10198D30) takes both pawns' **head** positions.
Worth correcting here, since it is the same subsystem.

### Stage 18 — grenade body semantics — DONE

`PO_SetGrenade` and `PO_SetFreedomOfRotation` are real, the grenade is a
simulated sphere again, and the rocket flies nose first. What the binary said
about all of it — including that `PO_SetMissile` is netcode-only and that
`PO_SetFriction` / `PO_SetRestitution` never reached Havok in the first place —
is in [`Physics.md`](Reference/Physics.md), under Projectiles. `Restitution =
1.4` turned out to be a value the solver never saw.

Left open there: the original's collision-group filter (what a missile may
touch is an assumption), and the sizer's cinfo material read (friction 0.5,
restitution 0.9) rests on a stack-offset mapping — worth a second look if
props feel too lively.

### Stage 19 — collision-group and contact plumbing

Broad, cheap, and it unblocks parts of the four stages above:

`ENTITY.PO_SetCollisionGroup` (91 sites), `PO_SetMovedByExplosions` (71 — Stage 13
needs it to know what a blast may push), `EnableCollisionsToRagdoll` (25),
`EnableCollisionsToAll`, `PO_Activate`, `PO_SetPlayerShocked`, `EnableGunPass`,
`EnableDeathZoneTest`, `WORLD.EnableDeathZone`, `WORLD.SetCollisionGroupMeshGroup`.

`CreateScriptBody` already takes the group and switches on 1 (Fixed) and 7
(Noncolliding); this is the rest of that switch reaching the layer filters.

### Stage 20 — gibbing and the ragdoll joint API

`MDL.MakeGib`, `RagdollSelfExplosion`, `SetRagdollMovedByExplosions`,
`SetRagdollRestitution`, `SetRagdollBreakablesThreshold`, `SetRagdollHardDeactivator`,
`GetRagdollJointPos` / `GetRagdollJointRotation`, `BreakConstraintsForJoint`,
`ApplyVelocitiesToJoint` / `ToJointLinked` / `ToAllJoints`, `ApplyPositionToJoint`,
`ApplyRotationToJoint` / `ApplyRotationQuaternionToJoint`, `MoveAllJoints`,
`CopyMatrixFromJointToJoint`, `GetClosestJoint`, `SetJointPositionLowLevel`,
`ENTITY.RecreateRagdollIfNone`.

Gate this on Stage 13: `RagdollSelfExplosion` and the `MovedByExplosions` pair have
nothing to react to until explosions exist.

### Stage 21 — world and lifetime

- `PARTICLE.Die` is done ([`Particles.md`](Reference/Particles.md)).
  `PARTICLE.Restart` and `SetImmortal` sit with it and are still stubs.
- `WORLD.SetWorldSpeed` — slow motion; already flagged as an assumption in
  `Game/PlayerPawn.h`. `PHYSICS.SetBunnyHopAcceleration` and `PHYSICS.SetGravity`
  belong here too.
- `WORLD.RemoveEntity`, `DeleteDyingEntities`, `DeleteDelayedEntities`,
  `UpdateAllEntities`, `GetEntityList`, `AdvanceFrameCounter` / `GetFrameCounter`.
- `WORLD.CheckStartGlass`, `IsUnderwater` / `MakeUnderwater`,
  `PHYSICS.ActiveMeshGroup*` (the breakable mesh groups).
- `INP.GetTimeFromTimerReset` and the rest of the `INP` timer family.

### Stage 22 and after — presentation

Real work, none of it changes behaviour. Roughly in the order things stop looking
wrong:

| | |
|---|---|
| Decals | `ENTITY.SpawnDecal` (35), `SpawnOrientedDecal`, `ReloadDecalSystem`, `R3D.KeepDecals`. Every impact mark in the game. |
| Dynamic lights | `LIGHT.Setup` / `SetFalloff` (502 calls a run) and the six flag setters; `ENVIRONMENT.SetAmbient` / `SetFog` / `SetDirLight` / `RemoveLights` (250). |
| Model materials | `MESH.SetDetailMap` / `SetNormalMap` / `SetCubeMap` / `SetSpecular` / `AddSpecularLight` (648); `MDL.SetMaterial`, `SetTexture`, `EnableNormalMaps`, `MATERIAL.Replace` (44). |
| Camera | `R3D.SetCameraFOV` (401) — FOV is fixed, so no zoom and no FX. |
| Acoustics | `WORLD.FindEnvironmentAtPoint` (200), `SOUND.SetRoomType`, `SOUND3D.SetObstructed` / `SetIntensity`. `SOUND.SetSoundProperties` is done ([`Sound.md`](Reference/Sound.md)). |
| Visibility switching | `WORLD.UseSwitchZones` (398), `EnablePortal`, the antiportal family, `EnableDrawMeshGroup`. |
| Streams | `SOUND.StreamLoad` / `StreamPlay` / `StreamPause` / `StreamSetVolume` — music. |
| Menus and save/load | the `PMENU` surface (102 stubs), `WORLD.SaveGame` / `LoadGame`. |
| Netcode | `NET`, `MPSTATS`, `GAMESPY`, `ENTITY.EnableNetworkSynchronization`, `SetSynchroString`. Last, and possibly never. |


## Known unknowns

- **Native signatures.** We have names and addresses but not argument lists. The
  instrumented stubs are the cheapest way to recover them — log the actual Lua
  arguments at runtime rather than reading disassembly.
- **Explosion falloff.** See Stage 13.
- **Havok vs Jolt restitution.** See Stage 18.
- **`MonsterBodyScale`.** See Stage 14.
- **Gameplay feel.** `PhysicsObject::FixHavokPositionBug` shows behaviour was tuned
  around Havok's quirks; ragdoll feel under Jolt will differ and need retuning.
- Material blocks are solved for both formats. Remaining: 7 of 2,532 model meshes
  fail the exact-landing material parse, and a few per-object bytes after the last
  `.mpk` material are unmapped. Neither blocks rendering.
- The `.pkmdl` geometry header preceding the index array varies between models, so
  PainKit uses a strict-then-loose heuristic. Fully mapping it would remove that.


## Open questions

- The ~11 small unidentified `luaL_reg` tables (1–8 functions each) in
  `PainfulEngineHelpers/Engine_LuaAPI.md` — worth naming to complete the module map.
- 201 of the 839 names in the generated surface are never referenced by the shipped
  scripts. Dead API, debug-only, or used by content not in this install? Some may be
  interesting.
- Exact `k0` seed generator (per-entry). Brute force sidesteps it for extraction,
  but repacking to a byte-identical archive needs the real formula. Leads: `k0`
  correlates loosely with entry index; `r = (k0 - 2*(nl+1)) & 0xFF` tracks the index
  with noise — likely a running counter or an FIdx-derived value.
- Loose-dir vs pak precedence.
- `.pkm` internal format (same as `.pak`? a zip? — `GZipPack::GetFile` exists,
  suggesting the engine also supports real ZIP archives).
