# Status — what works, what does not

A running inventory of the port. The short version lives in the README; this is
the detail, with the authority for each rule named where one exists.

## Source layout

Layering is one-directional and enforced by the CMake targets:

```
Core   <- Assets <- World <- Render
```

```
Source/
  Core/     Common.h/.cpp     Mat4, Reader, ReadFile
            Log.h
            PakArchive        one .pak: directory parse, name de-obfuscation, inflate
            FileSystem        the mounted view: archives shadow loose files
  Script/   LuaHost           Lua 5.0.2 state, boot, the engine->Lua frame contract
            Natives           the native API: module tables, stubs, real impls
            NativeList.inc    generated from Docs/Engine_LuaAPI.md + script usage
  Game/     ScriptEngine      the seam: entity registry + world state behind
                              the ENTITY.*/WORLD.* natives, renderer optional
  Assets/   Mpk               world meshes, materials, per-slot UV transforms
            Dat               item mesh packs (the o.Pack containers)
            Pkmdl             models: geometry, skeleton, skin weights
            Ani               skeletal animation
            Skeleton          hierarchy, bind matrices, skinning
            Properties        the Lua-ish property files, and o.Rot / o.Ang
            ShaderScript      the .shader material scripts
            Emitter           particle emitter .ini and effect .pfx
            Tweaks            LScripts/Main/Tweak.lua, the physics constants
  World/    Level             level settings, entity placement, world mesh
            Templates         the BaseObj template chain, with level overlays
            Zones             portal/zone visibility graph
            CollisionMesh     BVH over solid geometry, for line-of-sight queries
            PhysicsWorld      Jolt: the static world, the placed props, queries
  Render/   Window            SDL3
            Renderer          bgfx device
            Frustum           view-frustum planes and AABB tests
            MaterialState     .shader pass -> bgfx render state, blend modes
            TextureCache      extension-agnostic texture resolution
            WorldRenderer / EntityRenderer / SkyRenderer
            ParticleRenderer / BillboardRenderer
            DebugLines        world-space line overlay, for collision shapes
  main.cpp
Shaders/    bgfx shader sources, compiled by the build into Shaders/ beside
            the executable
Docs/       reverse-engineering notes and the porting plan
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
- Particle emitter `.ini` and effect `.pfx` — see [`Particles.md`](Particles.md).
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
  than inferred, in [`TextureTransforms.md`](TextureTransforms.md).
- Per-stage texture transforms in the engine's own order:
  `uv' = ((uv * slotXform) + pan * t) * tile`. `pan[N]` is units per second and
  `tile[N]` scales the already-panned coordinate, so a stage with both scrolls
  `tile` times faster than the pan figure alone reads.
- Water surfaces, identified the way the engine identifies them - a `strstr`
  test for "water" on the object name - and drawn with the nv20 construction:
  a cube-map reflection through a scrolling, tiled normal map, times the
  lightmap. Its numbers come from the level's own `o.Water` block, and the
  pixel and vertex programs (`water_embm.pso`, `water_ref.vso`) are decoded.
  Eight maps carry world-geometry water. See [`Water.md`](Water.md).
- Cube maps load through `TextureCache::GetCube`.

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
[`Particles.md`](Particles.md).

### Billboards and light coronas

`CBillboard` entities, covering both plain sprites and light coronas: distance
size ramp, timer-based fade in and out, and occlusion by a line trace against
solid geometry, throttled to ten traces a second per corona as the original
does. 2790 placed across the shipped levels, 1465 of them coronas, no
unresolved textures. Details in [`Billboards.md`](Billboards.md).

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

Details, the numbers, and the sizeable list of what is still missing are in
[`Physics.md`](Physics.md).

## What is missing

### Rendering

- Water above the fixed-function tier: the EMBM cube pass, reflection and
  refraction render targets, and the `FXWater` programs inside `Water.fxo`.
  See [`Water.md`](Water.md).
- Texture rotation in the stage transform is not implemented either. Nothing in
  the shipped data sets one - it can only arrive through a named xform, whose
  contexts leave it at zero - so it is currently unreachable.
- The Factory conveyor strip (`tasmashape`) renders as grey mush where the
  original shows crisp ridges, and too bright. Several causes ruled out; the
  remaining suspect is its lightmap atlas region. Details in
  [`TextureTransforms.md`](TextureTransforms.md).
- The water combine above the reflection: which `o.Water` property feeds the
  diffuse and specular terms of `mad r0, t3, v0, v1` is not recoverable from
  the shipped files. Also the vertex wave, and the swamp surface's
  `$envcubemap`. See [`Water.md`](Water.md).
- Post-processing: no bloom (`Bloom.fxo`), no shadow maps, no motion blur.
- Dynamic lights and specular are not implemented — lighting is baked
  lightmaps plus level ambient only.
- Skeletal animation does not play back; models render in bind pose.
- No decals (`Scripts/Decals`) and no trails (`Scripts/Trails`).
- Particle texture animation uses frame 0 only, and the `WarpTex` refraction
  pass is not implemented.
- Antiportal occlusion is parsed but unused, and portal frustum clipping is
  approximate (a portal in view opens its zones, where the original narrows
  the frustum through the portal polygon).
- A few `.mpk` per-object trailing bytes are still unparsed; they appear to
  hold extra blend-layer materials.

### Script layer

- The Lua 5.0.2 host boots the real game scripts: `Loader.lua` (68 files),
  `Game:Init()` — 1054 templates preloaded — and the per-frame
  `Game_Tick*/Render/GC` chain, with zero script errors, from the archives or
  a loose tree alike (`PainfulEngine lua <DataRoot> [frames] [level]`).
  Unimplemented natives are instrumented stubs that report what the scripts
  call, which is how the remaining API gets recovered.
- **Script-driven level loading works**: `Game:LoadLevel` runs its own
  pipeline — `.CLevel` via LoadObj, level templates, every entity instance
  file, `Lev:Apply()`, `GObjects:Apply()` — and the `ENTITY.*`/`WORLD.*`
  natives land in a real registry (`Source/Game/ScriptEngine`).
  `PainfulEngine game <DataRoot> [level]` renders the result: on Cathedral
  the scripts create 631 entities and the window shows the world, fog and
  models they asked for.
- **Physics runs on the script path**: `WORLD.LoadMap` builds the Jolt
  static world synchronously, `ENTITY.PO_Create` makes each body bare and
  the scripts dress it through the `PO_Set*` family — the original's own
  division of work. Items settle onto the floor before the first frame, and
  the camera collides and pushes props as in `run`.
- **Sky, particles and coronas run on the script path**: the layered sky
  through `WORLD.LoadSky`/`SetupSkyLayer`, effects through
  `PARTICLE.AddEmitter`/`SetupEmitter` (resolution stays script-side), and
  coronas through `BILLBOARD.SetupCorona`. The scripts create the item-bound
  flames the hand-driven batch loader never resolved, so `game` shows more
  than `run`. Not yet on this path: collision events into `Game_GetMsg`,
  player, sound. See [`LuaHost.md`](LuaHost.md).

### Everything else

- `.pkm` mod packages do not auto-mount yet (their internal format is still an
  open question — `GZipPack` exports hint the engine also reads real ZIPs).
- The script layer does not yet drive the engine: `WORLD.*`/`ENTITY.*` natives
  are stubs, so no scripted spawning, triggers, doors, pickups, AI or level
  progression reaches the screen yet.
- Nothing wakes the physics props, no player controller, and no ragdolls,
  glass, explosions, buoyancy, ladders or ice. See [`Physics.md`](Physics.md).
- No sound, no HUD, no menus, no save/load, no netcode.
