# Plan — what is left, and why in this order

The forward-looking half of the project. What already works is in
[`Status.md`](Status.md); the recovered rules are in [`Reference/`](Reference).

**The work queue itself is [`Stubs.md`](Stubs.md)** — every remaining stub,
ranked by cost and measured against a combat run. This file holds what a stub
count cannot: the strategy, the stand-ins that are argued rather than
recovered, and the open questions.

## The strategic finding

**Painkiller's gameplay logic is not compiled.** It lives in 431 Lua 5.0 files plus
thousands of serialised property tables (`.CActor`, `.CItem`, `.CWeapon`, ...).
Weapons, monsters, AI, HUD, menus and level scripting are all script-side.

That changes what a source port *is*. You are not reimplementing Painkiller's game
design — you are implementing the **native API those scripts call**, and letting the
original scripts drive it. The porting specification is therefore concrete and
finite rather than open-ended: 941 native Lua functions recovered from
`Engine.dll`, 791 module natives in the generated surface, 695 of them
referenced by the shipped scripts. The implemented / stub split and how it is
counted are in [`Stubs.md`](Stubs.md).

A call-frequency-ranked list is in [`native_priority.tsv`](Data/native_priority.tsv)
(name, call count, module).

**Before building a system, check whether the scripts already are it.** Damage
needed no native work at all — a weapon traces, looks the hit entity up in
`EntityToObject` and calls `obj:OnDamage`. Only the *reaction* was missing.

## The order

Gameplay first. Presentation — model materials, acoustics, portal switching —
is real work and it is at the bottom, because none of it changes what the game
*does*. As of 2026-09-19 every shared gameplay family is in and the combat
report is almost entirely presentation, so the order from here is:

1. The gameplay leftovers ([`Stubs.md`](Stubs.md), Tier 1).
2. The stand-ins below, as play-testing points at them.
3. Feel (Tier 2), then presentation (Tier 3).
4. Netcode. Last, and possibly never.

## Argued rather than recovered

Where the port runs on an assumption instead of a recovered rule. Each page
says what would settle it; this is the index, and where to look first when the
feel is off.

| | |
|---|---|
| Explosion strength taken as an impulse, not `force * dt` | [`Physics.md`](Reference/Physics.md), Explosions |
| `WorldMesh::GetClosestPoint` not ported, so the second blast pass over world meshes does not run | same |
| Monster mass and the player's push; the wall slide; `StandCharacterOnFloor` | [`MonsterMovement.md`](Reference/MonsterMovement.md) |
| Stairs and slopes under the dynamic monster body — unmeasured | same |
| Waypoint routing ignores the `floor` index; whether storey-to-storey portal routing ever differs from flat A* is unmeasured | same, "The waypoint graph" |
| The collision-group pair filter (what a missile or a corpse may touch) | [`Physics.md`](Reference/Physics.md), "A corpse's collision group" |
| The sizer's cinfo material read (friction 0.5, restitution 0.9) rests on a stack-offset mapping | [`Physics.md`](Reference/Physics.md), Projectiles |
| The scripted movers are a servo reconstructed from argument lists | [`Physics.md`](Reference/Physics.md), "The scripted movers" |
| Active meshes: autodelete timers, the activation lottery, `physdest` damping, hulls for concave bodies | [`Physics.md`](Reference/Physics.md) |
| The Demon Morph fresnel scale, set by eye | [`DemonFx.md`](Reference/DemonFx.md) |

## Housekeeping

- The physics natives (`PO_SetMass` / `SetFriction` / `SetRestitution` /
  `SetLinearDamping` / `SetAngularDamping`) and `SeesEntity` still live in
  `Game/ScriptSound.cpp`. Moving them is a rename-only change and wants its own
  commit.

## Known unknowns

- **Native signatures.** We have names and addresses but not argument lists. The
  instrumented stubs are the cheapest way to see what the scripts pass; the
  binary is the authority on what the native does with it.
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
- About 100 module natives in the generated surface are never referenced by the
  shipped scripts. Dead API, debug-only, or used by content not in this install?
- Loose-dir vs pak precedence.
- `.pkm` internal format (same as `.pak`? a zip? — `GZipPack::GetFile` exists,
  suggesting the engine also supports real ZIP archives).
