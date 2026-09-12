# Painful Engine

A 64-bit, cross-platform recreation of **PainEngine**, the engine behind
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
`Painkiller.exe`, and run it. The `.pak` archives are read directly. Numbered
patch archives are layered the same way the original engine mounts them.

The original's `config.ini` keeps all of its settings. Settings that are new
with Painful Engine live in `painful_config.ini`, written beside the
executable on first run in the same style, one `Pf.Key = value` a line: the
widescreen HUD mode, the window mode (fullscreen, windowed or borderless) and
the shadow maps (the flashlight's, the models' from the level's directional,
and the placed lights': on/off, size, strength, how many lights) so far. In
the game the console sets them the way it sets the original's: `pfhudaspect
2`, written at once; Tab lists them and a bare `pfhudaspect` explains it. Logs go
beside it too: `painful.log`, with each line tagged by category, and
`painful_crash.log` after a crash. `PAINFUL_LOG=warn|info|trace` sets how much
reaches it.

## Tools

`PainfulTools.exe` holds the diagnostics. Each subsystem has a command that
loads its data and prints what it found, so a change can be checked without
opening a window. `PainfulTools help` lists them all; the list comes from the
same table the tool dispatches on, so it stays current.

```
PainfulTools level <DataRoot>/Levels/<name> <DataRoot>     what is in a level
PainfulTools physics <DataRoot>/Levels/<name> <DataRoot>   the Jolt world and probes of it
PainfulTools lua <DataRoot> 60 <name>                      boot the scripts, tick, report native calls
PainfulTools run <DataRoot>/Levels/<name> <DataRoot>       the free-camera viewer
```

## Documentation

Almost every rule in `Docs/` was recovered from the game, not guessed. Where a
guess was made and later proved wrong, the docs say so and say how it was
caught. That record is kept on purpose.

| | |
|---|---|
| [`Docs/Status.md`](Docs/Status.md) | what works, with the source of each rule |
| [`Docs/Plan.md`](Docs/Plan.md) | what is left, in order |
| [`Docs/Data/native_priority.tsv`](Docs/Data/native_priority.tsv) | ranks the native API by call count. It is the work queue. [`CLAUDE.md`]CLAUDE.md) has the project conventions. |
| [`Docs/Reference/Formats.md`](Docs/Reference/Formats.md) | the shipped binaries, `.pak`, and every decoded asset format |
| [`Docs/Reference/LuaHost.md`](Docs/Reference/LuaHost.md) | the Lua 5.0.2 host, the native API, the boot and frame order |
| [`Docs/Reference/Physics.md`](Docs/Reference/Physics.md) | the Jolt world, the tweak constants and the player body |
| [`Docs/Reference/PlayerMovement.md`](Docs/Reference/PlayerMovement.md) | the player mover, from `PhysicsObject::PlayerAction` |
| [`Docs/Reference/MonsterMovement.md`](Docs/Reference/MonsterMovement.md) | monsters are moved, not simulated, and how that was found |
| [`Docs/Reference/Animation.md`](Docs/Reference/Animation.md) | the animation clock, blending and the posed skeleton |
| [`Docs/Reference/Hitboxes.md`](Docs/Reference/Hitboxes.md) | per-limb hit volumes on the posed skeleton |
| [`Docs/Reference/Levels.md`](Docs/Reference/Levels.md) | what a level is made of, and writing one from code |
| [`Docs/Reference/Particles.md`](Docs/Reference/Particles.md) | emitter formats and simulation |
| [`Docs/Reference/Billboards.md`](Docs/Reference/Billboards.md) | billboards, coronas and the occlusion trace |
| [`Docs/Reference/Decals.md`](Docs/Reference/Decals.md) | impact marks and blood: the `.ini`, projection, fade, who spawns them |
| [`Docs/Reference/Lighting.md`](Docs/Reference/Lighting.md) | dynamic lights: the flashlight, carried torches, scripted flashes |
| [`Docs/Reference/TextureTransforms.md`](Docs/Reference/TextureTransforms.md) | pan, tile and the detail-map transform |
| [`Docs/Reference/Water.md`](Docs/Reference/Water.md) | water surfaces and the material tiers |
| [`Docs/Reference/Bloom.md`](Docs/Reference/Bloom.md) | the bloom post-process: threshold, kernel, gains, and where the port departs |
| [`Docs/Reference/DemonFx.md`](Docs/Reference/DemonFx.md) | Demon Morph: the black-and-white world, the fresnel monsters, the warp and the trail |
| [`Docs/Reference/Hud.md`](Docs/Reference/Hud.md) | the 2D layer: `MATERIAL`, `HUD.PrintXY`, fonts and the colour palette |
| [`Docs/Reference/Menu.md`](Docs/Reference/Menu.md) | the widget model behind `PMENU`, and the staging |
| [`Docs/Reference/Console.md`](Docs/Reference/Console.md) | the `~` console: panel, keys, `CONSOLE` natives, cheats |
| [`Docs/Reference/Sound.md`](Docs/Reference/Sound.md) | the mixer, the voice pool and the `SOUND` natives |
| [`Docs/Reference/Vectors.md`](Docs/Reference/Vectors.md) | the Vec3 type, its float[3] interop, what is converted and what a blanket sweep costs |
| [`Docs/Reference/Diagnostics.md`](Docs/Reference/Diagnostics.md) | checks vs ordinary answers, logging, truncated files, the PAINFUL_* switches, layering |

## Third-party

All dependencies use permissive, GPL-compatible licences and keep their own
terms. [`THIRD-PARTY.md`](THIRD-PARTY.md) lists them with the paths to their
licence texts.

- [SDL3](https://github.com/libsdl-org/SDL): zlib licence
- [bgfx](https://github.com/bkaradzic/bgfx), with bx and bimg: BSD 2-clause,
  built through [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) (CC0)
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics): MIT licence
- [miniz](https://github.com/richgel999/miniz): MIT licence, the copy inside
  bimg's tree, used for `.pak` inflate
- [Lua 5.0.2](https://www.lua.org/versions.html#5.0): MIT licence, vendored
  unchanged in `External/lua-5.0.2/`. This is the exact interpreter the game
  shipped with.
- [minimp3](https://github.com/lieff/minimp3): CC0, vendored header, used for
  music streams
