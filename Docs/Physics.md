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

```
PainfulEngine physics <levelDir> <DataRoot>
```

prints the tweak values it read, what went into the world, whether a sphere
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

## What is missing

- **Little disturbs the props once they settle.** The camera's body can shove
  them, but the things that would really move them — weapons, explosions,
  `PO_Hit`, `PhysicsWorld::Explosion` — are all script-driven.
- **A few props leave the level.** One Cathedral barrel travels 27 units, and
  seven vases drift. Those are individual shapes or placements, not the
  systematic 1.08 above.
- **The renderer and the physics world resolve entities differently.** Cathedral
  gives physics 135 props with nothing unresolved, while the renderer places 218
  models and leaves 420 entities unresolved. Most of that gap is entities that
  legitimately have no model, but the hull view also shows at least one hull
  with nothing drawn at it, so the two disagree somewhere real.
- **No player controller.** It belongs with the Lua host: the player body is
  created by `EngineGame::CreatePlayer`, driven by `PhysicsObject::PlayerAction`
  from an action bitfield the scripts fill in, and `SetPlayerSpeed` /
  `GetPlayerSpeed` are natives the game calls constantly. The constants are all
  recovered (above) and `PlayerAction` is decompiled, so this is a matter of
  wiring, not of research.
- **No ragdolls, no glass, no explosions, no water buoyancy, no ladders, no
  ice.** Each is a named piece of the original: `PrecacheRagdoll` /
  `SetRagdollCollisionGroup`, `Glass` and `Tweak.Glass`,
  `PhysicsWorld::Explosion`, `EnableUnderwaterWorld` and `Tweak.Underwater`,
  `World::NearLadder`, `World::OnIce`.
- **`World/CollisionMesh` still exists** and still answers the corona
  line-of-sight trace. Jolt can answer the same query; the BVH stays until
  there is a reason to move it.
