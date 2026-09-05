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

  **Friction and restitution set by the scripts never reach the solver.**
  `PhysicsObject::SetFriction` (0x10189c80) and `SetRestitution` (0x10189ca0)
  write the engine's body wrapper (+0x38, +0x3c) and return; nothing copies
  those into the Havok body afterwards. The only reader that hands them to
  Havok is `FixHavokPositionBug` (0x1018d120), which rebuilds a body from a
  saved record — and `PositionBugFixed` (0x101903a0) only calls it when the
  body's position has gone NaN. Everything else that reads the wrapper values
  is engine-side flight code: `FixGrenadeFlight` (friction, below) and the
  multiplayer missile controller. So the contact material every script body
  has is what the sizer (0x101b3e20) puts in its `hkpRigidBodyCinfo`:
  **friction 0.5** (the Havok default the sizer leaves alone), **restitution
  0.9** (`0x3f666666` into the cinfo's restitution slot), linear and angular
  damping 0. The world mesh does take the level's `DefaultMeshFriction` /
  `DefaultMeshRestitution`, passed into its own creation. The 67 templates
  that declare `Restitution` and the 64 that declare `Friction` are tuning a
  value the solver never sees — `Ball.CItem`'s `Restitution = 5` included.
  The port keeps the two on the entity (`bodyFriction`, `bodyRestitution`)
  for the flight code and gives every script body the cinfo material.

  **Material combine is the geometric mean.** Havok's `hkpMaterial` combines
  both friction and restitution as `sqrt(a*b)`; Jolt's default takes the
  MAX restitution, which made every prop bounce off the 0.5 floor. The port
  installs the geometric mean for restitution (friction already is). This is
  Havok's documented default, not a decompiled fact — Havok is statically
  linked without symbols.

  **Mass and the freedom of rotation.** `PhysicsObject::SetFreedomOfRotation`
  (0x10189a30) is an inertia tensor: a locked axis is `3.4e38`, a free single
  axis (modes 1, 5, 6 — Y, X, Z) has inertia 10, `HardTurn` (4) is isotropic
  `softness * 10`, and `AllAxes` / `FullFree` (2, 3) take the shape's own
  inertia. Every body is created in mode 1 (Y only); `CObject:PO_Create` then
  sets 4 when the object declares `Softness` and 3 otherwise.
  `PhysicsObject::SetMass` (0x10189510) rescales the inertia with the mass
  only in modes 2 and 3; in every other mode it sets the mass alone. A
  grenade (`Softness = 1`, mass 40, radius 0.165) therefore carries inertia
  10 against a sphere's natural 0.44, and that is what stops it on its first
  floor contact: friction turns almost all of its 13 m/s into a slow roll at
  `v * m r² / (m r² + I)` ≈ 1.2 m/s. Measured headlessly after the port:
  12.6 → 1.06.
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

## Activation and a body out of the world

**A script body that is not in the world must never be activated.** Jolt puts it
in the active list without a broadphase entry; the next `DestroyBody` frees it
while it is still listed, and the following step reads a dead body in
`JobApplyGravity`. The fault lands two frames after the call that caused it, on
a solver worker thread, with nothing left to say who did it.

`PO_Enable(e, false)` is what takes a body out — `SetScriptBodyEnabled` calls
`RemoveBody` and clears `inWorld`. So every setter that wakes a body has to
check `inWorld` first: `SetScriptBodyPose`, `SetScriptBodyVelocity`,
`AddScriptBodyImpulse`.

The staked grenade is the case that found it. `Stake:Tick`'s combo branch
clones a `Grenade.CItem`, points its `_Entity` at the stake's own, and explodes
it; `Grenade:Explode` disables the body first — *"bo inaczej by zglaszal msg
'explosion' z soba samym"* — and an `ENTITY.SetVelocity` from a `GObjects:Update`
pass then woke it again. Two Jolt asserts name it exactly:

```
BodyManager.cpp:506  body.IsInBroadPhase() - Use BodyInterface::AddBody to add the body first!
BodyManager.cpp:353  !body->IsActive()
```

Both are on in Debug and RelWithDebInfo (`JPH_ENABLE_ASSERTS`, set in
`CMake/Dependencies.cmake`), routed to the log through `JPH::AssertFailed` with
a stack from `Core/CrashReport`. That pair — the assert for the rule, the stack
for the caller — is how this was found; reading the code was not enough,
because the guard was already present on one of the three setters.

## A corpse is not a wall

`PhysicsWorld::RayCast` resolved the body it hit against `scriptBodies` only.
A ragdoll limb is in neither that list nor the world mesh, so every shot into a
body already on the floor fell through the loop and came back `bodySlot = -1` —
which `TraceCommon` reports as **entity 0**, and entity 0 is how the scripts
spell "the world". `ENTITY.IsFixedMesh` answers true for it and
`EntityToObject[0]` is nil, so a corpse read as level geometry to every
projectile script at once.

The Painkiller's released head shows it: spinning, it is meant to cross a crowd
cutting everything on the way, and the shipped script sends it home on anything
that is not a `CActor`.

```lua
if e and not (obj and (obj._Class == "CActor" or obj.DestroyPack)) then
    ... self._back = true ...
```

Two contacts kill a 250 hp Deto, the third lands on the ragdoll it has just
made, `EntityToObject[0]` is nil — and the blades bounce off their own kill 2.2 m
into a 60 m throw. Intermittent by nature: it depends on whether anything dies
in front of them, which is why a line of lepers (no gib ragdoll, so no corpse
bodies) passed clean through while a real fight did not.

A ray that hits a ragdoll now reports what a hit on the LIVE monster reports —
the owning entity, and a limb handle naming the bone, so
`GetJointFromHavokBody` and the weak-point tests keep working once the thing is
down. Measured on five Detos in a line on TestFloor: before, `BACK at x=3.87,
flown 2.2/60`; after, five kills and no `_back`, the corpses reporting
`cls=CActor cg=10` (RagdollNonColliding).

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

**A receiver with no Lua object goes to the level.** `ExecMsgQueue` hands a
contact whose `e_me` has no `EntityToObject` entry - a `phys` world mesh, which
is an entity but not an object - to `Lev:OnCollision(x,y,z, e_other, h_me,
e_me)`, and `CLevel:OnCollision` looks the mesh up by `ENTITY.GetName(e_me)` in
the level's `_MeshesCollisions` table for its impact sound. The port had no
`GetName`, the missing-native stub returned nil, and the concatenation in
`CLevel.lua:530` raised every tick a released piece of debris touched
anything. A Lua error there aborts `Game:Tick2` for the frame - every later
object's `Tick`, including each rocket's own world line trace - so after one
rocket blast near the Catacombs ledge (which unpins the wood debris there),
rockets stopped exploding on the static world until the debris settled.
Found 2026-09-05 with `bridge_rocket2.lua`; `GetName` now returns the mesh
object's name, or the script's own name, and never nil.

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
monster's body is dynamic like any other and pins the same way; its per-tick
re-command (`StepCharacters`) skips a body that is not dynamic.

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
- The `distance <= 0.0001` branch for props. The ragdoll branch itself is
  ported - see Gibs below.

## Gibs

Recovered 2026-09-02 from `World::GibModel` (0x10060D90), `Model::SetupGib`
(0x101E1A40), `Ragdoll::SelfExplosion` (0x1019CC40 -> FUN_101B0DC0) and
`Ragdoll::IsMovedByExplosions` (0x1019CBB0). Port: `ScriptEngine::MakeGib`
(ScriptDeath.cpp), `PhysicsWorld::RagdollSelfExplosion`, and the corpse branch
of `ScriptEngine::Explosion`.

### When a monster gibs

All of it is script logic in `CActor`, keyed on the template's
`enableGibWhenHPBelow` (-45 for most humanoids, -60 for the skeleton soldier,
-5 for Deto, 0/1 for things that always burst; `nil` never gibs):

1. **At death** (`CActor:EnableRagdoll`): if `Health` is already below the
   threshold - a rocket on a 40-HP skeleton - `CreateGib` runs instead of
   `MDL.EnableRagdoll`. Also when the actor is frozen, or `Game.Cheat_AlwaysGib`.
   On easy difficulty the threshold gains a quarter of the base health.
2. **After death** (`CActor:OnDamage`, dead branch): damage of type
   `Explosion`, `Rocket`, `Grenade` or `PainkillerRotor` accumulates in
   `_HealthAfterDeath`, and the corpse gibs when that drops below the
   threshold. This is the rocket into a pile of bodies, and the spinning
   Painkiller blades held into a corpse (`PainKiller:OnUpdate`, `obrot` /
   `rozkrecenie`, `PainKnifeDamage` per tick).
3. **Item gibs** (`CItem:OnDamage`) for items with the field, and the player's
   own corpse below -40 (`CPlayer`).

`Tweak.GlobalData.DisableGibs` (the German build) turns the lot off.

### What MakeGib does

`MDL.MakeGib(e, group, velocityJoint)` returns a NEW entity or nothing:

- the model is `<Model>_gib` (Cache.lua precaches exactly that name);
- `SetupGib` poses both skeletons and copies the source's bone matrices into
  the gib's **by bone name**, then `Ragdoll::Animate` puts the gib's limbs
  there - the pieces start exactly where the body was;
- `Ragdoll::Activate`, then `Ragdoll::SetVelocities` gives every limb the
  source body's linear and angular velocity - or, when the source is already
  an active ragdoll and a joint is named, that joint's (CItem passes
  `gibGetVelFromJoint`);
- the bounding box is forced to +/-42 so the flying pieces are never culled;
- a gib model with no ragdoll is removed again and nothing is returned.

The scripts then release the source entity, re-key `EntityToObject` to the
gib, play the gib sound and effects, and burst it: `SetRagdollMovedByExplosions
(gib, false)`, two ticks later `(true)` plus `RagdollSelfExplosion(gib, x,y,z,
GibExplosionStrength * FRand(0.2, 0.25), GibExplosionRange)`. The two ticks
matter: the rocket's own `Explosion2` is still being delivered, and a gib that
was moved by it would be launched twice.

### The self-explosion law

`FUN_101B0DC0`, per limb: nothing at or beyond `range`, nothing at `d <=
0.0001` (0x102C8C58), otherwise an impulse of

```
(strength / limbCount) * (1 - d / range)      along (limb - centre)
```

The strength is SHARED across the limbs and the falloff is linear - not the
sine `Explosion2` uses on props. The original measures `d` to the limb's
nearest surface point when within `3 * range`; the port uses the centre of
mass. The whole function is gated on the ragdoll being active AND
`IsMovedByExplosions` (bit 0x10 of its flag byte): a ragdoll with the flag off
takes no push and, because the same loop computes it, **no damage message**.

`WORLD.Explosion2` reaching a corpse goes through the same function with the
blast's strength and range, and posts one `EXPLOSION` with `damage * sin`
falloff of the nearest limb - that is how a rocket gibs a body that is already
on the floor. The port's `Explosion` does exactly that for any entity with an
active ragdoll.

Measured (Cemetery, `Skeleton_SoldierV2`, headless): a 150-damage rocket on
the live skeleton makes the gib at death and the joint spread grows from
1.6 x 2.2 units to 4.6 x 3.9 over 40 ticks; a 50-damage kill then a
3200/8 blast at the corpse takes `_HealthAfterDeath` to -99.5 and gibs it.
Whether the burst FEELS right is a play test.

### The binary .hke

132 of the 324 shipped `.hke` files start with `B` instead of `A` (69 of the
93 gibs, templar, vamp_v2, and every level item with a ragdoll - doors, lamps,
chains, both bridges). It is the SAME grammar as the text form with the words
replaced by numbers, and `Hke.cpp` decodes it back to text so one parser reads
both. Recovered from the reader in `Engine.dll` (FUN_101c02f0 and its word
lookup, FUN_102641C0 hashing into the table at DAT_103E70F4):

| element | encoding |
|---|---|
| keyword or value word (`TRUE`, `Ragdoll`, `Inline`...) | u32 = ELF hash of the word, mod 0x7fffffff |
| `END_*` keywords | preceded by the marker u32 `0x12ABCDEF` |
| int, float | 4 bytes |
| string (names) | NUL-terminated |
| boolean | ONE byte - the first decoder read four and slid off the grammar at `default_subspace` |
| geometry block | kind word, name, u32 nverts, 3 floats each, u32 ntris, 3 u32 each |

The word table is `kHkeWords` in `Hke.cpp`. It has to carry every word the
text form ever uses: `RESTITUTION` (Spring actions), `TWO_BODIES` and the
`Dashpot` action type turned up only in binary files and were found by hashing
every identifier in `Engine.dll` (`HkeStrings.java` in the session
scratchpad; `HkeHash.java` checks one word). The census (`PainfulTools
ragdoll <dir>`) parses 324 of 324 with 0 unknown keywords, which is the check
that the table is complete. `PainfulTools hketext <file.hke>` prints any file
as text.

Before the decoder, a gib with a binary file was built from the live model's
ragdoll cut where the gib MESH is cut (a constraint survives when some
`_gib.pkmdl` mesh is skinned to bones on both sides of it; verified against
the 24 text gibs). That rule is kept in `RagdollDef` only as the fallback for
a file that will not parse, and none shipped does. `MDL.JointsLinked` likewise
answered from the skeleton (yes) when a model had no decoded ragdoll: with
the wrong answer (no) the stake, bolt and Painkiller head had treated every
hit on 19 monster models as a hit on a detachable element and done no damage.
Both answers now come from the real ragdoll.

One file carries a `Dashpot` action (`C2L2_Door2`, an `hkLinearDashpotAction`
between the fixed `joint1` and the door) and 17 a `Spring` action; both are
parsed and neither is simulated yet.

Still stubs: `SetRagdollRestitution`, `SetRagdollCollisionGroup`,
`EnableCollisionsToRagdoll` (the gib-splash collision sounds), and the
`ApplyVelocitiesToJoint` / `ApplyRotationToJoint` joint-level family.


## Ragdoll items: the Catacombs bridge

`Cat_bridge1` is a `CItem` (scale 3.2, `Mass = 250` on the physics object)
whose `OnCreateEntity` calls `MDL.SetRagdollLinearDamping(1)`,
`SetRagdollAngularDamping(1)` and then `MDL.EnableRagdoll(true,
RagdollNonColliding)`: the bridge IS its ragdoll, eight bodies in a row and
nothing else. Until the binary `.hke` decoded there was no ragdoll and the
player fell through the drawn planks. Three rules came out of making it stand.

### Fixed bodies

A rigid body with `MASS 0` (and `ACTIVE FALSE`) is Havok's fixed body. 34
shipped ragdolls have one: the wall end of every hanging lamp, chain, gate and
door, the roots of Catacombs' `korzenie` and `uapka`, and both ends of each
bridge (`joint1_getmass` / `joint8_getmass` here; `most.hke` has three). The
port had let Jolt compute a mass from the hull, so every one of these fell.
`CreateRagdoll` now makes a mass-0 part KINEMATIC and `SetRagdollPose` never
switches it to dynamic; it is seeded at its authored place with the rest and
stays there. The other six planks are 300 each.

### Stiff springs

`hkStiffSpringConstraint` (`BEGIN_CONSTRAINT StiffSpring`) holds one point on
each body a fixed distance apart, and that distance is `SPRING_LENGTH`, not
the authored gap. In beast2, C1L4_TrupA and flagatest the two are equal to
three decimals; the bridge is the exception - its ropes are 3.5 model units
against planks authored 1.3-2.5 apart, so the chain lengthens on activation
and the deck hangs deeper than it was drawn, which is a rope bridge doing what
a rope bridge does. The reader in `Engine.dll` agrees: FUN_1026ee30 reads
`LOCAL_POINT_A/B` and `SPRING_LENGTH`, transforms the points by their bodies,
and stores the length it READ into the constraint info (`local_f0`) as the
last thing before constructing the constraint (FUN_10201810); nothing
recomputes it from the points. `BuildConstraint` sets Jolt's
`DistanceConstraint` min and max to `SPRING_LENGTH * scale`. The `ragdoll <file>` report prints each stiff
spring's authored distance beside its length; the anchor-gap check skips them,
since a rod is not a coincident pair (the 26.9-unit "gap" it used to report on
the bridge was that).

### Damping set before the ragdoll exists

`MDL.SetRagdollLinearDamping` / `AngularDamping` used to require a live
ragdoll slot and the bridge calls both BEFORE `EnableRagdoll`, so they did
nothing and the deck swung for the whole 15-second headless run without
settling (joint5 between -40.7 and -42.0). The values are now kept on the
entity and applied when the ragdoll is created.

### The player's weight

The player is a swept sphere with no mass, so standing on the deck did not
move it - only flying through in noclip did, via the pusher. Havok's character
proxy presses its `characterMass` on the bodies it stands on, so
`PlayerPawn::Move` now calls `PhysicsWorld::PressGround` while grounded: a
downward force of `kPlayerMass * gravity` (80 x 19.62) on every dynamic,
non-character body whose contact point is under the feet sphere. Measured:
with the player on the middle plank it hangs 0.8 lower than with nobody on it
(-43.6 against -42.8) and the neighbours shift by 0.3-0.5.

### The culling box follows the pose

`EntityRenderer` culled an instance by its bind-pose box under the entity
transform, and the sagging deck leaves that box by five units, so a plank in
plain view could vanish. `SetScriptSkinning` now grows the box to every bone's
posed centre padded by the model's half-diagonal; it can only get larger.

Measured headlessly (`bridge_stand.lua`, player dropped 1.5 above the middle
plank): the player lands 0.9 above the plank and stays; the plank under the
player creeps 0.03 between 5 s and 25 s and then holds. The two `noclip_decha`
world meshes at the bridge ends are non-collidable, as `noclip` says ("Active
meshes", the `noclip` row).

## Collision groups: what collides with what

Recovered 2026-09-02 from the Havok world's construction (`FUN_101B5960`,
called by `PhysicsWorld::PhysicsWorld` 0x10196B10): a group filter that
enables every pair and then disables a list. `ECollisionGroups` in
`Definitions.lua` names the groups. The defaults: a script body created with
no group is **4 (Body)**, the player **23 (PlayerBody)**, an active mesh 3
(Normal), the sizer's `-1` branch at 0x101B3E20.

What matters for actors, group 4:
- **4 vs 4 is NOT disabled** - monsters collide with each other.
- 4 vs 23 not disabled - monsters collide with the player's body.
- 4 vs 3 not disabled - and with active meshes.
- disabled: 4 vs 5, 6, 7, 8, 9, 21, 22, 25, 30 - missiles, ragdolls,
  non-colliding, particles, inside-items, RagdollSpecial, Barrier,
  OnlyWithFixedSpecial, ClientGrenade.

So a live monster passes through a `barrier` object (22) that stops the
player, and through ragdolls (6, which is why corpses do not trip them).
MonsterBarrier (27) is the mirror: disabled against 23 and almost everything
else, NOT against 4. The full disabled-pair list is in the decompile
(`PainfulEngineHelpers/ghidra/colgroup3.log`); the port's layers implement the
actor-relevant part of it and not the rest.

## Active meshes: world geometry that is a rigid body

Implemented 2026-09-02. Sources: `WorldMesh::SetupFlags` 0x101D7050,
`World::LoadMeshPakFile` 0x1005DD40, `PhysicsWorld::AddMesh` 0x1019AA00,
`CreatePhysicsObjectFromMesh` 0x10199450, the explosion 0x101B79F0, the
release 0x101B5010, the group calls 0x101B2580 / 0x101B25D0 / 0x101B9AE0.

Some world-mesh objects are not scenery. The `.mpk` encodes the intent in the
OBJECT NAME, and the engine promotes those objects out of the static world into
rigid bodies at load:

| in the name | meaning | where |
|---|---|---|
| `phys` | this object is a body, not static world | `SetupFlags` sets bit 24 of `WorldMesh+0x18`, which `AddMesh` branches on |
| `noclip` | NO body. `SetupFlags` (0x101D7050) sets `WorldMesh+0x1a` bit 0x40, which is bit 0x400000 of the flag dword at +0x18, and `ReloadWorld` (0x1019B180) skips `AddMesh` for any mesh with that bit (unless 0x8000000 is also set). The editor doc ("excluded from Havok physics - player can walk through") is exactly right. A wrong reading on 2026-09-05 had it collidable for a few hours: `FindImm 0x400000` cannot see a byte-wide `OR [+0x1a],0x40`, so the flag looked unset by anything but `EnableDynamic`. Catacombs' `noclip_decha01/02` are the two deck ends at the bridge anchors (world x 160 and 209); the bridge the player walks is the `Cat_bridge1` ragdoll, "Fixed bodies" below | `SetupFlags` |
| `pinned` | starts static; released by a blast, a group activation or a moving neighbour | `AddMesh` |
| `concave` | a mesh body (type 8) rather than a convex one (type 7) | `AddMesh` |
| `physdest` | a destructible's piece: angular damping 1.8, removed from the entity list until its twin's release ("Destructibles" below) | `AddMesh` |
| `statdest` | a destructible's intact twin (not `phys`): static body of its own, hidden and removed at release | `AddMesh` |
| `autodelete` | a deletion timer once released | `AddMesh` |
| `actgrpNN` | active mesh group NN, `sscanf("actgrp%d")` | `LoadMeshPakFile`, into `WorldMesh+0x7e2` |

Every such object is also an ENTITY in the world (`LoadMeshPakFile` calls
`AddEntity` on the mesh), which is why they are lit like entities and not
like the lightmapped world: they carry one UV set and no lightmap.

### Destructibles: a "statdest" twin and its "physdest" pieces

Recovered 2026-09-05 from `AddMesh` (0x1019AA00, both branches),
`FUN_101BA530` (the post-load pairing pass), the release `FUN_101B5010`,
`PhysicsWorld::ActiveMeshGroupActivate` = `FUN_101B9AE0`,
`ActiveMeshGroupEnable` = `FUN_101B2580`, `ActiveMeshGroupStaticMeshEnable`
= `FUN_101B25D0`, and `Game.lua:1150` (`EXPLODEMESH`).

A destructible is TWO sets of objects that share a name stem:

| name | what it is | at load |
|---|---|---|
| `statdest_<stem>shape` | the intact object, static world | `AddMesh` finds its group record, builds a STATIC body for it and a "static twin" record (`FUN_101A6AF0`, entry+0xc) |
| `physdest_<stem>_<k>shape<n>` | the pieces it breaks into | `AddMesh` builds the dynamic body (angular damping 1.8), then `World::RemoveEntity`: no drawing, no ticking, until released |

The pairing (`FUN_101BA530`) is by NAME: from "statdest" on, "stat" becomes
"phys" and the first "shape"/"Shape" is cut, so `statdest_actgrp02_b_grob22shape`
owns every `physdest_actgrp02_b_grob22...`. The matcher that applies that
prefix is not located; the port gives each piece the LONGEST matching prefix
(else Enclave's `grob2` takes `grob22`'s), which pairs every piece on Cemetery
(223 for 22 twins) and Enclave (1523 for 381). What would settle it: the
function that fills `WorldMesh+0x108/+0x10c` on the statdest mesh.

**The release** (`FUN_101B5010`, one entry) for a statdest entry: disable the
static twin, `World::RemoveEntity` the intact mesh, then for each piece
`World::AddEntity` + `MeshesTableToSlotAdd`, zero velocity, release and
`Activate`, a time-to-live (the group's activation params, else 10-19 s),
optional collision callbacks by mass and chance, and the blast force if it is
within range. The scripts get `EXPLODEMESH(actgrp, x, y, z)` -
`Lev:OnExplodeMesh`, which Cemetery and Opera answer with a sound and a quake.
For a pinned phys entry the same function is the plain unpin, with
`EXPLOSION` posted instead.

What calls it: the explosion (0x101B79F0) for every enabled entry within
range + radius - which is how explosives AND the Giant break graves, its stomp
being `WORLD.Explosion2` (Giant.lua:1182) - and `ActiveMeshGroupActivate`
for a whole group (no shipped script calls it). `ActiveMeshGroupEnable(g,
false)` only clears the "explosions may release" bit; `StaticMeshEnable`
switches the twin bodies on and off (Alastor's floors, beside
`WORLD.EnableDrawMeshGroup` for the drawing).

The port: `MapObject::isStaticTwin/isDestructiblePiece/piecePrefix`,
`PhysicsWorld::CreateStaticTwinBody` (an exact MeshShape, static, out of the
world body), pieces created dynamic then `SetScriptBodyEnabled(false)` and
hidden, `ScriptEngine::destructibles_` + `ReleaseDestructible`. Not ported:
the time-to-live, the collision-callback lottery, `EnableDrawMeshGroup`, and
saving the released state.

Before this the pieces were ordinary active meshes - visible around the intact
stone from the start, pushable, and doubled with it - on every level that
ships destructibles.

### Drawing them costs GPU handles, and the pool is finite

The port draws each active mesh through the entity path with its own vertex
and index buffer (`EntityRenderer::CreateWorldObject`), and the static world
keeps one pair per object. Enclave is the extreme: 2367 objects, 1585 of them
`phys` (none pinned - the level itself calls `ENTITY.EnableCollisionsToAll`
and sets activation params for group 2), with 8235 material runs across the
map. Until 2026-09-05 every material run got an index buffer of its own,
which asked bgfx for more than its default 4096 index-buffer handles. bgfx
hands back an INVALID handle in that case and, in a release build, says
nothing; a draw with one uses whatever indices are bound, so every model
created after the pool ran dry - the active meshes themselves, then the
weapons - rendered as spikes and streaks ("vertex explosion" on every
surface, from frame 1, with the world floor intact). The Giant's own few
exploded vertices in the original are unrelated.

Now: one index buffer per mesh or object with parts drawing ranges of it
(`Part::firstIndex`), the pools are 16384 (`CMake/Dependencies.cmake`), and
`MakeIndexBuffer` / `MakeVertexBuffer` (`Render/GpuBuffers.h`) log the first
invalid handle. `PainfulTools level` prints the active-mesh and material-run
counts so a map's demand can be read off before it is loaded.

`AddMesh` builds the body in collision group 3 with the level's
`DefaultMeshFriction` / `DefaultMeshRestitution`, a hard deactivator, and a
mass scaled by `Level_GetActiveMeshesData(name)` when that returns anything
but 1, else by `ActiveMeshesMassScale` from `WORLD.Init`. So the level's
`ActiveMeshesData` values are MASS FACTORS, not groups - an earlier version
of this page read them as groups. Catacombs: `wejsciowy_kamien` x10,
`kolumna` x2, and `ActiveMeshesMassScale = 2` for the rest. Cemetery:
`vheavy` x9, `wieczko` x5.

Catacombs' entrance stones are `pinned_phys_wejsciowy_kamienshape` through
`...shape32` - **31 objects**, pinned. Its columns are
`phys_kolumna_wielka_actgrp24_1` and friends.

The global the ENGINE calls for the factor (nothing in the shipped Lua calls
it):

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

### What releases a pinned one

`AddMesh` gives a pinned object a static twin and files a record (an
0x34-byte entry: position, radius, the body, the twin, the mesh, flags). Two
things clear it, and only two:

- **An explosion** (0x101B79F0, after its impulse pass): every record whose
  centre lies within `range + record radius` of the blast and whose flag bit 0
  is clear is released through 0x101B5010 - twin removed, body enabled and
  activated, collision callbacks and an autodelete timer drawn from the group's
  activation settings.
- **`PHYSICS.ActiveMeshGroupActivate(g)`** (0x101B9AE0): every record of
  group g, unconditionally.

`ActiveMeshGroupEnable(g, on)` (0x101B2580) is what sets or clears that flag
bit, so a disabled group's stones ignore blasts until enabled.
`ActiveMeshGroupStaticMeshEnable` (0x101B25D0) adds or removes the twins.
There is NO contact-driven release: a crate resting on a pinned stone leaves
it pinned, and a stone that a blast released does not wake its neighbours by
touching them. (0x101B87E0, the release routine's other caller, is the
multiplayer explosion.) A first cut here released on contact, and the first
entrance stone came loose at load under the crate standing on it.

### The port

`MapObject::isActiveMesh` is the name test. `PhysicsWorld::BuildStaticWorld`
and `WorldRenderer::Upload` skip those objects; `ScriptEngine::CreateActiveMeshes`
(from `WORLD.LoadMap`) gives each one a script body
(`PhysicsWorld::CreateActiveMeshBody`: convex hull about the bounds centre,
group 3, the level's mesh friction and restitution, static while pinned) and
an entity drawn by `EntityRenderer::CreateWorldObject`, which takes the
object's vertices to world space, re-bases them on the body's centre and lights
them as an entity. The mass factor is fetched by calling the Lua global from
C++ at load; `WORLD.Init`'s `ActiveMeshesMassScale` is applied afterwards to
the bodies the level gave no factor. `WORLD.Explosion2` releases pinned
bodies within `range + radius` before applying its impulse, and
`ActiveMeshGroupActivate` / `Enable` are real; `StaticMeshEnable` and
`SetActivationParams` accept and do nothing (the body itself is the static
twin here, and the autodelete timers are not ported).

### Why they sit still at load

Reported from play: the Cemetery's coffins dropped the moment the level
opened, where the original leaves them until touched. Measured with
`PAINFUL_ACTIVE_TRACE=<frame>` (every active mesh more than 0.05 from where it
was built): **475 of the Cemetery's 548 had moved by frame 60**, coffins by
0.8-1.2, a column drum by 7.4. Two causes, both ours:

- Jolt's default convex radius inflates a hull by 0.05, so two authored
  touching objects - stacked coffins, a column's drums - start 0.1 inside each
  other and the solver pops the stack apart. The hulls are built with 0.005.
- `CreatePhysicsObjectFromMesh` puts every one under the engine's own
  deactivator (`SetHardDeactivator` 0x1018ABF0 -> 0x101A8B40 files the body
  with its position; `WORLD.Init`'s `Deactivator.Delay` 5.0 and `MaxPosDiff`
  0.3 are its thresholds), and a frozen Havok body stays where it is,
  supported or not. Ours are created asleep, which is the same state: a
  sleeping Jolt body does not fall, and an awake body touching it, a blast
  or a release wakes it.

After both: 8 of 548 move by frame 300, planks and one gravestone near the
spawn that something awake touches. Catacombs' blast test is unchanged (28
of 31 stones down).

Deviations: a "concave" object is a hull too (Jolt simulates no concave
dynamic body); `physdest` gets no special damping; the collision-callback
lottery on release is not ported; the deactivator's exact 5 s / 0.3 rule is
Jolt's own sleep test instead.

Measured on Catacombs, headless: 452 bodies, 31 pinned, all 31 still pinned
at frame 30; a range-8 blast beside the first entrance stone releases the
heap and moves 29 of the 31 (max 3.67). Cemetery: 548 bodies, 8 pinned.
Cathedral's squad numbers are unchanged. Not measured: how they look - the
entity lighting on a Cemetery coffin is a play check.

## Projectiles: grenades, rockets, and what "missile" means

Sources: `PhysicsObject::UpdateEntity` 0x10191f90, `FixGrenadeFlight`
0x1018d990, `SetGrenade` 0x1001e020, the `PO_SetGrenade` / `PO_SetMissile`
natives 0x101369b0 / 0x10136a60, `SetMPMissileController` 0x101890a0 and its
controller 0x101a4a50 / 0x101a3ec0, `PCFSystem::SwitchToState` 0x10052660,
the sizer 0x101b3e20, `Grenade.lua`, `Rocket.lua`, `MiniGunRL.lua`.

**A rocket is an ordinary dynamic body.** `Rocket:OnCreateEntity` makes a
`BodyTypes.Sphere` of scale 0.001 in the Particles group, turns gravity off,
and `Rocket:Tick` traces its own path each frame to find what it hit. Nothing
drives it; it flies straight because nothing acts on it. The port used to
turn `ENTITY.RemoveFromIntersectionSolver` into "this is a driven projectile"
— the native only switches line-trace collision off (0x101348e0) — and
since `Grenade:OnCreateEntity` calls it too, the grenade flew a dead straight
line through the level with no body in the solver. That reading is gone;
only `ECollisionGroups.Noncolliding` (7) is driven, as before.

**`PO_SetMissile` is multiplayer only.** The native does nothing unless
`GEngine+0xdc` is set, and that is the `NetworkDevice2` which
`PCFSystem::SwitchToState` creates in states 3–5 (dedicated server, MP
server, MP client) and nulls in state 2 (single game). When it exists,
`SetMPMissileController` attaches a dead-reckoning controller — 16-bit
quantised velocity, its own gravity of -29.43 for grenades, a reflect of
`(restitution + 1.01)` and a friction of `0.2 * wrapper friction` on a hit —
and `UpdateEntity` prefers it over everything below. Not ported; it is the
netcode's projectile model, not the game's.

**A grenade is a dynamic sphere plus `FixGrenadeFlight`.** `PO_SetGrenade`
sets bit 0x20 at `PhysicsObject+0x74`; `UpdateEntity` then runs
`FixGrenadeFlight` after every step instead of the player fix:

1. Trace from where the ENTITY was (its position before the step) to where
   the body is now, up to ten times.
2. Each hit posts `COLLISION_WITH_OTHER_ENTITY` when the body has collision
   callbacks (bit 0x8), with the hit entity and both body handles — the same
   message the contact listener sends, so `Grenade:OnCollision` cannot tell
   them apart. Then the velocity is mirrored across the hit normal, the
   remaining path and its end point are mirrored too, the remainder is
   clamped to 0.002 (`0x102c8530`), and the next trace starts 2 mm past the
   hit.
3. If anything was hit, the body is moved to the mirrored end point and the
   velocity is scaled by `1.6 - friction` (`0x102c852c`; the wrapper's
   friction, 0.8 for a grenade, so × 0.8). Ten hits without escaping zeroes
   it.

It is a tunnelling fix with an energy loss on it, running on top of the
solver's own contact. The port runs it as `ScriptEngine::TickGrenades`
between the physics step and the read-back, which is the same moment.

**The sphere's radius is `scale * 1.1`.** The sizer's sphere case makes
`hkpSphereShape(scale * 0.2 * 5.5)` and never looks at the mesh: a grenade
asks for 0.15 and gets 0.165; a rocket asks for 0.001 and is a point. The
mass it computes, `(0.2 * scale)³ * 10000`, is overwritten by `PO_SetMass`.

**What a missile collides with.** The port puts Missile (5) and Particles
(8) bodies in their own Jolt layer: they collide with the world, props and
monsters, but not with each other and never with the camera's or the pawn's
pusher sphere, and no trace lands on those spheres any more either. The
missile-to-missile rule comes from the data: `BoltGunHeater:AltFire` fires
ten heater bombs 0.05 apart, each a 0.165 sphere, and `HeaterBomb:OnCollision`
explodes on its second contact with any entity — so they cannot be allowed
to touch, or the whole salvo detonates in the barrel (which is exactly what
it did). A rocket is
spawned 0.2 in front of the head — inside the 0.4 pusher — and a dynamic body
born inside a kinematic one is shoved rather than launched. The original's
group filter has not been recovered; this is the assumption, and
`Grenade:OnCollision` guards the owner case itself.

Measured headlessly after the port (Cathedral, stakegun alt fire): the
grenade leaves at (19.0, 6.1, 0.4), arcs, meets the floor at 59 frames with
`e = 0`, bounces to +4.3 vertical, rolls at 1.06, bounces twice more and
explodes on its 69-tick timeout. A rocket leaves at 40.0, keeps it to three
decimals, and `Rocket:Tick`'s own trace finds the Slab at frame 106.

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
- **Monsters are Havok bodies stood in for by Jolt ones.** The tick rule is
  recovered and ported; the body's mass and the player's push are argued
  stand-ins, and Jolt's one-sided mesh needs a floor-standing correction the
  original never did. All in [`MonsterMovement.md`](MonsterMovement.md).
- **Corpses cannot be pinned.** `ENTITY.PO_SetPinned` works on props (see
  Pinned bodies above), but `PHYSICS.PinHavokBody` and the `MDL.SetPinned*`
  family are still stubs, so the stakegun cannot pin a body to a wall.
- **`World/CollisionMesh` still exists** and still answers the corona
  line-of-sight trace. Jolt can answer the same query; the BVH stays until
  there is a reason to move it.
