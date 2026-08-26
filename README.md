# PainfulEngine

An open reimplementation of **PainEngine**, the engine behind *Painkiller* (2004).

Requires your own copy of the game's data. **No original assets or binaries are
distributed with this project.**

The formats it reads were reverse-engineered from the shipped game; the notes
are in the merged
[`Docs/Source_Port.md`](Docs/Source_Port.md).

## Why this is tractable

Painkiller's gameplay logic is not compiled — it lives in Lua scripts and
serialised property tables. A source port therefore means implementing the
*native API those scripts call* rather than rewriting the game's design. Of the
941 native functions recovered from `Engine.dll`, 790 are actually called by the
shipped scripts and just **113 cover 80% of all call sites**.

## Layout

Layering is one-directional and enforced by the CMake targets:

```
Core   <- Assets <- World <- Render
```

```
Source/
  Core/     Common.h/.cpp   Mat4, Reader, ReadFile
            Log.h
  Assets/   Mpk    world meshes (+ materials, lightmaps)
            Dat    item mesh packs (o.Pack containers)
            Pkmdl  models (geometry, skeleton, skin weights)
            Ani    skeletal animation
            Skeleton  hierarchy, bind matrices, skinning
            Properties  the Lua-ish property files
  World/    Level      level settings, entity placement, world mesh
            Templates  the BaseObj template chain under LScripts/Templates
  Render/   Window    SDL3
            Renderer  bgfx device
            WorldRenderer / EntityRenderer / SkyRenderer
  main.cpp
Shaders/    bgfx shader sources, compiled by the build into Shaders/ next to
            the executable
```

Headers sit beside their sources rather than in a parallel `include/` tree — it
halves the navigation cost and there is no public/private distinction to enforce.

## Building

Dependencies live in `External/` as git submodules and are built from source,
so nothing is installed system-wide.

```
git clone --recursive <this repo>
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The build also compiles the shaders (via bgfx's `shaderc`) whenever their
sources change. bgfx picks a backend automatically; on Windows that is
**Direct3D 11**.

## Usage

The executable is `build/Release/PainfulEngine.exe`, a windowed application:
double-clicking it opens no console.

**Recommended setup:** copy `PainfulEngine.exe` and the `Shaders/` folder into
the game's `Bin/` directory, next to the original `Painkiller.exe`. Launched
with no arguments it finds the sibling data directory on its own and opens the
first campaign level. (Currently the unpacked `Data_Extracted/` layout is
required; reading the `.pak` archives in `Data/` directly is planned.)

Run from a terminal for the developer commands:

```
PainfulEngine                                      find the game data and run
PainfulEngine run   <DataRoot/Levels/NAME> <DataRoot>   open a specific level
PainfulEngine run   ... --shot <file>              capture one frame and exit
PainfulEngine run   ... --skyview                  render only the sky
PainfulEngine level <DataRoot/Levels/NAME> <DataRoot>   headless report
PainfulEngine levels <DataRoot>                    list levels
PainfulEngine map   <file.mpk>                     inspect a world mesh
PainfulEngine model <file.pkmdl>                   inspect a model
PainfulEngine dat   <file.dat | directory>         inspect item packs
```

Controls: click to capture the mouse, WASD to move, shift for fast, space/ctrl
for up and down, `[` `]` to cycle levels, Escape to release the mouse and again
to quit.

Any of the 56 folders under `Data_Extracted/Levels` works as a level name.

## Status

Working:
- All major asset formats parse: world meshes (`.mpk`), models (`.pkmdl`),
  animations (`.ani`), item packs (`.dat`), property files, templates.
- Levels render end to end: world geometry at the level's authored scale,
  lightmaps, fog, translucency, and the engine's object-name semantics
  (portals, zones, barriers).
- Entities place with data-driven transforms (quaternion convention and the
  Scale rules recovered from `Engine.dll` and the shipped Lua).
- Layered animated skies composite per the engine's rules across all levels.
- Level cycling in-viewer; screenshot capture for automated comparisons.

Not yet implemented:
- Reading the `.pak` archives directly (extract to `Data_Extracted/` for now).
- Skeletal animation playback on placed models (formats parse; models render
  in bind pose).
- Lua host, physics (Jolt planned), audio, netcode.

## Third-party

- [SDL3](https://github.com/libsdl-org/SDL) — zlib licence
- [bgfx](https://github.com/bkaradzic/bgfx) via
  [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) — BSD 2-clause
