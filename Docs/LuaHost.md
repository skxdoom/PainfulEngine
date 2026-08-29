# The Lua host

Painkiller's gameplay is not compiled: it is Lua scripts driving a native API.
This layer reimplements that host. Everything here is recovered from the
shipped scripts, `Engine.dll`'s registration tables, or the binary's string
table — the authority is named for each rule.

## The interpreter is Lua 5.0.2, exactly

`External/lua-5.0.2/` vendors the interpreter verbatim from lua.org (MIT).
The version is load-bearing: the shipped scripts use 5.0-only forms —
`for i,v in <table> do` (the table-iteration compatibility mode), `table.getn`,
`math.mod`, `string.gfind` — that 5.1+ removed. `Engine.dll` statically links
this same version.

PCF extended their build (LuaPlus-derived, per comments in `Utils.lua`) with
`table.setconstant` / `table.isconstant`; `Natives.cpp` provides both. The
original enforces constness in the VM; ours records the mark in a weak-keyed
registry so `isconstant` answers truthfully — a read-only metatable would
change behaviour, because `Clone()` and the inheritance helpers branch on
`getmetatable(t)`.

## Boot sequence (authority: Loader.lua, Engine.dll strings)

1. The engine runs `LScripts/Loader.lua`. It sets `path = "../Data/LScripts/"`
   (paths are relative to `Bin/`) and `DoFile`s the type classes
   (Vector/Quaternion/Color/Collection), the entity classes (`CObject`,
   `CItem`, `CActor`, ... — then `Inherit`s each from `CObject`), the game
   logic (`Game.lua`, `GameMP.lua`), localisation, HUD, menu and editor
   scripts. 68 files on retail data.
2. The engine then calls `Game:Init()`. It **gates on
   `GetPainkillerVersionString()` and `GetEngineVersionString()` both being
   exactly `"1.4"`** (the internal version pair, not the marketing patch
   number) and refuses to run otherwise. Init loads Cfg/Tweak, preloads every
   template under `LScripts/Templates` (1054 files, via `FS.FindFiles`),
   creates the empty `"NoName"` level through `Game:NewLevel` → `Lev:Apply()`,
   loads HUD data and key bindings, and applies settings.
3. Per frame, the engine calls (names from `Engine.dll`'s string table,
   definitions at `Game.lua:1549`):

   | call | when |
   |---|---|
   | `Game_Tick(delta)` | before physics |
   | `Game_Tick2(delta)` | after physics, before world tick |
   | `Game_Tick3(delta)` | after world tick |
   | `Game_Render(delta)`, `Game_PostRender(delta)` | render hooks |
   | `Game_GC()` | drives `collectgarbage` with per-mode thresholds |

4. Engine events go through **`Game_GetMsg(msg, ...)`** — the message pump
   (`EXPLOSION`, `ENTITY_CREATE`, `ENTITY_DELETE`, `PO_CREATE`,
   `REGION_ENTERED`, `COLLISION_WITH_OTHER_ENTITY`, ...). Console input goes
   through `Hud_OnConsoleCommand` / `Hud_OnConsoleTab` / `Hud_OnSayToAll` /
   `Hud_OnSayToTeam`.

`LuaHost` mirrors this: `Boot()` runs the loader, `CallGameInit()` makes the
method call, `FrameTick(delta)` issues the five per-frame calls plus GC in the
engine's order, `PostMsg(msg)` feeds the pump.

## The native API shape

All natives are **plain functions in global module tables, taking an entity
handle as their first argument** — C-style, no userdata methods, no metatable
dispatch:

```lua
self._Entity = ENTITY.Create(ETypes.Model, self.Model, self._Name..":Script", self.Scale*0.1)
ENTITY.SetVelocity(self._Entity, x, y, z)
local i = MDL.SetAnim(self._Entity, "idle", true)
```

(Note the `Scale*0.1` — the model-scale rule found empirically in the renderer
is written in the scripts.)

The 941 recovered natives (`Docs/Engine_LuaAPI.md`) are grouped by
registration table; the binary stores only table addresses, so the Lua-visible
names were recovered by majority vote over the shipped scripts'
`PREFIX.Function` usages (`Tools/GenNativeList.ps1` →
`Source/Script/NativeList.inc`). The vote is decisive everywhere it matters:

| table | name | | table | name |
|---|---|---|---|---|
| Menu/GUI (146) | `PMENU` | | World mesh/collision (140) | `ENTITY` |
| World/level (73) | `WORLD` | | Ragdoll/joints (72) | `MDL` |
| Render/debug (40) | `R3D` | | Network (39) | `NET` |
| Sound system (31) | `SOUND` | | Input/timing (28) | `INP` |
| Havok bodies (24) | `PHYSICS` | | Filesystem (18) | `FS` |
| Camera (18) | `CAM` | | Materials/textures (18) | `MESH` |
| Console (17) | `CONSOLE` | | Scoreboard (15) | `MPSTATS` |
| Waypoints (15) | `WPT` | | HUD drawing (14) | `HUD` |
| Player/bots (14) | `PLAYER` | | Sound instance (14) | `SOUND3D` |
| Mouse (10) | `MOUSE` | | + `LIGHT`, `PARTICLE`, `ENVIRONMENT`, `MBOARD`, `MATERIAL`, `GAMESPY`, `SND`, `PATH` | |

`SOUND2D`, `EDITOR` and `LANG` exist in scripts but not in the recovered
tables; they are created as auto-stub modules. Every module table carries an
`__index` that materialises a logging stub for any unlisted name, so a
mismapped native surfaces in the call report instead of as "attempt to call
nil". Bare globals (49, registered individually) include `DoFile`, `Log`, the
quaternion/vector helpers (flat multi-value: `EulerToQuat(ax,ay,az) →
w,x,y,z`, engine order `(w,x,y,z)`), and the build/edition/CD-check flags.

## The instrumentation loop

Unimplemented natives are stubs that log their first three calls with
formatted arguments and count the rest; they return **no values** (nil is
falsy, so the `Is*`/`Get*` family reads as "no" by default, and any call site
that genuinely needs a value errors loudly — which names the next native to
implement). The loop:

```
PainfulEngine lua <DataRoot> [frames]
```

boots, calls `Game:Init()`, ticks N frames, and prints the report. Current
state: **68 files, 0 script errors, from either the .pak archives or a loose
tree**; 56 distinct natives still stubbed on that path.

Natives with real implementations so far: the script loader (`DoFile`,
`DoString`, VFS-aware `loadfile`/`dofile`), `LANG.ParseLangFile` (native reads
the file, the parse rules stay Lua-side in `Languages_ParseLangLine` — the
original's split), `FS.FindFiles` (returns bare child names, non-recursive,
FindFirstFile mask semantics where `*.*` matches everything),
`R3D.RGB/RGBA`, `INP.GetTime/GetTimeMultiplier`, `R3D.ScreenSize`,
`MOUSE.GetPos`, the bit-flag family, the quaternion/vector helpers, and the
version/edition/CD flags. `WORLD.LoadSky`/`LoadLowQualitySky` return a real
layer count of 0 until the renderer is wired in.

## Signatures recovered by instrumentation (so far)

```
WORLD.LoadMap("../Data/Maps/<map>", levelName, scale, bool, bool, 128, 24)
WORLD.Init(1, 0.5, 0.5, 5, 0.3)
WORLD.SetupFog(mode [, start, end, density, packedColor])
WORLD.SetFarClipDist(dist)
WORLD.BloomFXParams(0.25, 1, <packedColor>, 0.8)
WPT.Load("../Data/Maps/<map>")
ENTITY.Create(etype, model, "name:tag", scale) -> handle
FS.FindFiles(pattern, wantFiles, wantDirs) -> {names}
LANG.ParseLangFile(path) -- calls Languages_ParseLangLine per line
```

## Next stages

1. Wire `WORLD.*` / `ENTITY.*` / `CAM.*` to the existing renderer, level and
   physics subsystems, so `Game:LoadLevel` drives the same pipeline `run`
   drives by hand today.
2. `Game_GetMsg` events from physics (collisions, regions) and
   `CreatePlayer` + the player controller.
3. `PMENU` against a real menu renderer; `SOUND` when audio lands.
