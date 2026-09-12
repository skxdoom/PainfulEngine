# Status — what works, what does not

A running inventory of the port. The short version lives in the README; this is
the detail, with the authority for each rule named where one exists.

## Version

`Source/Core/Version.h` is the one place it is set — two numbers and a stage.
Bumping it is editing that header and the README line; nothing else carries a
copy, and the `.rc` files include the same header behind `RC_INVOKED`.

It comes out as two strings, deliberately:

| | |
|---|---|
| `Painful Engine` | the window, the taskbar, and Task Manager's process list — `FileDescription` in the `VERSIONINFO` block, not `ProductName`, which is the field Task Manager actually reads |
| `Painful Engine 0.5 alpha` | the head of `painful.log` — a log is read after the fact and has to say which build wrote it |

The window carries no version on purpose: it read as clutter in the taskbar.
Explorer's Details tab gets the numbers instead, with `FileVersion` and
`ProductVersion` both plain `0.5`.

Not to be confused with the version the **scripts** ask about:
`GetEngineVersionString` must keep answering exactly `"1.4"` or `Game:Init`
refuses to run. That is the original engine's number and never follows this one.

## Source layout

One directory, one CMakeLists, one target, one project in the IDE, so the
solution reads like the tree does. The layering is one-directional and the CMake
targets name what each layer may link. That is link-time only, so
`CMake/Layering.cmake` checks the includes directly and fails the configure on
an upward one. Docs/Reference/Diagnostics.md, "Layering".

```
Core <- Assets <- World <- Render
```

```
Source/
  Core/     Common          Mat4, Reader (bounds-checked), ReadFile
            Log             one painful.log, printf-checked, tagged by category
            Check           PAINFUL_CHECK/ASSERT: a failed invariant, logged and tallied
            Debug           the PAINFUL_* switch table (PainfulTools traces)
            Vec3            a 3-vector laid out as float[3]; the engine speaks it now
            Frustum         view-frustum planes and AABB tests (pure maths, so World may use it)
            PakArchive      one .pak: directory parse, name de-obfuscation, inflate
            FileSystem      the mounted view: archives shadow loose files
            AppPaths        finding the game data and the executable's resources
            CrashReport     a symbolised stack on a fault, to stderr and a file
  Assets/   Mpk             world meshes, materials, per-slot UV transforms
            Dat             item mesh packs (the o.Pack containers)
            Pkmdl           models: geometry, skeleton, skin weights
            Ani             skeletal animation
            Skeleton        hierarchy, bind matrices, skinning
            Hke / Rde       ragdoll definitions, render descriptors
            AnimationCache / SkeletonCache
            Properties      the Lua-ish property files, and o.Rot / o.Ang
            ShaderScript    the .shader material scripts
            Emitter         particle emitter .ini and effect .pfx
            Waypoints       the .wps navigation graph
            Tweaks          LScripts/Main/Tweak.lua, the physics constants
  Script/   LuaHost         Lua 5.0.2 state, boot, the engine->Lua frame contract
            Natives         the native API: module tables, stubs, real impls
            NativeList.inc  generated from the recovered list + script usage
  World/    Level           level settings, entity placement, world mesh
            Templates       the BaseObj template chain, with level overlays
            Zones           portal/zone visibility graph
            Lighting        the light set reaching a point
            CollisionMesh   BVH over solid geometry, for line-of-sight queries
            PhysicsWorld    Jolt: the static world, the placed props, queries;
                            +ScriptBodies (script bodies, characters) and
                            +Ragdolls, one class across three TUs
  Render/   Window          SDL3
            Renderer        bgfx device
            Camera          the camera, and the free camera's collision radius
            MaterialState   .shader pass -> bgfx render state, blend modes
            TextureCache    extension-agnostic texture resolution
            WorldRenderer / EntityRenderer / SkyRenderer
            ParticleRenderer / BillboardRenderer
            FontCache / HudRenderer
            DebugLines      world-space line overlay, for collision shapes
  Audio/    AudioEngine     one mixed device stream behind SOUND / SOUND2D / SOUND3D
  Game/     ScriptEngine    the seam: the entity registry and world state behind
                            the natives. One class, one file per family -
                            ScriptEntity, ScriptMonster, ScriptSound,
                            ScriptPlayer, ScriptInput, ScriptTrace, ScriptAnim,
                            ScriptWorld, ScriptHud, ScriptMenu, ScriptDeath,
                            ScriptLimbs - each declaring its own natives and
                            its own binding table, so adding one recompiles
                            that file alone. ScriptBind calls the binders.
            EngineBoot      window, device and level-independent caches, shared
                            with the `run` viewer
            MenuSystem      the retained widget model behind PMENU
            PlayerPawn      the engine-side mover
            Input           bindings, actions, mouse
  App/      Main            the entry point
            GameApp         the script-driven run - PainfulEngine.exe
  Tools/    ToolsMain       the command table - PainfulTools.exe
            ViewerApp       the `run` free-camera viewer
            Report*         one file per family of headless reports
Shaders/    bgfx shader sources, compiled once per backend and embedded in
            the executable (D3D11, D3D12, Vulkan, OpenGL on Windows)
Docs/       Reference/ the recovered rules, Status and Plan, Data/ the work queue
```

Headers sit beside their sources rather than in a parallel `include/` tree.

## What works

### Asset formats

All parse and are cross-checked against a second implementation.

- `.mpk` world meshes: objects, transforms, both UV sets, materials with their
  four texture slots **and per-slot UV transforms**, bounds.
- `.dat` item packs: fully decoded (string table + TOC + `WorldMesh` payloads).
  All 372 shipped packs parse.
- `.pkmdl` models, `.ani` animations, skeletons and skin weights.
- `.CLevel` / `.CItem` / `.CActor` property files and the `BaseObj` template
  chain. Both assignment forms are accepted — `Name.Key = v` and
  `Name["Key"] = v`, the latter used by 117 templates. Level-local
  `Levels/<name>/Templates` directories shadow the global set for that level,
  which 43 levels rely on.
- `.shader` material scripts: 221 definitions, hardware variants, `copy`
  inheritance, `setflag`, `pass copy previous`.
- Particle emitter `.ini` and effect `.pfx` — see [`Particles.md`](Reference/Particles.md).
- `.pak` archives read natively: the shipped `Data/` folder mounts directly
  (`Core/PakArchive` + `Core/FileSystem`), so `Data_Extracted/` is a reference,
  not a requirement. Name recovery decodes 65,628/65,628 file names across all
  ten archives, validated against a full extraction; every headless report is
  byte-identical run from `Data` vs `Data_Extracted`. Mount order follows the
  engine's: `<name>2.pak` > `<name>1.pak` > `<name>.pak` > loose files
  (`Source_Port.md` §3; loose-vs-pak precedence is still unconfirmed
  empirically — the port puts archives first).

### World rendering

- World mesh at the level's authored scale. `o.Scale` scales the **static world
  only** — entities are authored in the scaled space (`CLevel.lua` →
  `WORLD.LoadMap(map, name, Scale, …)`); the class default is `0.3`, not 1.
- Render state comes from the game's own material scripts, not from
  heuristics: blend modes, depth/colour/alpha writes, culling, alpha test with
  the authored `alpharef`, and per-stage sampler flags from `texenv`.
- Material selection follows the engine's naming: a script named after the mesh
  object wins (that is how conveyors and special surfaces are defined), then
  the `defaultTU2` / `defaultTU2x2` / `defaultNTU` families with the
  `trans` / `atest` / `2sided` variants from object-name substrings.
- `o.Overbright` selects the ×2 lightmap material set (`lmx2.shader`), so
  levels that author ×1 are no longer doubled.
- Per-slot UV transforms are applied — terrain tiles its textures 30× and 20×,
  and ignoring that renders one magnified texel patch instead of a surface.
- Terrain blending: two tiled textures mixed through a mask, with the roles
  identified by which slots carry tiling rather than by assumption.
- Fog modes 0/1/2/3 exactly as `CLevel.lua` defines them, per-level
  `FarClipDist`, and the void cleared to the fog colour.
- UV panning (`pan[N]`) in units per second and the detail map sized by the
  level's `DetailMap.TileU/TileV` - both confirmed against the engine rather
  than inferred, in [`TextureTransforms.md`](Reference/TextureTransforms.md).
- Per-stage texture transforms in the engine's own order:
  `uv' = ((uv * slotXform) + pan * t) * tile`. `pan[N]` is units per second and
  `tile[N]` scales the already-panned coordinate, so a stage with both scrolls
  `tile` times faster than the pan figure alone reads.
- Water surfaces, identified the way the engine identifies them - a `strstr`
  test for "water" on the object name - and drawn with the nv20 construction:
  a cube-map reflection through a scrolling, tiled normal map, times the
  lightmap. Its numbers come from the level's own `o.Water` block, and the
  pixel and vertex programs (`water_embm.pso`, `water_ref.vso`) are decoded.
  Eight maps carry world-geometry water. See [`Water.md`](Reference/Water.md).
- Cube maps load through `TextureCache::GetCube`.
- Texture filtering follows the Video Options row (`Cfg.TextureFiltering`:
  bilinear / trilinear / anisotropic) for every scene stage that is not
  `point`, applied live from the menu and at boot; see
  [`Menu.md`](Reference/Menu.md), "Texture filtering".

### Visibility

Rebuilt to match `World::BuildZones` (`Engine.dll` 0x1005c1c0).

- Portals link to zones **geometrically** (names are never parsed): a validity
  pass at tolerance 2.0 discards portals touching fewer than two zones, then a
  link pass at 0.1 attaches each portal to *every* zone it touches.
- Visibility walks from all zones containing the camera through portals that
  pass the frustum test; portals close to the camera stay open, so passing
  through a doorway never blinks the far room out.
- Frustum culling for world chunks and entity instances.
- Everything ambiguous errs toward drawing. Train Station's spawn tunnel drops
  from 1410 to 82 world draws with pixel-identical output.

### Entities

- Transforms are data-driven: `o.Rot` is a `(w, x, y, z)` quaternion and the
  engine applies the textbook matrix to row vectors **without transposing**
  (`Entity::SetRotation`, and the Havok bridge at 0x10196e10 fixes the
  component order); `o.Ang.X/Y/Z` is the Euler fallback.
- `.pkmdl` models are created at `Scale * 0.1` — literal in `CActor.lua` and
  `CItem.lua`. Pack meshes use plain `Scale`.
- Models take the `palskinned` material (which authors `cull cw`, confirming
  the exporter's winding); pack meshes use the `defaultNTU` family.
- Entities whose mesh lives in a `.dat` pack resolve it through the template
  chain, and `Slab` barriers that start open are correctly not drawn.

### Sky

Layered animated domes, composited per the engine's own rules: alpha-weighted
layer blending, mask value read as colour × alpha, mask and lightmap on the
second UV set, layer rotation only where the data asks for it. Verified across
all 56 levels.

### Particles

`CParticleFX` entities resolve through template → `.pfx` → emitter `.ini` and
simulate: spawn interpolation along the emitter's path, velocity blending,
acceleration, the three-point alpha curve, colour ramp, spin, position wrapping,
immortal particles, and both draw types (camera-facing sprite and
velocity-aligned spark). All 56 levels resolve with nothing unresolved; 1183
effects placed. Details, field map and the parts not yet done are in
[`Particles.md`](Reference/Particles.md).

### Billboards and light coronas

`CBillboard` entities, covering both plain sprites and light coronas: distance
size ramp, timer-based fade in and out, and occlusion by a line trace against
solid geometry, throttled to ten traces a second per corona as the original
does. 2790 placed across the shipped levels, 1465 of them coronas, no
unresolved textures. Details in [`Billboards.md`](Reference/Billboards.md).

`World/CollisionMesh` backs the trace: a BVH over the collidable map triangles
(the same object set the original hands to Havok), answering "does this segment
hit anything". It is general-purpose — line of sight, projectiles and AI
visibility all want the same query. 275k–341k triangles per level, built in
100–170 ms at load.

### Physics

Jolt, standing in for Havok. The collidable map geometry is one static body,
the props the level places are real bodies with the mass and friction their
templates declare, and the free camera collides against both — it still flies,
it just stops at walls and shoves loose props aside. Anything the simulation
moves is drawn where it moved to. The constants come from
`LScripts/Main/Tweak.lua` and the level's own `o.Physics` block, which is what
the engine reads too. `P` draws the collision shapes.

Falling out of the level kills: the map's `deathzone*` volumes are read with
the water surfaces, and anything asking to be tested posts `IN_DEATH_ZONE` when
it enters an enabled one. Levels switch individual zones on and off.

A level can switch whole `actgrp` mesh groups: drawn, collidable, regrouped or
removed outright. That is what the two boss arenas are built from — Alastor's
map carries 1,990 grouped objects and shows a few groups at a time.

Windows break. Every `*glass*` object is its own static body, and a shot that
starts one takes it out of the collision, out of the drawn world and takes the
bullet holes stuck to it with it — so the shot carries on through, which is what
the stake and the electro disk ask about. **The shards are not ported**: the
pane vanishes with the shipped dust puff and nothing else, and a savegame does
not remember what is broken ([`Physics.md`](Reference/Physics.md), "Glass").

Details, the numbers, and the sizeable list of what is still missing are in
[`Physics.md`](Reference/Physics.md).

### Entity lighting

Models carry no lightmap and are lit at runtime instead, as
`Entity::ComputeVSLights` does it: level ambient overwritten by whichever
`CEnvironment` box the entity stands in (blended at the box edges here, where
the original cross-fades in time), one directional from
`o.DirLight`, and the nearest four `CLight`s by attenuated intensity — all
evaluated once at the entity origin, including the specular half-vector. That
coarse, per-entity half-vector is why the original's sheen is low-frequency,
and it is reproduced rather than improved on. See
[`Lighting.h`](../Source/World/Lighting.h).

### The script layer

The Lua 5.0.2 host boots the real game scripts — `Loader.lua`, `Game:Init()`
with 1054 templates preloaded, and the per-frame `Game_Tick*/Render/GC` chain —
from the archives or a loose tree alike. 964 files load, none missing.
Unimplemented natives are instrumented stubs that report what the scripts
asked for, which is how the remaining API gets recovered; the reading and the
work queue are in [`Plan.md`](Plan.md).

Of the 638 natives the shipped scripts reference, 227 are implemented. What
that buys:

- **Levels load through the game's own pipeline.** `Game:LoadLevel` runs
  `.CLevel` via LoadObj, level templates, every entity instance file,
  `Lev:Apply()` and `GObjects:Apply()`, and the `ENTITY.*` / `WORLD.*` natives
  land in a real registry (`Game/ScriptEngine`). Cathedral creates 796
  entities.
- **Physics runs on the script path.** `WORLD.LoadMap` builds the Jolt static
  world, `ENTITY.PO_Create` makes each body bare and the scripts dress it
  through the `PO_Set*` family — the original's own division of work. Items
  settle onto the floor before the first frame.
- **Sky, particles and coronas** come through `WORLD.LoadSky` /
  `SetupSkyLayer`, `PARTICLE.AddEmitter` / `SetupEmitter` and
  `BILLBOARD.SetupCorona`, with resolution staying script-side.
- **The player walks, and acts through the game's own seam.** Keys and
  bindings live in `Game/Input`, read out of the scripts' `Cfg` table the way
  `INP.LoadBindings` does; `CPlayer:Tick` turns them into an `Actions` bitmask
  that reaches the mover through `ENTITY.PO_SetAction` and
  `PLAYER.ExecAction`. Measured against `Tweak.PlayerMove`: walking settles at
  7.9999 m/s against `PlayerSpeed` 8.0, and a jump rises 0.753 m. Air control,
  the step ladder and what the player collides with all come from the binary —
  see [`PlayerMovement.md`](Reference/PlayerMovement.md).
- **The scripts own the camera.** `Game:Tick2` reads `MOUSE.GetDelta`,
  accumulates onto `CAM.GetRawRotation` and writes back through `CAM.SetPos` /
  `SetAng`; the C++ loop feeds the mouse in and adopts the result. The free
  camera keeps its own look for noclip and for levels with no player yet.
- **Triggers fire.** CBox ambushes poll the player globals in Lua, engine
  regions post `REGION_ENTERED` / `REGION_LEFT` into `Game_GetMsg`, and hard
  landings post `PLAYER_HIT_GROUND`.
- **Weapons fire and land.** `WORLD.LineTrace` / `LineTraceFixedGeom`, the
  ragdoll intersection-solver registry, the view model, and the hit reaction
  through `ENTITY.PO_Hit` / `WORLD.HitPhysicObject`. Damage needed no native
  work: a weapon traces, looks the entity up in `EntityToObject` and calls
  `obj:OnDamage` — the chain was already live once the trace resolved to the
  right handle.
- **Contacts reach the scripts.** `COLLISION_WITH_OTHER_ENTITY` carries both
  body handles and the velocity each had *at the contact*, so
  `StdOnCollision` can compare impact speed against
  `Destroy.MinSpeedOnCollision` and destructibles break when hit hard enough.
- **Explosions damage and shove.** `WORLD.Explosion2` — the single funnel for
  every explosion in the game — collects what the blast reached, pushes each
  body and posts one `EXPLOSION` message per entity for the scripts to damage.
  The falloff is the engine's own sine curve, verified to three decimals
  ([`Physics.md`](Reference/Physics.md)).
- **Wreckage carries the blast.** `ENTITY.ExplodeItem` gives each part the
  item's own velocity on top of its outward spread, so debris from a barrel a
  rocket just hit leaves with the barrel's motion rather than dropping in
  place. `ENTITY.PO_SetPinned` holds a prop static until a level action
  releases it — which is what makes the Catacombs blockade breakable
  ([`Physics.md`](Reference/Physics.md)).
- **Animation plays.** `MDL.LoadAnim` / `SetAnim` return real indices, the
  clock natives (`GetAnimTime` / `GetAnimLength` / `GetAnimTimeScale` /
  `SetAnimTimeScale` / `ResetFrame`) answer truthfully, and `EntityRenderer`
  poses skinned models on the CPU. That also opens `CActor`'s animation-event
  loop, which is how melee damage, footsteps and attack sounds fire.
- **Monsters move as the original moves them.** A monster is a dynamic body
  whose velocity `PhysicsWorld::StepCharacters` re-commands every tick from
  the recovered `PhysicsObject::Tick`: the AI's `PO_Move` vector plus half of
  whatever the solver added. So the player can shove one, a shot knocks one
  back, two never stand inside each other, and a floor ray answers
  `PO_IsOnFloor` with a real normal. `PO_GetPawnFloorPos` / `HeadPos` follow
  the engine's offsets and `SeesEntity` traces head to head
  ([`MonsterMovement.md`](Reference/MonsterMovement.md)).
- **Ragdolls.** `MDL.EnableRagdoll`, the joint damping and friction setters,
  `ApplyPointImpulseToRagdoll`, and `TickRagdolls` in the frame.
- **Gibs.** `MDL.MakeGib` makes the `<Model>_gib` entity in the pose the body
  died in and hands it the body's velocity; `RagdollSelfExplosion` bursts it
  with the engine's own shared-strength law; blasts reach corpses, so a rocket
  gibs a body on the floor and the spinning Painkiller blades gib what they
  touch. The triggers are the scripts' own thresholds
  ([`Physics.md`](Reference/Physics.md), "Gibs").
- **HUD and sound.** The `HUD.DrawQuad` family with a real `MATERIAL.Create` /
  `Size`, and the `SOUND` / `SOUND2D` / `SOUND3D` families over one mixed
  device stream, with the original's virtual-voice policy deciding what is
  audible: `SOUND.SetSoundProperties` caps instances per file and spaces
  their starts. Levels are the original's (master twice, Miles' rolloff),
  and the ambient/battle music streams play from the shipped `.mp3`s
  ([`Sound.md`](Reference/Sound.md)).
- **Save and load.** The shipped `SaveGame.lua` runs as written: quick save
  and load (F5 / F9), checkpoints, the level-start autosave, and the Saves /
  Autosaves screens with their table, over the original's own container -
  `Save.dat` is a pak, and its name seed is now known, which also made the
  archive reader exact. The world file is ours; what it carries and what it
  does not is in [`LuaHost.md`](Reference/LuaHost.md), "Saving and loading".
- **The console.** `~` drops the panel, and the shipped `Console.lua` runs
  its commands as written: the cheats (`pkgod`, `pkweapons`, `pkhealth`,
  `pkhaste`, `pkdemon`, ...), the settings, Tab completion and the history.
  The multiplayer commands answer for themselves that there is no
  multiplayer. Confirmations show on the message strip after the panel goes
  down, as they did ([`Console.md`](Reference/Console.md)).

## What is missing

The ordered work queue, with the evidence behind each item, is
[`Plan.md`](Plan.md). This is the inventory.

### Gameplay

- **Monster body stand-ins.** The tick rule is the original's; two things
  under it are argued rather than recovered — the stack's mass (`k^3 * 10000`,
  the player's rule assumed for the Fatter stack) and the player's push
  (`speed * 80 / (80 + mass)`, standing in for a Havok contact) — and Jolt's
  one-sided mesh needs `StandCharacterOnFloor`, which Havok never did
  ([`MonsterMovement.md`](Reference/MonsterMovement.md)). Not yet exercised:
  stairs and slopes under the dynamic body, and flyers (`PO_SetFlying` is a
  real flag now, but the `Maintain*` movers behind it are still stubs).
- **Corpse collision groups.** Pinning itself works — a stake nails a body to a
  wall, and the questions asked about a pinned corpse afterwards now answer
  (`MDL.IsPinned` / `IsPinnedJoint`, the joint pose and velocity family;
  [`Physics.md`](Reference/Physics.md), "Pinning a CORPSE"). What is left is the
  group: 53 monster scripts enable their ragdoll as `RagdollNonColliding` and
  every corpse here is an ordinary moving body instead, because the group-pair
  filter behind those numbers has not been recovered.
- **Active meshes, the leftovers.** World objects named `phys` are rigid
  bodies now, drawn by the entity path where physics puts them, released from
  `pinned` by blasts and group activation. Not ported: the autodelete timers
  and collision-callback lottery of `ActiveMeshGroupSetActivationParams`,
  `physdest`'s damping, concave bodies (hulls instead). Detail in
  [`Physics.md`](Reference/Physics.md).
- **Waypoint routing across storeys.** `PATH.GetShortest` routes over the
  whole graph with the waypoint `floor` index unused. The original's
  `Pathfinder2` snaps start and end by 3D distance too (0x10166870, no floor
  test) but then routes storey to storey through portals; whether that ever
  yields a different route than flat A* over the same links is not measured.
  `WPT.GetClosest` / `GetPosition` now answer, so the five scripts that put a
  monster back on the walkable set do so.
- **Flying and scripted movers.** `PO_SetFlying` is a real flag the tick
  honours, but `PO_MaintainVelocity` / `MaintainLinearMovement` /
  `MaintainPosition` and `PO_EnableSpeedDamping` behind it are stubs, so
  Alastor and the ravens still have no mover.
- **Collision-group plumbing.** `EnableCollisionsToAll`, `PO_Activate`.
  `PO_SetCollisionGroup` applies the layer/motion rule to a live body (the
  thrown cans and fireballs), and the player is hit by the AI's traces,
  by contacts and by blasts ([`Physics.md`](Reference/Physics.md), "The
  player takes hits"); the finer group pairs of the original's filter are
  still not modelled.
  `EnableCollisionsToRagdoll` is real: a corpse's armed joints report their
  landing, which is the fall sound and the blood ([`Physics.md`](Reference/Physics.md),
  "Ragdoll limbs report too").
- **The ragdoll joint API.** The `ApplyVelocitiesToJoint` /
  `ApplyRotationToJoint` family, `SetRagdollRestitution`. The `Spring` and
  `Dashpot` actions in the
  `.hke` are parsed but not simulated ([`Physics.md`](Reference/Physics.md),
  "The binary .hke").
- **Lifetime and world state.** `WORLD.SetWorldSpeed` (slow motion),
  `WORLD.RemoveEntity` / `DeleteDyingEntities`, `PHYSICS.SetGravity`.
  `PARTICLE.Restart` and `SetImmortal` are still stubs.
- No glass, buoyancy, ladders or ice. See [`Physics.md`](Reference/Physics.md).
- `PLAYER.GetCameraFix` answers a literal 0, so there is no view bob or crouch
  offset on the camera.

### Rendering

- Decals are in: `ENTITY.SpawnDecal` / `SpawnOrientedDecal` / `SpawnStaticDecal`
  / `UpdateDecal`, `R3D.KeepDecals` and `Cfg.DecalsStayTime`, cut against the
  map object under the hit ([`Decals.md`](Reference/Decals.md)). Not on models
  or pack meshes. Trails (`AttachTrailToBones`) are still stubs.
- Script-driven dynamic lights are in: the whole `LIGHT.*` family, so the
  flashlight (cookie, cone and flicker), the torches monsters carry and the
  flashes an action fires off all light both models and the world mesh
  ([`Lighting.md`](Reference/Lighting.md)). The lights now come from the
  scripts rather than from the level file, `CLight:Apply` included, and models
  and the world run the SAME per-pixel evaluation - eight lights each, with the
  gains recovered from the shipped `proj.pso` / `ps_atten_dst2.pso`. That last
  part is deliberately better than the original, whose model path was four
  per-entity constants on a different falloff curve; the reasons are listed
  under "Deviations". The flashlight casts real shadows - world and models
  into one depth map, tested per pixel ("Shadows" there); `R3D.EnableShadows`
  gates it. The models also cast from the environment directional, onto the
  world and each other, from a second map only they are drawn into. Left:
  `ENTITY.AddLight`, `WORLD.SetDirLight`, `LIGHT.SetLitParentFlag`, and
  shadows from the other dynamic lights - a torch still lights through a wall
  within its range.
- Model material extras: `MESH.SetDetailMap` / `SetNormalMap` / `SetCubeMap` /
  `SetSpecular`, `MDL.SetMaterial` / `SetTexture`, `MATERIAL.Replace`.
- `R3D.SetCameraFOV` / `GetCameraFOV` carry `Cfg.FOV`, and every shipped call
  site is a whole-screen change (`Game:Init`, the menu's 90, the console's
  `fov` command), so the field of view is no longer fixed.
- Water above the fixed-function tier: the EMBM cube pass, reflection and
  refraction render targets, and the `FXWater` programs inside `Water.fxo`.
  Also the water combine above the reflection — which `o.Water` property feeds
  the diffuse and specular terms of `mad r0, t3, v0, v1` is not recoverable
  from the shipped files — the vertex wave, and the swamp surface's
  `$envcubemap`. See [`Water.md`](Reference/Water.md).
- Texture rotation in the stage transform is not implemented. Nothing in the
  shipped data sets one — it can only arrive through a named xform, whose
  contexts leave it at zero — so it is currently unreachable.
- The Factory conveyor strip (`tasmashape`) renders as grey mush where the
  original shows crisp ridges, and too bright. Several causes ruled out; the
  remaining suspect is its lightmap atlas region. Details in
  [`TextureTransforms.md`](Reference/TextureTransforms.md).
- Post-processing: bloom is in ([`Bloom.md`](Reference/Bloom.md) - the
  original's threshold, kernel and gains, at the screen's own resolution
  instead of a 512x512 point-sampled copy). No motion blur.
- Particle texture animation uses frame 0 only, and the `WarpTex` refraction
  pass is not implemented.
- Antiportal occlusion is parsed but unused, portal frustum clipping is
  approximate (a portal in view opens its zones, where the original narrows
  the frustum through the portal polygon), and `WORLD.UseSwitchZones` /
  `EnablePortal` are stubs so levels cannot switch their own visibility.
- A few `.mpk` per-object trailing bytes are still unparsed; they appear to
  hold extra blend-layer materials.

### Everything else

- Acoustic environments and sound occlusion: `WORLD.FindEnvironmentAtPoint`,
  `SOUND.SetRoomType`, `SOUND3D.SetObstructed` / `SetIntensity`, and the
  music streams' low-pass.
- Menus: the main menu, options, controls (with key capture), the campaign
  map and the save / load screens work, and a plain launch boots to the menu
  and starts a new game through it, with a runtime level switch. Still
  missing: credits, movies (Bink), multiplayer (its three rows are greyed
  out until there is a network layer). The weapon priority lists,
  value widgets, row placement and the font metrics follow the engine
  ([`Menu.md`](Reference/Menu.md)); the map's layout is a stand-in. No
  netcode.
- `.pkm` mod packages do not auto-mount yet — their internal format is still an
  open question, and the `GZipPack` exports hint the engine also reads real
  ZIPs.

### A caveat on the measurement

The headless report runs clean — 0 script errors, and the HUD lays itself out
from real material sizes (see [`Hud.md`](Reference/Hud.md)) — but by default it
is an **idle run**: nothing fires, nothing takes damage and no monster engages,
so the weapon, explosion and pin natives are absent for lack of exercise rather
than because they work. The `lua` command's fourth argument fixes that: an exec
chunk that gives ammo and pulses `Actions.Fire` makes the same run fight, which
is the only way those families appear at all. The command, the census it
produced and the static sweep behind it are in [`Stubs.md`](Stubs.md).

## Why it is built this way

### Why the renderer is a full rewrite
`D3Dev.dll` exposes exactly **two** exported functions, `CreateDevice` and
`DestroyDevice`. Everything else is internal, so there is no usable seam to
reimplement against — the renderer must be written from scratch. The good news is
that what it must *draw* is fully understood: `.mpk` geometry with two UV sets
(diffuse + lightmap), and skinned `.pkmdl` meshes.

### Why physics is tractable
PCF already wrapped Havok behind `PhysicsWorld` / `PhysicsObject` / `Ragdoll`
using opaque handles and engine-native math types, and collision is **built at load
time from `WorldMesh`/`Model` geometry** rather than deserialised from Havok. The
17,643 `.mopp` files are a Havok-specific *cache* derived from that geometry — a
Jolt backend regenerates its own acceleration structure and ignores them entirely.

