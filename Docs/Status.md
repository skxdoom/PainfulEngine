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
  Assets/   Mpk               world meshes, materials, per-slot UV transforms
            Dat               item mesh packs (the o.Pack containers)
            Pkmdl             models: geometry, skeleton, skin weights
            Ani               skeletal animation
            Skeleton          hierarchy, bind matrices, skinning
            Properties        the Lua-ish property files, and o.Rot / o.Ang
            ShaderScript      the .shader material scripts
            Emitter           particle emitter .ini and effect .pfx
  World/    Level             level settings, entity placement, world mesh
            Templates         the BaseObj template chain, with level overlays
            Zones             portal/zone visibility graph
            CollisionMesh     BVH over solid geometry, for line-of-sight queries
  Render/   Window            SDL3
            Renderer          bgfx device
            Frustum           view-frustum planes and AABB tests
            MaterialState     .shader pass -> bgfx render state, blend modes
            TextureCache      extension-agnostic texture resolution
            WorldRenderer / EntityRenderer / SkyRenderer
            ParticleRenderer / BillboardRenderer
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

## What is missing

### Rendering

- Texture panning (`pan[N]`) animates, but the units are not yet confirmed
  against the engine, so speed/direction may be wrong.
- Detail maps are applied (`addsigned` grain) but sized by inference. The
  shipped `tu2_detail.vso` shows detail rides `texcoord0` through its own
  matrix (`c27/c28`), separate from the diffuse's — the code that fills those
  constants lives in `D3Dev.dll`'s material system, which is the next read.
- Water: `FXWater` (EMBM reflection) is not implemented; water surfaces render
  as plain geometry.
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

### Everything else

- No `.pak` reading — the data must be extracted first.
- No Lua host, so nothing scripted runs: no spawning, triggers, doors,
  pickups, AI or level progression.
- No physics (Jolt is the intended replacement for Havok) and no collision
  response. `CollisionMesh` answers ray queries but simulates nothing; player
  movement is still a free-flying camera.
- No sound, no HUD, no menus, no save/load, no netcode.
