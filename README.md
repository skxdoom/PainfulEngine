# PainfulEngine

An open reimplementation of **PainEngine**, the engine behind *Painkiller* (2004).

Requires your own copy of the game's data. **No original assets or binaries are
distributed with this project.**

The formats and behaviour it reproduces were reverse-engineered from the shipped
game — the notes are in [`Docs/Source_Port.md`](Docs/Source_Port.md).

## Method: read the original, do not guess

Every rule the engine follows is recovered from something the game shipped,
in this order of preference:

1. **The shipped data itself** — `.mpk` material slots, `.CLevel` properties,
   the `.shader` material scripts, the `.dat` packs.
2. **The shipped Lua** — `LScripts/` holds the class behaviour (`CLevel.lua`
   decides world scale, fog and sky; `Slab.lua` decides which barriers are
   visible at level start).
3. **The binaries** — `Engine.dll` decompiled in Ghidra when the data is
   ambiguous, and the original DX8 shader bytecode (`Data/Shaders/*.vso`,
   `*.pso`) hand-decoded when the maths must be exact.

Heuristics that "look right" have repeatedly turned out wrong here, in ways
that only showed up levels later. Where a rule below cites an address or a
file, that is the authority for it.

## Why a source port is tractable

Painkiller's gameplay logic is not compiled — it lives in Lua scripts and
serialised property tables. A source port therefore means implementing the
*native API those scripts call* rather than rewriting the game's design. Of the
941 native functions recovered from `Engine.dll`, 790 are actually called by the
shipped scripts and just **113 cover 80% of all call sites**
(see [`Docs/native_priority.tsv`](Docs/native_priority.tsv)).

## Layout

Layering is one-directional and enforced by the CMake targets:

```
Core   <- Assets <- World <- Render
```

```
Source/
  Core/     Common.h/.cpp    Mat4, Reader, ReadFile
            Log.h
  Assets/   Mpk              world meshes, materials, per-slot UV transforms
            Dat              item mesh packs (the o.Pack containers)
            Pkmdl            models: geometry, skeleton, skin weights
            Ani              skeletal animation
            Skeleton         hierarchy, bind matrices, skinning
            Properties       the Lua-ish property files
            ShaderScript     the .shader material scripts
  World/    Level            level settings, entity placement, world mesh
            Templates        the BaseObj template chain
            Zones            portal/zone visibility graph
  Render/   Window           SDL3
            Renderer         bgfx device
            Frustum          view-frustum planes and AABB tests
            MaterialState    .shader pass -> bgfx render state
            WorldRenderer / EntityRenderer / SkyRenderer / TextureCache
  main.cpp
Shaders/    bgfx shader sources, compiled by the build into Shaders/ beside
            the executable
Docs/       reverse-engineering notes and the porting plan
```

Headers sit beside their sources rather than in a parallel `include/` tree.

## Building

Dependencies live in `External/` as git submodules and build from source, so
nothing is installed system-wide.

```
git clone --recursive <this repo>
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The build also compiles the shaders (via bgfx's `shaderc`) whenever their
sources change. bgfx selects a backend automatically; on Windows that is
**Direct3D 11**.

To have each build copy the executable and shaders into a game folder, set the
deploy path once (it is a local cache variable, never committed):

```
cmake -S . -B build -DPAINFUL_DEPLOY_DIR="X:/Painkiller/Bin"
```

## Usage

`build/Release/PainfulEngine.exe` is a windowed application: double-clicking it
opens no console, but running it from a terminal still prints.

**Recommended setup:** copy `PainfulEngine.exe` and the `Shaders/` folder into
the game's `Bin/` directory, beside the original `Painkiller.exe`. With no
arguments it finds the sibling data directory itself and opens the first
campaign level. (The unpacked `Data_Extracted/` layout is required for now;
reading the `.pak` archives directly is planned.)

```
PainfulEngine                                        find the game data and run
PainfulEngine run    <DataRoot/Levels/NAME> <DataRoot>   open a specific level
PainfulEngine level  <DataRoot/Levels/NAME> <DataRoot>   headless level report
PainfulEngine levels <DataRoot>                      list levels
PainfulEngine map    <file.mpk>                      world mesh: objects, materials, bounds
PainfulEngine model  <file.pkmdl>                    model: meshes, skeleton, bounds
PainfulEngine dat    <file.dat | directory>          item packs (a directory validates all)
PainfulEngine skydump <file.mpk>                     dome shells, UV ranges, material slots
PainfulEngine shaders <DataRoot> [name]              material scripts; one name prints it resolved
PainfulEngine zones  <DataRoot/Levels/NAME> <DataRoot> [x y z]   portal/zone graph
PainfulEngine mats   <file.mpk>                      material/lightmap sanity report
PainfulEngine texdump <DataRoot> <name> [out.tga]    decode a texture, print corner pixels
PainfulEngine resolve <DataRoot> <name>              where a texture reference resolves
PainfulEngine ground <DataRoot/Levels/NAME> <DataRoot> <x y z> <radius>   floor probe
```

`run` flags:

| Flag | Effect |
|---|---|
| `--pos x y z` | start position |
| `--look yaw pitch` | start orientation, radians (the HUD prints `rot` in this form) |
| `--shot <file>` | capture one frame to a `.tga` and exit |
| `--skyview` | draw only the sky |
| `--novis` | disable frustum and zone culling, and lift the far clip |
| `--cull ccw\|cw\|none`, `--ecull …` | override world / entity winding |
| `--escale <f>` | debug multiplier on entity scale |

Environment: `PAINFUL_SHOT_FRAME=<n>` delays `--shot` capture (for verifying
animation), `PAINFUL_SKYLAYER=<n>` draws only one sky layer.

Controls: click to capture the mouse, WASD to move, shift for fast, space/ctrl
for up and down, `[` `]` to cycle levels, Escape to release the mouse and again
to quit. The HUD prints position, orientation, draw counts and visible zones.

`Tools/shot.ps1` converts a captured `.tga` to a downscaled `.png`
(`-Crop "x,y,w,h"`, `-Full` for native resolution).

## What works

**Asset formats** — all parse and are cross-checked against a second
implementation:

- `.mpk` world meshes: objects, transforms, both UV sets, materials with their
  four texture slots **and per-slot UV transforms**, bounds.
- `.dat` item packs: fully decoded (string table + TOC + `WorldMesh` payloads).
  All 372 shipped packs parse.
- `.pkmdl` models, `.ani` animations, skeletons and skin weights.
- `.CLevel` / `.CItem` / `.CActor` property files and the `BaseObj` template
  chain.
- `.shader` material scripts: 221 definitions, hardware variants, `copy`
  inheritance, `setflag`, `pass copy previous`.

**World rendering**

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

**Visibility** — rebuilt to match `World::BuildZones` (`Engine.dll` 0x1005c1c0):

- Portals link to zones **geometrically** (names are never parsed): a validity
  pass at tolerance 2.0 discards portals touching fewer than two zones, then a
  link pass at 0.1 attaches each portal to *every* zone it touches.
- Visibility walks from all zones containing the camera through portals that
  pass the frustum test; portals close to the camera stay open, so passing
  through a doorway never blinks the far room out.
- Frustum culling for world chunks and entity instances.
- Everything ambiguous errs toward drawing. Train Station's spawn tunnel drops
  from 1410 to 82 world draws with pixel-identical output.

**Entities**

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

**Sky** — layered animated domes, composited per the engine's own rules:
alpha-weighted layer blending, mask value read as colour × alpha, mask and
lightmap on the second UV set, layer rotation only where the data asks for it.
Verified across all 56 levels.

## What is missing

**Rendering**

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
- No decals, particles or billboards.
- Antiportal occlusion is parsed but unused, and portal frustum clipping is
  approximate (a portal in view opens its zones, where the original narrows
  the frustum through the portal polygon).
- A few `.mpk` per-object trailing bytes are still unparsed; they appear to
  hold extra blend-layer materials.

**Everything else**

- No `.pak` reading — the data must be extracted first.
- No Lua host, so nothing scripted runs: no spawning, triggers, doors,
  pickups, AI or level progression.
- No physics (Jolt is the intended replacement for Havok), no collision
  response, no player movement beyond a free-flying camera.
- No sound, no HUD, no menus, no save/load, no netcode.

## Roadmap

Rendering first: it has a ground truth (the original renders the same level, so
screenshots can be compared directly), while gameplay correctness only reveals
itself deep into a playthrough.

1. Confirm the detail-map and pan constants in `D3Dev.dll`.
2. Water (`FXWater`), which needs a render-target pass — shared with:
3. Bloom and the rest of the `.fxo` post chain.
4. Skeletal animation playback and GPU skinning.
5. `.pak` reading, so a vanilla install works untouched.
6. Lua 5.0.2 host with the natives from `native_priority.tsv`, in call-count
   order — the point where the game starts to *play*.
7. Physics via Jolt, driven by the same native API.

## Third-party

- [SDL3](https://github.com/libsdl-org/SDL) — zlib licence
- [bgfx](https://github.com/bkaradzic/bgfx) via
  [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) — BSD 2-clause
