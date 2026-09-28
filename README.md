# Painful Engine

A 64-bit, cross-platform and faithful recreation of **PainEngine**, the engine behind
*Painkiller* (2004).

![](/pk.jpg)

**The project is at an early stage.** It launches the game
at playable state, with some bugs and occasional crashes. Some original functionality 
might not be implemented yet. See [`Docs/Status.md`](Docs/Status.md).

Tested only for the Steam version of **Painkiller: Black Edition** 1.64.

## Features

- Runs the **Painkiller** and **Battle Out Of Hell** single-player campaigns
- Matches the original's physics and gameplay feel as closely as possible
- Support for windowed and borderless modes
- Support for wide and ultrawide screen resolutions
- Shadows casted from flashlight and dynamic lights
- Basic Screen Space Ambient Occlusion
- Higher-resolution post-process effects and water reflections
- Loads original save files
- Minor bug fixes where the original had them

## Building

Needs CMake 3.20 or newer and a C++20 compiler. Dependencies are git submodules
or vendored in `External/` and build from source. Nothing is installed
system-wide, and no game data is needed to build.

```
git clone --recursive https://github.com/skxdoom/PainfulEngine.git
```

**Windows**, Visual Studio 2022 - the platform the engine is developed and
played on:

```
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release
```

**Linux and macOS** build from the same tree with any single-config generator:

```
cmake -S . -B Build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Build
```

Treat those two as untested. The platform code is there and guarded, and CI
compiles all three on every push, but nothing has been *played* on either yet.
On Linux the build needs the X11, Wayland, GL and audio development headers;
`.github/workflows/build.yml` lists the packages.

The build produces `PainfulEngine` under `Build/Bin/Release` with Visual
Studio, or `Build/Bin` with Ninja. Everything it needs is inside it - there is
one file to ship.

## Playing

Copy `PainfulEngine.exe` into the game's `Bin/` folder, next to the original
`Painkiller.exe`, and run it.

## Console commands

The engine's own settings, on top of the game's. Open the console with `~`,
type a command alone to see its value, or follow it with a new one:
`pfmode96 1`. Changes are saved to `painful_config.ini` beside the executable
and apply on the next frame, `pfwindowmode` at the next start or video mode
change. `pf` and Tab lists them all.

| Command | Default | What it does |
|---|---|---|
| `pfhudaspect` | `2` | aspect ratio of the interface: 0 stretched, 1 centred, 2 anchored by fifths |
| `pfwindowmode` | `0` | 0 default (set from config.ini), 1 windowed, 2 borderless |
| `pfflashlightshadows` | `1` | whether the flashlight casts shadows |
| `pfflashlightshadowmapsize` | `512` | flashlight shadow map size in texels |
| `pfcharactershadowsize` | `256` | character shadow size in texels (32 to 1024) |
| `pfcharactershadowstrength` | `60` | character shadow strength |
| `pfcharactershadowcasters` | `24` | characters with a shadow per frame, nearest first (the original's cap is 24; up to 64) |
| `pfshadowmapplacedlights` | `1` | whether the level's placed lights cast shadow maps |
| `pfshadowmapdynlights` | `1` | whether the dynamically created lights cast shadow maps |
| `pfshadowmapmaxlights` | `8` | lights with a shadow map per frame, up to 8 |
| `pfshadowmaplightsradius` | `40` | the radius in which a light gets a shadow map |
| `pfshadowmapsize` | `256` | light shadow map size in texels |
| `pfshadowmapstrength` | `80` | light shadow map strength |
| `pfviewmodelshadows` | `1` | whether the weapon view model casts self shadows |
| `pfviewmodelshadowmapsize` | `1024` | view model shadow map size in texels, for each of its four maps |
| `pfssao` | `1` | enables SSAO |
| `pfssaoscreenradius` | `40` | SSAO radius, thousandths of the screen's height at any distance |
| `pfssaointensity` | `500` | SSAO intensity |
| `pfbloomscale` | `2` | bloom is blurred at 1/N of the screen; the original is 2 |
| `pfbloomkernel` | `0` | bloom blur: 0 the Gaussian to three sigma, 1 the original 13 taps |
| `pfmode96` | `0` | mode 96: point-filtered textures, no post, no shadows, no specular |
| `pfmode96miplevel` | `2` | mode 96: the mip level every surface samples; higher is blockier |
| `pfmode96colors` | `16` | mode 96: levels per channel for the albedo, blue at half |

More in [`Docs/Reference/Console.md`](Docs/Reference/Console.md#pf).

## How it was done

The original **PainEngine** is closed source, so the only way to recreate it
and its trademark feel is to recover the rules from what the game shipped: the
data files, the Lua scripts, and the engine itself decompiled in Ghidra. That means
reading huge amounts of barely readable decompiled code, which AI is pretty
good at.

No decompiled output was re-used as is. It served only as a reference for what
the engine does. The implementation is written from scratch on a different
renderer, physics engine and audio stack. Ultimately it's just a passion project.

## Third-party

SDL, bgfx, Jolt Physics, Lua 5.0.2, minimp3, miniz and stb. All permissive,
and GPL-compatible.

[`THIRD-PARTY.md`](THIRD-PARTY.md) has the licence and the path for each.
