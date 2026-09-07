# PainfulEngine

An open reimplementation of PainEngine (Painkiller, 2004). The game's logic is
not compiled. It is Lua 5.0 scripts plus serialised property tables. So the
work is implementing the native API those scripts call, not redesigning the
game.

## Recover, don't guess

Every rule the engine follows comes from something the game shipped: the data,
the Lua scripts, or `Engine.dll` decompiled in Ghidra. Before reasoning out
what a native does, read it in the binary. Guesses that looked right have been
wrong here, and each one showed up levels later and cost more to find than a
check would have.

When a guess cannot be avoided, say so in the code and in the doc, and say
what would settle it.

## Docs are part of the change

`Docs/` has four kinds of file, maintained differently:

| | |
|---|---|
| `Docs/Reference/` | The recovered rules. Changes only when a new fact is recovered. |
| `Docs/Status.md` | What works today. Changes when a checklist item flips. |
| `Docs/Plan.md` | What is left, in order. |
| `Docs/Data/` | Generated. `native_priority.tsv` comes from `Tools/GenNativeList.ps1`. Never hand-edited. |

**A change to a recovered rule updates its `Docs/Reference/` page in the same
commit.** Not later, not in a follow-up.

**State each fact in one file and link to it from the others.** Status once
lived in four places and drifted in all of them. If something belongs in
Reference, the README and Status link to it instead of repeating it.

## Whitespace

Indentation is **tabs**, four columns wide. Nothing is padded with spaces to
line up with anything else - not declarations, not trailing comments, not
arguments under an opening bracket. A continuation sits **two tabs** past the
line that started the statement, so the file reads the same at any tab width.

Inside a comment or a string literal the spacing is the author's business and
is left alone - the tables and diagrams in the comment prose depend on it, and
a report's format strings are load-bearing. The vendored `External/` keeps
whatever upstream ships.

This applies to the C++, the shaders, the CMake and the scripts. There is no
formatter: clang-format cannot express it, because it breaks the compact
one-liners (`uint32_t u32() { uint32_t v = peekU32(p_); p_ += 4; return v; }`)
that several headers are built out of.

## Comments

Short and technical. A comment says what the code does and which constraint it
follows, in about three lines. The evidence (measured numbers, the hypothesis
that failed, per-asset tables) goes in the matching `Docs/Reference/` page,
and the comment points to it in one line. That record is worth keeping, just
not in the middle of a function.

Keep the address or the constant, drop the story:

```cpp
// Monster width = the SMALLER horizontal half-extent; the larger one is arms,
// not body. The sphere centre sits above the origin, because a .pkmdl origin is
// the model centre, not the soles. Rig measurements: Docs/Reference/MonsterMovement.md
```

**This applies to code you write or change.** Existing long comments are not a
backlog. Rewrite one when you are already editing that function, not as a
sweep. Older files still carry their full derivations inline; that is known and
is not a defect to fix in bulk.

## Failing loudly

A guard that hides a real bug is worse than a crash. Use
`PAINFUL_CHECK(cond, "what broke %d", x)` where a false condition means the
engine is wrong; leave the silent early return where it means the data is
ordinary (a bone-less prop, an unattached slot, a nil argument). It logs once
per site and tallies, so the reports end with `checks: none failed` or the
list. Docs/Reference/Diagnostics.md draws the line.

## Vectors

Maths uses `Vec3` and `Quat` (`Core/Vectors.h`) and `Mat4` (`Core/Matrix.h`).
Each has the layout of the float array it replaced and converts to
`const float*` implicitly, so they still mix with the arrays at the Lua, Jolt
and bgfx boundaries. `Quat` is engine order (w,x,y,z) and is the single
authority for the rotation convention: `Rotate` is `conj(q)*v*q`, so **`a * b`
applies `a` first**, the same order `EngineRot9Mul` uses.

Converting a site is not a free rename: check the epsilon first (`Normalized()`
rescales any non-zero length; some hand-written helpers had a floor), then prove
it with a before/after diff of a report that exercises it. Not every `float[4]`
is a rotation - most are bgfx uniforms or UV transforms, and converting one of
those breaks bgfx silently. **Never let a mechanical sweep touch the self-test**
- it is the one file whose `float[3]` are deliberate.

## Layout

One directory, one `CMakeLists.txt`, one target, one project in the IDE. The
layering goes one way, and `CMake/Layering.cmake` checks it at configure time
(the targets alone enforce it only at link time, which an upward relative
include slips past):

```
Core <- Assets <- World <- Render        Script beside Assets; Audio off Core
Game is the seam; App and Tools sit on top
```

Two executables: `PainfulEngine.exe` (the game) and `PainfulTools.exe` (the
reports and the `run` viewer). To add a report, add a row to the command table
in `Source/Tools/ToolsMain.cpp`. The help text is generated from it.

The full tree is in [`Docs/Status.md`](Docs/Status.md).

## Build

```
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release
```

`-DPAINFUL_DEPLOY_DIR=<game>/Bin` makes each build copy the executable into a
game folder. Output goes to `Build/Bin/<Config>/` for both executables and
`Build/Lib/<Config>/` for the libraries. Shaders are compiled per backend and
embedded, so the executable is the whole deliverable. A `Shaders/` folder next
to it still overrides them, which is how to test a shader without a rebuild.

To check a change without opening a window, use the reports. Every subsystem
has one:

```
PainfulTools level <DataRoot>/Levels/<name> <DataRoot>
PainfulTools lua <DataRoot> 60 <name>
PainfulTools selftest                     # Vec3, Quat, Mat4 and the conventions
PainfulTools traces                       # every PAINFUL_* switch, and what is set
```
