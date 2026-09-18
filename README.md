# Painful Engine

A 64-bit, cross-platform and faithful recreation of **PainEngine**, the engine behind
*Painkiller* (2004).

You need your own copy of the game. This project ships no game assets or
binaries.

**The project is at an early stage.** It launches the game
and it's playable, with some bugs and occasional crashes. See
[`Docs/Status.md`](Docs/Status.md).

## Goals (all on-going)

- Run the **Painkiller** and **Battle Out Of Hell** single-player campaigns
- Match the original's physics and gameplay feel as closely as possible
- Windowed and borderless modes
- Widescreen resolutions
- Higher-resolution post-process effects and water reflections
- Bug fixes where the original had them
- Further graphics improvements

## Why it's being made with AI

The original **PainEngine** is closed source. Therefore the only viable way 
to recreate this engine and its trademark feel is decompiling it and reading 
huge chunks of unreadable decompiled code, which AI is pretty good at. 
Ultimately it's just a passion project.

Worth noticing that no decompiled output was re-used as is. It was only used
as a reference.

## Building

Dependencies are git submodules in `External/` and build from source. Nothing
is installed system-wide.

```
git clone --recursive <this repo>
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release
```

Shaders are compiled by bgfx's `shaderc` once per graphics backend and embedded
in the executable, so there is one file to ship. To copy it into a game folder
after every build, set the deploy path once (a local cache variable, not
committed):

```
cmake -S . -B Build -DPAINFUL_DEPLOY_DIR="X:/Painkiller/Bin"
```

## Playing

Copy `PainfulEngine.exe` into the game's `Bin/` folder, next to the original
`Painkiller.exe`, and run it.

## Third-party

All dependencies use permissive, GPL-compatible licences and keep their own
terms. [`THIRD-PARTY.md`](THIRD-PARTY.md) lists them with the paths to their
licence texts.

| Component | Licence | Where it comes from |
|---|---|---|
| **Lua 5.0.2** | MIT | Vendored in `External/lua-5.0.2/` |
| **SDL** | zlib | Submodule `External/SDL` |
| **bgfx**, **bx**, **bimg** | BSD 2-clause | Submodules under `External/bgfx` |
| **bgfx.cmake** | CC0 1.0 | Submodule `External/bgfx` (the build wrapper) |
| **miniz** | MIT | Header-only, from `External/bgfx/bimg/3rdparty/tinyexr/deps/miniz` |
| **Jolt Physics** | MIT | Submodule `External/JoltPhysics` |
| **minimp3** | CC0 1.0 | Vendored header in `External/minimp3/` (music streams) |