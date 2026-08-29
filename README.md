# PainfulEngine

An open reimplementation of **PainEngine**, the engine behind *Painkiller* (2004).

Requires your own copy of the game's data. **No original assets or binaries are
distributed with this project.**

Painkiller's gameplay logic is not compiled — it lives in Lua scripts and
serialised property tables. A source port therefore means implementing the
*native API those scripts call* rather than rewriting the game's design. Of the
941 native functions recovered from `Engine.dll`, 790 are actually called by the
shipped scripts, and just **113 cover 80% of all call sites**.

Every rule the engine follows is recovered from something the game shipped —
the data itself, the shipped Lua, or `Engine.dll` decompiled in Ghidra —
never from a heuristic that looks right. Heuristics have repeatedly turned out
wrong here in ways that only surfaced levels later.

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
Direct3D 11.

To have each build copy the executable and shaders into a game folder, set the
deploy path once (a local cache variable, never committed):

```
cmake -S . -B build -DPAINFUL_DEPLOY_DIR="X:/Painkiller/Bin"
```

## Usage

Copy `PainfulEngine.exe` and the `Shaders/` folder into the game's `Bin/`
directory, beside the original `Painkiller.exe`. With no arguments it finds the
sibling data directory and opens the first campaign level. A vanilla install
works untouched: the `.pak` archives in `Data/` are read directly, with
numbered patch archives (`Textures2.pak` > `Textures1.pak` > `Textures.pak`)
and loose files layered the way the original engine mounts them. An unpacked
`Data_Extracted/` tree still works as a data root too.

It is a windowed application, so double-clicking opens no console — but run
from a terminal it still prints.

Click to capture the mouse, WASD to move, shift for fast, space/ctrl for up and
down, `N` for noclip, `P` for the collision wireframe, `[` `]` to cycle levels,
Escape to release the mouse and again to quit. The HUD prints position,
orientation, draw counts and visible zones. The camera flies, but it collides
with the world and shoves loose props out of its way — `N` turns that off when a
level needs surveying from outside.

### Running a level

```
PainfulEngine                                             find the game data and run
PainfulEngine run <DataRoot/Levels/NAME> <DataRoot>       open a specific level
PainfulEngine levels <DataRoot>                           list levels
```

| `run` flag | Effect |
|---|---|
| `--pos x y z` | start position |
| `--look yaw pitch` | start orientation, radians (the HUD prints `rot` in this form) |
| `--shot <file>` | capture one frame to a `.tga` and exit |
| `--skyview` | draw only the sky |
| `--novis` | disable frustum and zone culling, and lift the far clip |
| `--cull ccw\|cw\|none`, `--ecull …` | override world / entity winding |
| `--escale <f>` | debug multiplier on entity scale |
| `--noclip` | start with camera collision off (`N` toggles it) |
| `--physdebug` | start with the collision wireframe on (`P` toggles it) |

`PAINFUL_SHOT_FRAME=<n>` delays the `--shot` capture (for verifying animation);
`PAINFUL_SKYLAYER=<n>` draws only one sky layer. `Tools/shot.ps1` converts a
captured `.tga` to a downscaled `.png` (`-Crop "x,y,w,h"`, `-Full` for native
resolution).

### Headless reports

Every subsystem has a command that resolves its data and prints what it found,
so a change can be checked without opening a window.

| Command | Prints |
|---|---|
| `level <levelDir> <DataRoot>` | level settings, entity counts, world mesh totals |
| `entities <levelDir> <DataRoot>` | placed entities and how each one resolves |
| `particles <levelDir> <DataRoot>` | effect → emitter chain, per-emitter parameters |
| `billboards <levelDir> <DataRoot>` | coronas and sprites, plus collision BVH timing |
| `physics <levelDir> <DataRoot>` | the physics world, and probes of it |
| `zones <levelDir> <DataRoot> [x y z]` | portal/zone graph, optionally from a point |
| `ground <levelDir> <DataRoot> <x y z> <radius>` | floor probe |
| `scale <levelDir> <DataRoot>` | world scale sanity check |
| `fit <levelDir> <DataRoot>` | entity/world fit report |
| `map <file.mpk>` | objects, materials, bounds |
| `mats <file.mpk>` | material/lightmap sanity report |
| `model <file.pkmdl>` | meshes, skeleton, bounds |
| `bones <file.pkmdl>` | skeleton hierarchy |
| `dat <file.dat \| dir>` | item packs (a directory validates all) |
| `skydump <file.mpk>` | dome shells, UV ranges, material slots |
| `skytex <levelDir> <DataRoot>` | sky layer textures and whether they resolve |
| `shaders <DataRoot> [name]` | material scripts; one name prints it resolved |
| `lua <DataRoot> [frames] [level]` | boot the script layer (optionally load a level), tick, report native calls |
| `game <DataRoot> [level] [--shot f]` | script-driven windowed run: the game's Lua loads the level |
| `textures <file.mpk> <DataRoot> <hint>` | which map textures resolve |
| `resolve <DataRoot> <name>` | where a texture reference resolves |
| `texdump <DataRoot> <name> [out.tga]` | decode a texture, print corner pixels |

## Status

Asset formats, `.pak` reading (a vanilla install runs untouched), static world
rendering, portal/zone visibility, entity placement, layered skies, particle
effects, light coronas and world collision all work. The Lua 5.0.2 host boots
the real game scripts and **script-driven level loading works**:
`PainfulEngine game <DataRoot> <level>` has `Game:LoadLevel` read the level,
create its 631 entities through the native API, and the window renders what
the scripts built. Not yet: physics/sky/particles on the script path,
skeletal animation playback, water and post-processing, the player and AI,
sound and UI.

The full inventory, with the authority cited for each rule, is in
[`Docs/Status.md`](Docs/Status.md).

## Roadmap

Rendering comes first: it has a ground truth — the original renders the same
level, so screenshots can be compared directly — while gameplay correctness
only reveals itself deep into a playthrough.

1. Water (`FXWater`), which needs a render-target pass — shared with:
2. Bloom and the rest of the `.fxo` post chain.
3. Skeletal animation playback and GPU skinning.
4. The Lua host's natives, from
   [`Docs/native_priority.tsv`](Docs/native_priority.tsv) in call-count order
   — wiring `WORLD`/`ENTITY`/`CAM` to the real subsystems is the point where
   the game starts to *play*. The host itself is up: see
   [`Docs/LuaHost.md`](Docs/LuaHost.md).
5. The rest of physics — the player controller, ragdolls, explosions and glass
   — driven through that same native API. The Jolt world underneath it is up
   already: see [`Docs/Physics.md`](Docs/Physics.md).

## Documentation

| | |
|---|---|
| [`Docs/Source_Port.md`](Docs/Source_Port.md) | architecture, the porting plan, and the format findings |
| [`Docs/Status.md`](Docs/Status.md) | source layout, what works, what is missing |
| [`Docs/Particles.md`](Docs/Particles.md) | emitter formats and simulation |
| [`Docs/Billboards.md`](Docs/Billboards.md) | billboards, coronas and the occlusion trace |
| [`Docs/TextureTransforms.md`](Docs/TextureTransforms.md) | pan, tile and the detail-map transform |
| [`Docs/Water.md`](Docs/Water.md) | water surfaces, the material tiers and what each needs |
| [`Docs/Physics.md`](Docs/Physics.md) | the Jolt world, the tweak constants and the player body |
| [`Docs/LuaHost.md`](Docs/LuaHost.md) | the Lua 5.0.2 host, the native API shape, the boot and frame contract |
| [`Docs/Engine_API.md`](Docs/Engine_API.md) | the C++ surface of `Engine.dll` |
| [`Docs/Engine_LuaAPI.md`](Docs/Engine_LuaAPI.md) | the native API the scripts call |
| [`Docs/native_priority.tsv`](Docs/native_priority.tsv) | that API ranked by call count — the work queue |

## Third-party

- [SDL3](https://github.com/libsdl-org/SDL) — zlib licence
- [bgfx](https://github.com/bkaradzic/bgfx) via
  [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) — BSD 2-clause
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — MIT licence
- [miniz](https://github.com/richgel999/miniz) — MIT licence; the copy bundled
  inside bimg's tree, reused for `.pak` inflate
- [Lua 5.0.2](https://www.lua.org/versions.html#5.0) — MIT licence, vendored
  verbatim in `External/lua-5.0.2/`; the exact interpreter the game shipped
