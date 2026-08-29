# Third-party components

PainfulEngine itself is GPL-3.0-or-later (see [`LICENSE`](LICENSE)). It builds
against the components below, each of which stays under its own terms. All of
them are permissive and GPL-compatible, so the combined work may be
distributed under the GPL while these notices are preserved.

Licence texts live with the code — the paths below are where each one ships —
and nothing here is relicensed by this project.

| Component | Licence | Where it comes from |
|---|---|---|
| **Lua 5.0.2** | MIT | Vendored in `External/lua-5.0.2/` |
| **SDL** | zlib | Submodule `External/SDL` |
| **bgfx**, **bx**, **bimg** | BSD 2-clause | Submodules under `External/bgfx` |
| **bgfx.cmake** | CC0 1.0 | Submodule `External/bgfx` (the build wrapper) |
| **miniz** | MIT | Header-only, from `External/bgfx/bimg/3rdparty/tinyexr/deps/miniz` |
| **Jolt Physics** | MIT | Submodule `External/JoltPhysics` |

Everything except Lua is a git submodule, so only a reference is stored here;
run a recursive clone to fetch them.

## Lua is vendored, and deliberately

`External/lua-5.0.2/` holds the interpreter's source verbatim from lua.org,
including its `COPYRIGHT` file, which must stay with it. The version is
load-bearing rather than incidental: the shipped game scripts use 5.0-only
forms — the generic `for k,v in <table> do`, `table.getn`, `math.mod`,
`string.gfind` — that 5.1 removed, and `Engine.dll` statically links exactly
this interpreter. A newer Lua does not run Painkiller's scripts.

## What this project is not

No Painkiller data, assets, or binaries are included, and none may be
redistributed with it. Running the engine requires your own copy of the game.

The rules the engine implements were recovered from the shipped data, the
shipped Lua scripts, and analysis of `Engine.dll`; the documentation records
addresses, constants and data layouts as factual descriptions of those
interfaces. No decompiler output is kept in this repository, and none should
be added to it.
