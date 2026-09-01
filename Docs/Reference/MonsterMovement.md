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
- `WPT.Load`, and `PATH.Create` / `Release` / `GetShortest` / `IsFinished` /
  `GetNextPoint` over a real `.wps` graph. A level with no graph, or an actor
  standing further from a waypoint than its template's `WPmaxDist`, gets an
  empty path - which `IsFinished` reports as finished, exactly as Engine.dll's
  own 0x1013AA20 does for a null path, and which `CActor` reads as "walk
  straight at the destination".

## What is not

- **Flyers.** The bodyless actors are bats, and they move through
  `UpdateFlying` rather than `UpdateWalking`. A separate mode, untested.
- **`WPT.GetClosest` / `GetPosition`** - a separate, smaller thing from the
  routing graph, used by four monsters (AlastorKing, Lucifer, StoneGolem,
  Apoc_zombie) to place themselves rather than to navigate.
- **The floors section** of a `.wps`, and with it `Select_OnSelectedFloors`.
  Routing does not need it.
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

1. ~~**The `.wps` waypoint graph.**~~ **Done** - see the end of this document.
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

## The waypoint graph

`.wps` is decoded and routed. The format, confirmed against **all 29 shipped
sets, byte for byte**:

```
u32  count
count x { f32 x,y,z;  u24 floor;  u32 linkStart;  u32 linkCount }   23 bytes
u32  linkTotal            repeats the sum of every linkCount
f32  cost[linkTotal]      TWO PARALLEL ARRAYS, not interleaved records -
u32  index[linkTotal]     every cost, then every neighbour
...                       the floors section, which routing does not need
```

Two things made this readable. The adjacency is **compressed sparse row**:
`linkStart[i] + linkCount[i]` is exactly `linkStart[i+1]`, and checking that
invariant on load is what confirms the 23-byte stride - a wrong stride fails on
the second record rather than silently producing a graph of noise. And the link
section is two parallel arrays, which is why it reads as a wall of floats if
you assume interleaved records: the entire first half is distances.

The three bytes after the position are a **floor index**, not flags: the
Cathedral uses 0..52 and its floors section opens with 53.

`painful wps <file>` reports the lot, including connectivity. Cathedral: 8829
waypoints, 130886 links, mean 14.8 each, none isolated.

### Connectivity, and a test that was wrong

Routing between the two corners of a set's bounding box reports "no path" on 17
of 29 levels - and that is the test being wrong, not the graph. A corner is
exactly where a sealed pocket or an out-of-bounds marker sits. Measured
properly, by components:

| level | largest component | route bend |
|---|---:|---:|
| 1x04_Cemetery, 2x02_Prison, 5x04_Hell | 100% | x1.02 - x1.60 |
| 1x01_Chaos | 84% | x1.02 |
| 1x03_Catacombs | 70% | **x2.49** |
| 5x02_Docks | 15% | x1.07 |

Every level routes within its largest component. Catacombs bending by 2.49 is a
maze behaving like one.

**Only 29 of 85 maps carry a `.wps`.** A level without one is not a fault - its
actors walk straight at their target, exactly as they did before any of this -
so a missing file is reported as ordinary and a malformed one is not.

### What it changed

Same squad of sixteen, Cathedral:

```
before   t1400 | reached 16, STUCK 0 | net 8.8  path  9.6
after    t1400 | reached 16, STUCK 0 | net 9.2  path 22.6
```

`path` pulling away from `net` is actors following routes instead of beelines -
the number this document predicted would move, and the reason it was the top
item. All sixteen still arrive; nothing is stuck.

## Five things reported from play

**The player walked through monsters.** The body was being placed at the
entity's position - the model's CENTRE - while the collision sphere it stands
for sits about a unit lower, on the soles. The two never overlapped: the
monster's body floated at chest height while the player's sphere swept the
floor. It now goes where the sphere is. Visible in the squad numbers
immediately: `reached` drops from 16 to 12 and one actor ends up stuck,
because sixteen monsters can no longer occupy the same spot.

**A standing jump launched sideways.** `airDir_` was only updated while a
movement key was held, so it kept the last direction walked - and the air
branch falls back to it when the takeoff mask is empty. Press nothing but
jump and you sailed off at full walking speed the way you last went, possibly
seconds earlier. It now tracks what the player is actually doing, zero
included.

**The head bob only started after taking damage, then never stopped.** CPlayer
decides it is `_Walking` from `ENTITY.GetVelocity(Player._Entity) > 2`, and the
player has no script body - it is the pawn - so that read the ENTITY velocity
store, which nothing ever wrote. The first knockback wrote one, and nothing
cleared it, so the bob switched from permanently off to permanently on. The
pawn now reports its real velocity, which is also what the footstep sounds are
waiting on.

**Some monsters still walk in place.** Not closed. Part of it is now honest -
a crowd blocks itself, and one stuck actor out of sixteen is a queue rather
than a bug. What is worth checking first is that `GetShortest` snaps to the
nearest waypoint by 3D distance, floor index ignored: an actor can bind to a
waypoint on the floor above or below and be handed a route it cannot walk. The
`floor` field exists precisely to disambiguate that, and is currently parsed
and unused.

**A stationary monster sank and stayed sunk.** `TickMonsters` accumulates a
sub-skin step rather than sweeping it, and the skip path returned before
`SlideSphere` - which is the only thing that calls `Depenetrate`. An actor with
`onFloor` latched true has `fallSpeed` 0, so its residual never grows, so it
never sweeps again: anything that put it inside geometry left it there for the
rest of the level. The skip path now sweeps a ZERO delta, which depenetrates and
advances nothing.

Adopting that result unconditionally trades the bug for its mirror image. A
sphere at rest reports a hairline overlap every frame, and taking it walked a
standing actor upward 0.0007 a frame - 0.19 over 250 frames. The correction is
only adopted when it exceeds the 0.05 sweep skin, i.e. when it is a real
extraction rather than resting contact.

Measured on Cathedral, `EvilMonkV2_WalkOnlyNoThrow_001` pinned stationary with
`PO_Move(e,0,0,0)` and pushed 0.15 into the floor at frame 50:

| | frame 51 | frame 300 |
|---|---|---|
| before | 0.000 of 0.15 recovered | 0.000 - stuck |
| after | 0.150 recovered | 0.150, stable to 4 dp |

---

# How the movement was recovered

Moved here from the gameplay roadmap: these are findings about the mover,
not plans. Each one cost a wrong hypothesis first, which is why they are
written down.
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
