# Physics

PainEngine is a Havok game. `Engine.dll` exports a `PhysicsWorld` and a
`PhysicsObject` that are wrappers around it, the `PO_*` natives are how Lua
reaches them, and ragdolls, glass, explosions and player movement all go
through the same layer.

Jolt stands in for Havok here. This is the bring-up: the level's collidable
geometry, the props the level places, and the queries the rest of the engine
needs. It is not the whole system — see [what is missing](#what-is-missing).

## Where the numbers come from

Nothing here is tuned by feel.

`LScripts/Main/Tweak.lua` is the engine's own physics configuration.
`PhysicsEngine::GetTweaksFromScript` (`Engine.dll` 0x10185a80) reads that exact
file field by field into a flat struct, and `Tweak:Apply()` → `WORLD.ApplyTweaks()`
pushes it into the physics world. `Assets/Tweaks` reads the same file, so the
values are the game's:

| | |
|---|---|
| `GlobalData.Gravity` | `2 * 9.81` = **19.62** units/s² (multiplayer uses `3 * 9.81`) |
| `PlayerMove.PlayerSpeed` | 8.0 units/s |
| `PlayerMove.SlopeAngleToSlide` | 60° |
| `MultiPlayerMove.MaxStepHeight` | 0.7 |
| `MultiPlayerMove.Overbounce` | 1.01 |

The struct layout was recovered by pairing the string references in
`GetTweaksFromScript` with the offsets it writes, in order — so
`Tweak.GlobalData.Gravity` is the float at tweak+0x28, which is what
`PlayerAction` reads when it applies gravity to the player.

The level supplies the surface properties, in the same call the original makes.
`CLevel.lua` runs

```lua
WORLD.Init(p.ActiveMeshesMassScale, p.DefaultMeshFriction, p.DefaultMeshRestitution,
           p.Deactivator.Delay, p.Deactivator.MaxPosDiff)
```

right after `WORLD.LoadMap`, with `o.Physics` from the `.CLevel` over
`CLevel.lua`'s class defaults (friction 0.5, restitution 0.5). 22 levels declare
`DefaultMeshFriction`; Cathedral's is 0.7.

## The static world

One Jolt `MeshShape` body, built from the same object set the original hands
Havok: `MapObject::isCollidable` rejects portals, zones, volumetric-light
helpers and anything named `noclip`. Triangles are in rendered space — raw mesh
coordinates times the level `o.Scale` — which is the space entity positions and
the camera already live in. Cathedral is 282,772 triangles.

**The triangles are wound backwards on the way in, on purpose.** Jolt treats
counter-clockwise as the front face; the world exporter winds the other way,
which is why the renderer draws `.mpk` meshes with `CULL_CCW`. Feeding them in
as authored gives every floor a face that points *down*, and the symptom is
nasty: shape *queries* still work, because a query can be told to collide with
back faces, so the camera collides correctly and a downward probe finds the
floor — while every simulated body falls straight through the level. Measured
before and after, on the same 135 Cathedral props woken for five seconds:

| | fell more than 1 unit |
|---|---|
| as authored | 129 of 135 |
| wound backwards | 81 of 135 |

(That 81 came down again to 41 once the prop shapes were built the way the
renderer builds the models — see below.)

## The props

A placed entity's physics is not in its property file. It is in script — either
the `o.StartCommand` the instance carries, or the Lua file beside its template,
which the engine runs when the entity is created. Cathedral's barrels have both:

```
Levels/C1L1_Cathedral/CItem/BarrelBig_001.CItem
                                    o.StartCommand = "o:PO_Create(BodyTypes.FromMesh)"
                                    o.BaseObj = "BarrelBig.CItem"
```

```
Templates/Items/BarrelBig.CItem     BarrelBig.Mass = 200, Friction = 1, Scale = 0.5
Templates/Items/BarrelBig.lua       function BarrelBig:OnCreateEntity()
                                        self:PO_Create(BodyTypes.FromMesh)
                                    end
```

Without a script host that call cannot be *run*, but it can be *read*, and 128
templates make it. The instance's own `StartCommand` wins; otherwise
`TemplateCache::PhysicsBodyType` walks the `BaseObj` chain looking for it. Both
return the `BodyTypes` value from `Definitions.lua`; the counts across the
shipped templates are

```
98  FromMesh      14  Simple        9  FromMeshNotCentered
 6  SphereSweep    5  Sphere        2  FromRagdoll   2  FromMeshNonConvex
```

Each such entity becomes a body:

- **shape** — `FromMesh` and its variants become a convex hull of the entity's
  own mesh, read from the `.dat` pack or the `.pkmdl` the template names. Jolt
  computes the centre of mass itself, which is the distinction the original
  draws between `FromMesh` and `FromMeshNotCentered`. `Simple` / `Sphere`
  become a sphere around the mesh.

  The shape has to be built **exactly** the way the renderer builds the model,
  or it is somewhere the player cannot see it. Two details matter: scale follows
  the same rule (pack meshes take `o.Scale` plainly, models take `Scale * 0.1`),
  and pack vertices are taken **raw, without the pack object's own transform** —
  which the renderer also ignores. Applying it put barrel hulls several units
  from their barrels, and only the hull view made that visible.
- **mass, friction, restitution, pinned** — from the property chain, which is
  what `CObject:PO_Create` reads off the object before handing it to the
  engine, and *only* what it declares:

  ```lua
  if self.Restitution then ENTITY.PO_SetRestitution(self._Entity, self.Restitution) end
  if self._Class == "CItem" then ENTITY.PO_SetFriction(self._Entity, 1) end
  if self.Friction then ENTITY.PO_SetFriction(self._Entity, self.Friction) end
  ```

  The level's `DefaultMeshRestitution` is the **world mesh's** surface, not
  every prop's; handing it to the props at 0.5 made them bounce when they
  landed. `Pinned` makes it a static body.
- **placed awake** at the authored transform, and then given a second and a
  half of simulation *before the level is first drawn*. They are authored
  hanging in the air (below), so without that the level visibly rains its own
  furniture into place on load — and a `--shot` of frame 30 would catch it
  mid-fall, differently every run. A fixed number of fixed steps keeps captures
  reproducible. Jolt sleeps each body as soon as it stops: after five seconds
  Cathedral has 4 of 135 still awake, Train Station 0 of 110.

  Whatever physics moves is drawn where it moved to
  (`EntityRenderer::SetEntityPose`). The per-frame sync reports only *awake*
  bodies, so the level load also does one full pass over every prop — settled
  means asleep, and a sync that skips sleeping bodies would draw the whole
  level back where it was authored.

Cathedral places 135, Train Station 110, Cemetery 75, and the deathmatch maps
none. Nothing is left unresolved on any of them.

`CActor` is deliberately excluded. `CActor:PO_Create` runs when a monster is
*spawned*, and spawning is script work that does not exist yet — the statically
placed actors are a dormant pool the real game never draws in place.

## The camera

The free camera is still a free camera. It does not walk and it has no gravity —
it just stops passing through walls, and pushes what it runs into.

Those are two different mechanisms, and it needs both:

- **Stopping** is a query. The move it always made is cast as a sphere and slid
  along whatever it hits, up to three planes so a corner resolves.
- **Pushing** is a body. A query touches nothing, so the camera could press
  into a barrel and the barrel would never know; a kinematic sphere follows the
  camera and does the shoving. Kinematic, so nothing pushes back and the camera
  keeps flying exactly as before. It teleports rather than sweeps across a level
  change or while noclipping, so it does not rake a level on the way through.

The body is **driven from inside the fixed step, not from the frame**. A
kinematic body moves by having a velocity during a simulation step, and the
steps are a fixed 1/60 that has nothing to do with how often a frame is drawn.
Aiming it with the frame's own delta made it move a different distance from the
one it was given — at 120 fps every other frame ran no step at all and the one
after moved twice as far — so it lagged behind the camera or overshot whatever
it should have pushed, and a push landed or missed depending on the frame rate.
It also **sweeps rather than steps** (`EMotionQuality::LinearCast`): at shift
speed the camera crosses 2 units per step against a body 1.2 wide, and a
stepped body would jump clean over a barrel without touching it.

**What stops the camera is decided by mass.**
`Tweak.PlayerMove.MaximalItemPushMass` is 2500, the line the engine draws
between what the player walks through and what stops it. Without it the
camera's own query treats a 200 kg barrel as a wall: it halts an inch short and
never presses into it, so the body that does the pushing barely moves and the
push feels feeble. The world and anything `Pinned` always block.

Having both means the camera's queries **must ignore the camera's own body**.
It sits exactly where the camera is, so a cast starting there hits it at zero
distance in every direction and the camera cannot move at all - which reads as
being permanently wedged in the world rather than as colliding with itself.

Every move also **depenetrates first**. A cast that starts inside geometry
reports a hit at zero distance whichever way it goes, so a camera that ends up
inside something can never cast its way out. It resolves **one overlap per
pass, the deepest, then looks again** — applying every overlap in a pass is the
obvious thing to write and badly wrong, because a floor is hundreds of
triangles and a sphere resting an inch into one overlaps a dozen of them and
gets pushed out a dozen times over.

The sphere is radius **1.2**. The player body's own widest sphere is 0.4 —
`EngineGame::CreatePlayer` asks for `BodyTypes.Player` at bodyScale 1.0, and the
shape factory (`Engine.dll` 0x101b3e20) builds that body as a stack of four
spheres in units of 0.2 × bodyScale:

| centre y | radius |
|---|---|
| −0.63 | 0.33 |
| −0.10 | 0.40 |
| +0.50 | 0.40 |
| +0.90 | 0.20 |

`GetPawnHeadPos` and `GetPawnFloorPos` confirm the extent — head = centre + 0.9,
floor = centre − 1.1 — so the player is two units tall, which puts the engine's
unit at about a metre. The camera is deliberately three times fatter than that:
it has no body to watch clipping into a wall, and at player width it slides
close enough to surfaces for the near plane to cut through them.

`N` toggles noclip, and `--noclip` starts that way. `--shot` is unaffected: a
still camera never moves, so nothing is cast.

## Looking at it

`P` toggles the collision wireframe (`--physdebug` starts with it on). The
physics world is the one part of the engine with no picture of its own — the
renderer draws a barrel from its model, the simulation collides against a hull
built from that model, and nothing says the two agree. This says it:

| | |
|---|---|
| blue | the static world, within 20 units of the camera |
| yellow | a prop that is asleep |
| green | a prop that is awake |

It draws with no depth test at all. A collision hull sits exactly on the surface
the renderer already drew, so depth testing rejects nearly every segment of it,
which looks precisely like the feature not working.

Two bugs surfaced within a minute of it working, neither of which any amount of
reading would have found:

- **Jolt's `GetTriangles` only works on leaf shapes.** A prop is a `ScaledShape`
  around a hull, whose `GetTrianglesNext` returns zero — and with asserts
  compiled out it says nothing about why. The leaves have to be collected first
  (`CollectTransformedShapes`), which is what Jolt's own assert message asks
  for.
- **The pack object transform**, above: hulls sitting several units from their
  models.

## Checking it without a window

Two ways, and they answer different questions.

**`PainfulTools lua <DataRoot> [frames] [level] [chunk]`** is the whole game
minus the renderer: a real `PhysicsWorld`, the same `Settle(90)`, and the same
per-frame tail the game loop runs — animations, monsters, projectiles, the
script layer, the physics step, ragdolls, the pawn probe, triggers, lifetimes,
attachments and contacts. The optional last argument is a Lua chunk, which is
how a probe gets in.

**It is deterministic, and the windowed path is not.** Fixed 1/60 steps and no
frame-rate coupling mean impact speeds, health and positions come back
bit-identical between runs. Two identical windowed captures of the same level
differ by 20–32% of sampled pixels because the props settle differently every
time, so comparing screenshots proves nothing about physics. Compare numbers
here instead:

```
$ PainfulTools lua <DataRoot> 200 TestFloor "<probe>"
HEADLESS F5   vase present hp=2
HEADLESS impact vl=31.056878731571 hp=2
HEADLESS F150 vase absent
```

That is a vase dropped 24 units, breaking on landing — the same `vl` the
windowed run reports, to the last digit, and the same on every run.

Whatever the game loop ticks, this must tick too. It drifted once:
`UpdateAttached`, `TickSounds` and `TickCollisions` were added to the game and
not here, so bound entities never followed their parents and **contacts were
never reported** — a destructible could not break headlessly even though the
physics under it was real. If a gameplay system works in the game and not here,
suspect the two loops disagreeing before suspecting the system.

What it cannot answer: anything about drawing. No renderer is attached, so the
beam, mesh visibility and billboard fades still need the game. No audio device
either, so `TickSounds` is a no-op.

**The physics report** is the other half: a static look at one level, with no
scripts running.

```
PainfulTools physics <levelDir> <DataRoot>
```

It prints the tweak values it read, what went into the world, whether a sphere
pushed 50 units along each axis from the spawn is blocked, how much wireframe
each half of the world produces, what the props do over five seconds, and
whether driving the camera's body through one moves it.

That last check runs the same traverse at 120, 60 and 30 fps and once at shift
speed, because the bug it exists for was invisible at any single rate: the
push landed or missed depending on the frame rate. The four numbers should
agree.

The settling is the honest measure of how far this has to go:

| | props moved > 1 unit in 5 s | still awake |
|---|---|---|
| Train Station | 0 of 110 | 0 |
| Cemetery | 1 of 75 | 0 |
| Cathedral | 42 of 135 | 4 |

Cathedral's are almost all one thing: **32 barrels, each settling a
repeatable 1.08 units downward**. That is not the physics being wrong - the
barrel's mesh sits 0.82 above its own origin, the entity is authored 0.26 above
the floor, and 0.82 + 0.26 is exactly 1.08. The barrels were being *drawn*
hanging in the air that whole time, and the simulation is what put them on the
floor. Whether the original centres a pack mesh on the entity origin (the
distinction its `FromMesh` / `FromMeshNotCentered` body types draw) is the
thread to pull next; until then the renderer is the one placing them wrongly,
and physics quietly corrects it on load.

## A body handle can go stale, and the scripts check

`PHYSICS.IsHavokBodyInWorld(body)` (`0x101297d0`) takes one handle and pushes a
bool. It exists because a body can be gone by the time a hit is resolved — the
thing gibbed, and `Ragdoll.Remove` took it — so a handle a trace reported an
instant ago may name nothing.

`PainHead:Tick` guards on it before every impulse:

```lua
if not PHYSICS.IsHavokBodyInWorld(he) then he = nil end
```

Leaving it unimplemented was worse than leaving it out. The call returned nil,
`not nil` is true, and `he` was cleared on **every** hit — so every
`WORLD.HitPhysicObject` below that line got nothing. The alt fire moved debris,
which is spawned fresh and shoved through another path, but never an intact
prop. **A native whose absence inverts a script's test is worse than one that
does nothing**, and this is the third of that species: `GetChildByName` and
`KillAllChildrenByName`'s return value were the other two.

Both handle kinds a trace can report have to answer here: the script body slot,
and the encoded limb handle a hit on a monster's bone reports.

## Contacts, and how a destructible breaks

Nothing damages a crate to destroy it. `CItem:Apply` installs `StdOnCollision`
on anything carrying `Destroy.MinSpeedOnCollision`, and that handler compares
the **impact speed** against it:

```lua
vl = vl * INP.GetTimeMultiplier()
if vl >= self.Destroy.MinSpeedOnCollision then
    self:OnDamage(vl * 0.3, self, AttackTypes.ItemCollision)
```

So a vase (Health 2, MinSpeed 10) shatters on any qualifying impact, while
BarrelBig (Health 40, MinSpeed 18) survives a long fall and needs roughly 133
units/s to go in one hit. It is also why the shotgun's freeze bolt breaks
crates while correctly imparting no impulse: it breaks them by *arriving fast*,
not by dealing damage.

The engine reports a contact as

```
Game_GetMsg('COLLISION_WITH_OTHER_ENTITY',
            e_me, x,y,z, nx,ny,nz, e_other, h_me, h_other)
```

and the script fills the rest in itself — `Game_GetMsg` reads both bodies back
through `PHYSICS.GetHavokBodyVelocity` and stores the relative speed as
`arg[14]`, which is the `vl` above. That is why the message carries body
handles as well as entities: without them the handler has nothing to measure.
`Game:ExecMsgQueue`, at the end of `Game:Tick2`, is what drains it.

Four things about this were easy to get wrong, and three of them were:

- **Sample the velocities inside the contact callback, mid-step.** Read after
  the step, both bodies have already been stopped by the solver and every crash
  measures as a nudge. `PHYSICS.GetHavokBodyVelocity` answers from that
  snapshot while the scripts are handling the messages, and from the live body
  otherwise.
- **One side being a script body is enough.** Requiring both hid the common
  case: almost everything a prop hits is the *static world* — a vase pushed off
  a balcony lands on the floor, not on another prop, and that is exactly the
  collision it is supposed to break on. The world reports as entity 0, the same
  stand-in a world hit already uses in the traces.
- **A capped contact buffer must drop the GENTLEST contact, not the newest.**
  `Settle()` runs 90 steps with no drain between them, so a level's whole load
  settle arrives in one batch; discarding by arrival threw away a vase's landing
  while two dozen other props were settling, and the same landing measured
  correctly with the level otherwise empty.
- Only `OnContactAdded` is recorded. Persisted contacts would report every frame
  for a body merely resting on another; the scripts' own `MinTime` gate
  (`EnableCollisions`) handles what chatter remains.

`ENTITY.EnableCollisions(e, on, minTime = 0.4, minStrength = 0.6)`
(`0x10130420`) is what asks for any of this. For a destructible `CItem:Apply`
passes `(true, 0.5, MinSpeedOnCollision * 0.2)` when the item has an impact
sound, so the engine's gate sits well below the script's and the script decides.


## Explosions

Every explosion in the game funnels through one native. `Explosion()` in
`Main/Utils.lua` is the single entry (91 call sites) and it calls
`WORLD.Explosion2` in single player, `MultiplayerExplosion` otherwise - so
grenades, rockets, barrels, the exploding cars and the bosses' shockwaves all
land in the same place.

**The engine does not deal the damage.** It collects what the blast reached and
posts one `EXPLOSION` message per entity; `Game_GetMsg` (`Game.lua:1249`) looks
the entity up in `EntityToObject` and calls `obj:OnDamage`. The same division
the weapon traces use.

```
Game_GetMsg('EXPLOSION', entity, x, y, z, explosionId, killer, attackType, damage)
```

`explosionId` is the dedupe key: the handler stores it as `obj._Exploded` and
skips an entity it has already seen, so every entity in one blast must share an
id and two blasts must not. `killer` is an entity handle in single player and a
client id in multiplayer - the native passes argument 6 through untouched.
`x, y, z` is the blast CENTRE, which is what `OnDamage` takes at parameters
4..6 and what the actors fall away from.

### The falloff is a SINE, not a ramp

From `FUN_101B79F0`, which `PhysicsWorld::Explosion` (0x1019BBD0) forwards to:

```
f = sin((1 - distance / range) * pi/2)
```

1.0 at the centre, 0.0 at the rim, and it holds its strength further out than a
linear ramp. The multiplier is the float at `0x102C86E4` = 1.5707964.

**`PhysicsWorld::SelfExplosion` (0x10197D10) uses a different law** - plain
`(1 - d/range)`, with a 0.001 minimum distance (the double at `0x102AE578`).
Two functions, two curves; do not carry one over to the other.

Verified against damage taken, range 8, damage 100:

| distance | measured | `100 * sin((1-d/8) * pi/2)` |
|---:|---:|---:|
| 0.001 | 100.000 | 100.000 |
| 2.000 | 92.388 | 92.388 |
| 4.000 | 70.711 | 70.711 |
| 6.000 | 38.268 | 38.268 |
| 7.900 | 1.963 | 1.963 |

The same `f` scales the damage carried in the message. That much is inference:
the recorder is a `DynamicArray` push of an 8-byte pair (entity + one float,
`FUN_100F97E0`), and the decompiler loses the value because it lives on the FPU
stack. Reading the disassembly at the `fsin` site would settle it.

### Constants

| address | value | what |
|---|---:|---|
| `0x102C86E4` | 1.5707964 | the pi/2 in the falloff |
| `0x102AE5A4` | 1.0 | the 1 in `1 - d/range` |
| `0x102AE5B0` | 0.5 | bbox midpoint, for the groups measured from bounds |
| `0x102AEEF0` | 3.0 | closest-point search radius, as a multiple of range |
| `0x103E618C` | 0.6 | world-mesh range multiplier |
| `0x102AE578` | 0.001 | `SelfExplosion`'s minimum distance |
| `0x102C8C58` | 0.0001 | near-zero distance; takes the ragdoll self-explosion path |


### Wreckage inherits the item's velocity

`ENTITY.ExplodeItem` gives every part the SOURCE ENTITY'S velocity on top of its
own outward spread. That is what makes debris carry the blast that broke it, or
the momentum of a fall, instead of dropping straight down where the item stood.

`CItem:DestroyItemFX` is the evidence:

```lua
if dInfo.VelocityFactor then
    local vx,vy,vz = ENTITY.GetVelocity(entity)
    ENTITY.SetVelocity(entity, vx*VF.X, vy*VF.Y, vz*VF.Z)
end
ENTITY.PO_Enable(entity,false)
ENTITY.ExplodeItem(entity, ...)
```

The script reads the velocity, scales it and writes it back immediately before
the call. Only two templates in the whole game set `Destroy.VelocityFactor` -
`Stake.CItem` and `BoltStick.CItem`, both to `(0,0,0)` - so the factor is an
opt-OUT for things that must not fling their parts along their flight path, and
inheriting is the default.

**The velocity has to be snapshotted when the body is disabled**, because
`DestroyItemFX` disables it *before* exploding it and a body out of the world
reports zero. `ENTITY.PO_Enable(false)` takes that snapshot into the entity
store, and `ExplodeItem` prefers the store over the body once the body is off.

**On the transition only.** `DestroyItemFX` disables the body TWICE; the second
call reads the already-disabled body as zero, and snapshotting on every call
clobbered the good value. Measured on a Cathedral `BarrelBig` hit by a
3200-strength blast from -X, mean part velocity along X:

| | mean vx | min vx |
|---|---:|---:|
| no inheritance | +0.296 | -8.883 |
| snapshot clobbered by the second call | +0.296 | -8.883 |
| on the transition | **+5.078** | -2.996 |

5.878 is what the barrel had when it broke, so the parts leave with the barrel's
own motion and none of them flies back into the blast.

### Pinned bodies

`ENTITY.PO_SetPinned` makes a script body STATIC, and releasing it makes it
dynamic again with its velocity cleared - a pinned body has been standing still
by definition, and letting it resume whatever it was frozen with launches it. A
monster's body is kinematic and carried by its own mover, so pinning skips it.

`CObject:PO_Create` pins anything whose template says `Pinned`, and marks the
call `-- bug havoka`: the original is working around Havok drifting a heavy
resting body.

**This is what makes the Catacombs blockade work.** `C1L3_Blokada.CItem` is
`Mass 10000, Health 1, Pinned = true, Immortal = true`, and
`AmbushForPlayer_005.CBox` carries

```
o.Actions.OnTouch[1] = "Pin:C1L3_Blokada_001,false"
o.Actions.OnTouch[2] = "SetImmortal:C1L3_Blokada_001,false"
```

for each of the eight stones. They are scenery until the player walks into the
box, and only then can the dynamite crates break them. At 10000 kg no blast
moves them by impulse - being released and destroyed is the whole mechanism.
Measured: blasted while pinned and immortal, moved 0.0000 and kept Health 1;
released and mortal, the same blast takes it to -172 and kills it.

### Strength is taken as an IMPULSE, and that is the tuning knob

The engine accumulates into `PhysicsObject::EffectForce` and spends the total
once per step in `EffectForces()`, which reads as Havok's `applyForce` - the
body would gain `force * dt`, not `force`. Measured both ways on a Cathedral
`BarrelBig` (declared mass 200) with a shipped 3200-strength blast at 1.5 units:

| reading | barrel travel |
|---|---:|
| `strength * dt` | 0.000 - nothing moves at all |
| `strength` as an impulse | 4.012 |

So the port takes it raw, which also matches how `PO_Hit` and
`WORLD.HitPhysicObject` already treat the numbers the scripts hand them. **This
is assumed, not recovered**, and it is the first number to retune if blasts feel
wrong. An exceptional 15000-strength car blast throws the same barrel 39 units,
which is probably too far.

`ENTITY.PO_SetMovedByExplosions` gates the impulse and **not** the damage -
which is why a grenade turns it off for itself and still hurts what it lands
on. Measured: 4.309 with it on, 0.000 with it off.

### Not ported

- **`WorldMesh::GetClosestPoint`.** The original measures a fixed mesh from its
  nearest SURFACE, not its origin, for anything within `3.0 * range`, and runs a
  second pass over world meshes at `0.6 * range`. A large static mesh therefore
  takes a blast it would otherwise be too far from. Ours measures from the body.
- **`WORLD.ExplosionUp`** `(x,y,z, stren, distance, stren, random)` and
  **`ExplosionParabolic`** `(x,y,z, flightTime, radius, targetX, targetY,
  targetZ)`. Boss moves - Thor's hammer and fists, the Panzer Demon's shockwave
  - and two of the three call sites are commented out in the shipped scripts.
- The ragdoll branch: at `distance <= 0.0001`, or for shape type 4, the original
  takes `Ragdoll::SelfExplosion` (0x1019CC40) instead of the plain force.


## Active meshes: world geometry that is a rigid body

**Not implemented.** This is why the heavy stones at the mouth of the Catacombs
do not move when a crate goes off beside them.

Some world-mesh objects are not scenery. The `.mpk` encodes the intent in the
OBJECT NAME, and the engine promotes those objects out of the static world into
rigid bodies at load:

| in the name | meaning |
|---|---|
| `phys_` | this object is a body, not static world |
| `pinned_` | it starts pinned (static until an action releases it) |
| `_actgrpNN` | it belongs to active mesh group NN |

Catacombs' entrance stones are `pinned_phys_wejsciowy_kamienshape` through
`...shape32` - **31 objects**, pinned, and grouped by name rather than by
suffix. Its columns are `phys_kolumna_wielka_actgrp24_1` and friends.

An object with no `_actgrpNN` gets its group from the level, through a global
the ENGINE calls (nothing in the shipped Lua calls it):

```lua
function Level_GetActiveMeshesData(mesh)          -- CLevel.lua:970
    if not Lev.ActiveMeshesData then return 1 end
    for i,o in Lev.ActiveMeshesData do
        if string.find(mesh,i,1,true) then return o[1] end
    end
    return 1
end
```

and Catacombs' `.CLevel` declares

```
o.ActiveMeshesData.kolumna[1] = 2
o.ActiveMeshesData.wejsciowy_kamien[1] = 10
```

so the entrance stones are group 10 and the columns group 2. Substring match on
the object name, lowercased, defaulting to group 1.

`CLevel:SetupMap` then configures the groups it cares about:

```lua
for i=20,30 do
    ENTITY.EnableCollisionsToAll(true, ..., i)
    PHYSICS.ActiveMeshGroupSetActivationParams(i, true, ...)
end
```

which is why every `_actgrpNN` in the shipped maps falls in 20..30.

How much of each level this covers:

| map | `phys_` objects | groups |
|---|---:|---|
| `1x03_Catacombs` | 452 | 20, 24, 29 (+ named 2, 10) |
| `2x02_Prison` | 32 | 20, 21, 25, 26, 27, 28, 30 |
| `3x02_Factory` | 7 | 22, 27 |
| `1x01_Chaos`, `1x02_Atrium`, `5x01_CityOnWater` | 0 | — |

`WORLD.Init` already takes `ActiveMeshesMassScale` and the deactivator settings,
and `SetScriptBodyMass` already applies that scale - the constants arrived
before the system did.

**What porting it needs**, in order of difficulty:

1. Split the named objects out of the one static collidable body and give each a
   dynamic convex-hull body, the way `CreateScriptBody` does for a `.dat` part.
2. Draw them where they moved to. This is the hard part: world geometry is baked
   into static vertex buffers, and an active mesh needs a per-object transform.
3. The group natives - `PHYSICS.ActiveMeshGroupEnable` / `Activate` /
   `SetActivationParams` / `StaticMeshEnable`, `GetHavokBodyActiveGroup`,
   `SetMaxRecursiveActivationDistance`, `WORLD.SetCollisionGroupMeshGroup` /
   `EnableDrawMeshGroup` / `SetTimeToDeleteMeshGroup` - plus the recursive
   activation the name suggests: disturbing one member wakes its neighbours
   within a distance, which is what makes a stack of stones collapse rather than
   one stone popping out of it.

## What is missing

Since this list was written the player controller and ragdolls have both
landed — see [`PlayerMovement.md`](PlayerMovement.md) and
[`Hitboxes.md`](Hitboxes.md). What is left:

- **A few props leave the level.** One Cathedral barrel travels 27 units, and
  seven vases drift. Those are individual shapes or placements, not the
  systematic 1.08 above.
- **The renderer and the physics world resolve entities differently.** Cathedral
  gives physics 135 props with nothing unresolved, while the renderer places 218
  models and leaves 420 entities unresolved. Most of that gap is entities that
  legitimately have no model, but the hull view also shows at least one hull
  with nothing drawn at it, so the two disagree somewhere real.
- **No glass, no water buoyancy, no ladders, no ice.** Each is a named piece of
  the original: `Glass` and `Tweak.Glass`, `EnableUnderwaterWorld` and
  `Tweak.Underwater`, `World::NearLadder`, `World::OnIce`.
- **Explosions land, with parts missing.** `WORLD.Explosion2` damages and
  shoves; the closest-point pass, `ExplosionUp` / `ExplosionParabolic` and
  the ragdoll self-explosion branch are not ported. Listed under Explosions
  above.
- **Monster ground contact is still wrong in three ways** — no step-up, a
  hardcoded floor normal, and a sweep shape that is not the collision shape.
  Listed with the evidence in [`../Plan.md`](../Plan.md); the rig
  measurements are in [`MonsterMovement.md`](MonsterMovement.md).
- **Corpses cannot be pinned.** `ENTITY.PO_SetPinned` works on props (see
  Pinned bodies above), but `PHYSICS.PinHavokBody` and the `MDL.SetPinned*`
  family are still stubs, so the stakegun cannot pin a body to a wall.
- **`World/CollisionMesh` still exists** and still answers the corona
  line-of-sight trace. Jolt can answer the same query; the BVH stays until
  there is a reason to move it.
