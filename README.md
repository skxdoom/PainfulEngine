# PainfulEngine

An open reimplementation of **PainEngine**, the engine behind *Painkiller* (2004).

Requires your own copy of the game's data. **No original assets or binaries are
distributed with this project.**

Painkiller's gameplay logic is not compiled — it lives in Lua scripts and
serialised property tables. A source port therefore means implementing the
*native API those scripts call* rather than rewriting the game's design. Every
rule the engine follows is recovered from something the game shipped: the data,
the shipped Lua, or `Engine.dll` decompiled in Ghidra.

## Building

Dependencies live in `External/` as git submodules and build from source, so
nothing is installed system-wide.

```
git clone --recursive <this repo>
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The build also compiles the shaders, via bgfx's `shaderc` - once per graphics
backend, embedded in the executable and chosen at runtime, so there is one file
to ship. To have each build copy it into a game folder, set the deploy path once
(a local cache variable, never committed):

```
cmake -S . -B build -DPAINFUL_DEPLOY_DIR="X:/Painkiller/Bin"
```

## Playing

Copy `PainfulEngine.exe` into the game's `Bin/` directory, beside the original
`Painkiller.exe`, and run it. With no arguments
it finds the sibling data directory and opens the first campaign level. A
vanilla install works untouched — the `.pak` archives are read directly, with
numbered patch archives layered the way the original engine mounts them.

WASD to move, shift for fast, space/ctrl for up and down, `N` for noclip, `P`
for the collision wireframe, `[` `]` to cycle levels. **Escape opens the menu**,
which pauses the game and hands the mouse over.

```
PainfulEngine                            find the game data and play
PainfulEngine game <DataRoot> [level]    play a named level
```

## Tools

`PainfulTools.exe` is the diagnostics: every subsystem has a command that
resolves its data and prints what it found, so a change can be checked without
opening a window. `PainfulTools help` lists all 33 — generated from the same
table it dispatches on, so it cannot fall out of date.

```
PainfulTools level <DataRoot>/Levels/<name> <DataRoot>     what is in a level
PainfulTools physics <DataRoot>/Levels/<name> <DataRoot>   the Jolt world, and probes of it
PainfulTools lua <DataRoot> 60 <name>                      boot the scripts, tick, report native calls
PainfulTools run <DataRoot>/Levels/<name> <DataRoot>       the free-camera viewer
```

## Status

The engine plays: the shipped Lua loads a level, creates the player, and moves
him with the game's own constants. Skeletal animation, monsters, ragdolls,
sound, particles, physics and the HUD all run from the original scripts. Not
yet: water and post-processing, save/load, and multiplayer.

The inventory, with the authority cited for each rule, is in
[`Docs/Status.md`](Docs/Status.md). What is left, and in what order, is in
[`Docs/Plan.md`](Docs/Plan.md).

## Documentation

`Docs/Reference/` holds the recovered rules — durable, and changed only when a
new fact is recovered.

| | |
|---|---|
| [`Formats.md`](Docs/Reference/Formats.md) | the shipped binaries, `.pak`, and every asset format decoded |
| [`LuaHost.md`](Docs/Reference/LuaHost.md) | the Lua 5.0.2 host, the native API shape, the boot and frame contract |
| [`Physics.md`](Docs/Reference/Physics.md) | the Jolt world, the tweak constants and the player body |
| [`PlayerMovement.md`](Docs/Reference/PlayerMovement.md) | the player mover, recovered from `PhysicsObject::PlayerAction` |
| [`MonsterMovement.md`](Docs/Reference/MonsterMovement.md) | monsters are moved, not simulated — and how that was found |
| [`Animation.md`](Docs/Reference/Animation.md) | the animation clock, blending and the posed skeleton |
| [`Hitboxes.md`](Docs/Reference/Hitboxes.md) | per-limb hit volumes against the posed skeleton |
| [`Levels.md`](Docs/Reference/Levels.md) | what a level is made of, and writing one from code |
| [`Particles.md`](Docs/Reference/Particles.md) | emitter formats and simulation |
| [`Billboards.md`](Docs/Reference/Billboards.md) | billboards, coronas and the occlusion trace |
| [`TextureTransforms.md`](Docs/Reference/TextureTransforms.md) | pan, tile and the detail-map transform |
| [`Water.md`](Docs/Reference/Water.md) | water surfaces, the material tiers and what each needs |
| [`Hud.md`](Docs/Reference/Hud.md) | the 2D layer: `MATERIAL`, `HUD.PrintXY`, fonts and the colour palette |
| [`Menu.md`](Docs/Reference/Menu.md) | the retained widget model behind `PMENU`, and the staging |
| [`Console.md`](Docs/Reference/Console.md) | the `~` console: the panel, its keys, the `CONSOLE` natives and how the cheats reach `Console.lua` |
| [`Sound.md`](Docs/Reference/Sound.md) | the mixer, the voice pool and the `SOUND` natives |

[`Docs/Data/native_priority.tsv`](Docs/Data/native_priority.tsv) ranks the native
API by call count — the work queue. [`CLAUDE.md`](CLAUDE.md) has the conventions.

## Licence

Copyright © 2026 Dmitry Karpukhin.

PainfulEngine is **GPL-3.0-or-later** — see [`LICENSE`](LICENSE).

Copyleft is the deliberate choice rather than the default one. This is a
preservation and interoperability project, and the licence keeps it that way:
anyone may study, change and share it, and nobody may take it closed or sell
it on. That is also the norm among source ports of commercial games, for the
same reasons.

The licence covers this project's own code. It grants nothing over
*Painkiller* itself, which remains its rights holders'. No game data, assets
or binaries are included here, and running the engine requires your own copy
of the game.

*Painkiller* was made by People Can Fly. This project is not affiliated with
them or with the game's current rights holders, and exists to understand and
preserve the engine rather than to compete with it.

## Credits

Written by Dmitry Karpukhin, in collaboration with **Claude** (Anthropic),
which wrote a substantial share of the engine, the Ghidra analysis behind it
and these docs. Per-commit authorship is recorded in `Co-Authored-By`
trailers throughout the history.

Nearly every rule in `Docs/` was recovered rather than guessed. Where a guess
was made and later proved wrong, the docs say so and say how it was caught;
that record is deliberate, and worth keeping as the project grows.

## Third-party

Every dependency is permissive and GPL-compatible, and each stays under its
own terms — [`THIRD-PARTY.md`](THIRD-PARTY.md) has the details and the paths
to their licence texts.

- [SDL3](https://github.com/libsdl-org/SDL) — zlib licence
- [bgfx](https://github.com/bkaradzic/bgfx), with bx and bimg — BSD 2-clause;
  built through [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake), which
  is CC0
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — MIT licence
- [miniz](https://github.com/richgel999/miniz) — MIT licence; the copy bundled
  inside bimg's tree, reused for `.pak` inflate
- [Lua 5.0.2](https://www.lua.org/versions.html#5.0) — MIT licence, vendored
  verbatim in `External/lua-5.0.2/`; the exact interpreter the game shipped
