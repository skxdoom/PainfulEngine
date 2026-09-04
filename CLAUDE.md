# PainfulEngine

An open reimplementation of PainEngine (Painkiller, 2004). The game's logic is
not compiled — it is Lua 5.0 plus serialised property tables — so the work is
implementing the native API those scripts call, not rewriting the design.

## Recover, don't guess

Every rule the engine follows comes from something the game shipped: the data,
the shipped Lua, or `Engine.dll` decompiled in Ghidra. Before reasoning out what
a native means, read it in the binary. Heuristics that looked right have been
wrong here in ways that only surfaced levels later, and each one cost more to
find than it would have cost to check.

When a guess is unavoidable, say so where it lives — in the code, and in the
doc — and say what would settle it.

## Docs are part of the change

`Docs/` has three kinds of file and they are not maintained the same way:

| | |
|---|---|
| `Docs/Reference/` | The recovered rules. Durable: changes only when a new fact is recovered. |
| `Docs/Status.md` | What works today. Changes when a checklist item flips. |
| `Docs/Plan.md` | What is left and in what order. |
| `Docs/Data/` | Generated — `native_priority.tsv` comes from `Tools/GenNativeList.ps1`. Never hand-edited. |

**A change that alters a recovered rule updates its `Docs/Reference/` page in the
same commit.** Not afterwards, not in a follow-up.

**Never state a fact in two files — link instead.** Status lived in four places
once (two docs and two README sections) and drifted in all of them. If something
belongs in Reference, the README and Status link to it rather than summarising.

## Comments

Short and technical. A comment says what the code does and which constraint it
obeys, in about three lines. The *evidence* — measured numbers, the hypothesis
that was tried and failed, per-asset tables — goes in the matching
`Docs/Reference/` page, and the comment carries a one-line pointer to it. That
record is worth keeping; it is just not worth keeping in the middle of a
function.

Keep the address or the constant, drop the narrative:

```cpp
// Monster width = the SMALLER horizontal half-extent; the larger one is arms,
// not body. The sphere centre sits above the origin, because a .pkmdl origin is
// the model centre, not the soles. Rig measurements: Docs/Reference/MonsterMovement.md
```

**This governs code you write or change.** Existing long comments are not a
backlog to work through — rewrite one when you are already editing that function,
not as a sweep of its own. The older files still carry their full derivations
inline; that is a known state, not a defect to fix in bulk.

## Layout

One directory, one `CMakeLists.txt`, one target, one project in the IDE. The
layering is one-directional and the CMake targets enforce it:

```
Core <- Assets <- World <- Render        Script beside Assets; Audio off Core
Game is the seam; App and Tools sit on top
```

Two executables: `PainfulEngine.exe` (the game) and `PainfulTools.exe` (the
reports and the `run` viewer). Adding a report means adding a row to the command
table in `Source/Tools/ToolsMain.cpp` — the help text and the README's command
tables both generate from it.

The full tree is in [`Docs/Status.md`](Docs/Status.md).

## Build

```
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release
```

`-DPAINFUL_DEPLOY_DIR=<game>/Bin` makes each build copy the executable into a
game folder. Output is `Build/Bin/<Config>/` for both executables and
`Build/Lib/<Config>/` for the libraries. The shaders are compiled per backend
and embedded, so the executable is the whole deliverable; a `Shaders/` folder
beside it still overrides them, which is the way to test one without a rebuild.

Verify a change without opening a window: every subsystem has a report.

```
PainfulTools level <DataRoot>/Levels/<name> <DataRoot>
PainfulTools lua <DataRoot> 60 <name>
```
