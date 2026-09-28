# Painful Engine

A 64-bit, cross-platform and faithful recreation of **PainEngine**, the engine behind
*Painkiller* (2004).

**The project is at an early stage.** It launches the game
at playable state, with some bugs and occasional crashes. See
[`Docs/Status.md`](Docs/Status.md).

Tested only for the Steam version of **Painkiller: Black Edition** 1.64.

## Features

- Runs the **Painkiller** and **Battle Out Of Hell** single-player campaigns
- Matches the original's physics and gameplay feel as closely as possible
- Support for windowed and borderless modes
- Support for wide and ultrawide screen resolutions
- Shadows casted from flashlight and dynamic lights
- Higher-resolution post-process effects and water reflections
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
