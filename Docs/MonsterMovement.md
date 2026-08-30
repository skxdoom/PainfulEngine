# Monster movement — scope

Monsters walk. What follows is the map of how, what is measured, and what is
still missing - written the way `Animation.md` was, after three sessions of
fixing one native at a time failed to close it.

**The last blocker was self-inflicted.** The mover computed its step and never
applied it: a probe-removal `sed` deleted the `SlideSphere` call out of
`TickMonsters`, and every actor sat at a position that never changed by a
single float. That is the signature to remember - a real collision failure
drifts, jitters or wedges against something, while a missing call leaves
positions PERFECTLY constant. Two sessions of diagnosis went past it because
the symptom read as "the sweep refuses to move them" rather than "nothing is
sweeping". Bulk `sed` over C++ is how it got there; see
[[pkre-budget-discipline]] rule 1.

## Two movement paths

`CActor:PO_Create` runs only `if self.CreatePO`, a per-template flag, and every
walking function branches on the result:

```lua
if not ENTITY.PO_Exist(self._Entity) then
    self.Pos.X = self.Pos.X + mvx        -- script-side, no engine call
    ...
else
    ENTITY.PO_Move(self._Entity, mvx * d, mvy * d, mvz * d)
end
```

`PO_Move` and `TickMonsters` serve the bodied path, which is nearly all of
them: **81 of 86 monster templates set `CreatePO = true`.** See "The 32
bodyless actors were bats" at the end - the split is far narrower than a first
count suggested.

## The chain, as measured

Reproduced with `PAINFUL_PLAYER_AT="-292,-2,-5"` (see Tooling), which puts the
player three units from a monk. Every stage below was read live out of the
running game by wrapping `Game_Tick` from the exec chunk:

| stage | state | how it was seen |
|---|---|---|
| sight | `see=true` | `SeesEntity` |
| enemy chosen | `brainDist=3.5` | `CAiBrain.r_closestEnemy` |
| walking | `walk=true` | `_isWalking` |
| turned to face | `dAng=0.00` | `_distToAngle` |
| movement curve | `curve=true` | `_HasMovingCurve` |
| animation | `anim=walk` | |
| root motion | `out 0.000 0.000 0.0196` | `GetAnimMovement` |
| `PO_Move` called | `-0.05, 0, -1.18` | the native itself |
| **position** | **walks and arrives** | `_groundx/y/z` |

Measured end to end with `Tools/spawn_test_monsters.lua`, which drops three
monks five units in front of the player: they fall to the floor (y -3.4 to
-5.09), close from z +2.09 to -4.00 on a player at z -2.9, then stop about a
unit away and go idle - which is arrival, and the switch into attack.

### The 60-degree gate

Worth knowing before touching any of this: `UpdateWalking` opens with

```lua
if math.abs(self._distToAngle) > 60 * 3.14/180 ... then
    ENTITY.PO_Move(self._Entity,0,0,0)
    return
end
```

An actor **turns first and walks second**, and while turning it explicitly
commands a stop. Anything that breaks the facing convention therefore presents
as "monsters never move" rather than as "monsters face the wrong way".

### Walking speed IS root motion

When `_HasMovingCurve` is set - true for `walk`, `run`, `atak1`, `atak2` on the
monks - `UpdateWalking` takes its step straight from
`MDL.GetAnimMovement`, rotates it by the actor's facing, and multiplies by
`1/delta` to make it a velocity. **A monster's speed is a property of its
animation, not a constant.** It is also damped hard as the actor closes on its
target, which is what produced the 0.46 units/second above.

## A latent bug found on the way

`SlideSphere` keeps a 0.02 skin off every surface and advances by
`length - skin`, so **a step shorter than the skin advances by nothing at
all**. The player never meets this - it moves 0.13 units a frame - but an actor
damps its speed as it closes on its target, and at 0.46 units/second a 60 Hz
step is 0.008 units.

`TickMonsters` accumulates the step and spends it once it clears the skin; the
distance is unchanged, it just arrives every third frame. This was NOT the
cause of the frozen monsters (that was the deleted sweep above) - it is a real
defect that would have bitten as soon as they started arriving anywhere.

## What is implemented

- `PO_Move`, `PO_SetMonsterType`, `PO_SetMonsterMovementConst`, `PO_IsOnFloor`,
  `PO_SetSightParams`, `SeesEntity` — all read out of Engine.dll.
- `GetAnimMovement` with the movement curve, and the curve's travel removed
  from the drawn pose.
- `PATH.Create` / `Release` / `GetShortest` / `IsFinished` / `GetNextPoint`.
  `IsFinished` answers **1**, which is what Engine.dll's own 0x1013AA20 returns
  for a null path, and in `CActor` that is the branch which walks straight at
  the destination rather than following waypoints.

## What is not

- **Flyers.** The bodyless actors are bats, and they move through
  `UpdateFlying` rather than `UpdateWalking`. A separate mode, untested.
- **The waypoint graph.** Every level ships one as a `.wps` beside its `.mpk`,
  and `PATH.GetShortest` is meant to search it. Without it actors walk straight
  lines and will hug corners they cannot see past. `WPT.GetClosest` /
  `GetPosition` are a separate, smaller thing used by four monsters
  (AlastorKing, Lucifer, StoneGolem, Apoc_zombie) for placement, not pathing.
- **`ERot`**, the movement curve's rotation channel.
- The engine's own rule for `BodyTypes.Fatter`, which lives inside
  `Entity::CreatePhysicsObject`.

## Tooling this needed

**`PAINFUL_PLAYER_AT="x,y,z"`** puts the player somewhere specific. Half of what
the AI does only happens near a player, so without it none of the above
reproduces outside a live session - and the position to stand at is exactly
what the HUD already prints. It is applied on the first `TickMonsters` rather
than at spawn, because the scripts place the player themselves during level
load and overwrite anything set earlier.

**Wrapping `Game_Tick` from the exec chunk** reads live script state headlessly:

```lua
local old = Game_Tick
Game_Tick = function(a,b,c,d)
    -- inspect Actors, Player, brains here
    return old(a,b,c,d)
end
```

That is how the table above was measured, and it beats adding C++ probes for
anything that lives on the script side.

## Order

1. **The `.wps` waypoint graph.** Walking monsters work; what they cannot do is
   round anything. `net == path` in the squad measurement is that limitation
   stated numerically, and it is the difference between monsters that chase and
   monsters that chase in open rooms only.
2. **Flyers**, through `UpdateFlying`. Bats, and the one other template that
   sets `CreatePO = false`.
3. **Sight from head positions**, matching `GetPawnHeadPos`, so low cover stops
   blocking sight the original sees over.
4. `ERot`, and the `Fatter` shape rule.

## Measured with a squad

`Tools/monster_stats.lua` spawns sixteen `EvilMonkV2` four deep in front of the
player and reports what they do. Cathedral, 1400 frames:

```
t 200 of 16 | saw 16, walked 16, moved 16, reached  4, STUCK 0 | net 4.9 path 4.9
t 400 of 16 | saw 16, walked 16, moved 16, reached 16, STUCK 0 | net 9.1 path 9.1
t1400 of 16 | saw 16, walked 16, moved 16, reached 16, STUCK 0 | net 8.8 path 9.6
```

All sixteen acquire the player, walk, arrive within three units by frame 400
(about seven seconds), and hold there attacking - nine `Client_OnDamage` events
land on the player over the run. Nothing is stuck.

**`net` equal to `path` is the absence of pathfinding, stated numerically.**
Straight-line distance equals distance walked, because there is no route to
follow. Once the `.wps` graph is in, `path` should exceed `net` wherever an
actor rounds something. The small divergence after arrival (8.8 against 9.6) is
the crowd jostling at the player, not navigation.

### Monsters were blinding each other

Tracing sight against physics bodies, only **9 of 16** ever saw the player -
the front rank, because each rank occluded the one behind it. Engine.dll's
`CalculatePawnToEntityVisibility` (0x10198D30) resolves visibility through
`World::FindZone`, the zone graph, after checking the range at
`PhysicsObject+0x24` and the pitch cone at `+0x30`. It is a question about
level geometry, not about what is standing in the way. Traced against the world
alone, all 16 see and arrive.

That change alone took the Cathedral report from 126 distinct unimplemented
natives to **135** - not a regression: monsters that actually reach the player
run combat code that had never executed.

### Known deviation

`CalculatePawnToEntityVisibility` takes both pawns' **head** positions
(`GetPawnHeadPos`). We trace between entity origins, which on these rigs is the
middle of the model. Low cover would therefore block sight here that the
original sees over.

## The 32 "bodyless" actors were bats

An earlier draft of this document called `CreatePO` a two-path split in the
movement system and made it the top open question. It is not: 81 of 86 monster
templates set `CreatePO = true`, and the 32 Cathedral actors without a physics
object are all `Bat_Adrian_*`, from the one template that sets it false. Bats
fly - `UpdateFlying`, not `UpdateWalking` - so they are a separate movement
mode rather than the same one taking a different path. Worth doing, much
smaller than feared.
