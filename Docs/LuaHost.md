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

## Script-driven level loading (Source/Game)

`ScriptEngine` is the seam where the natives meet the engine subsystems: the
entity registry behind `ENTITY.*` (integer handles - scripts store them in
`self._Entity`, key `EntityToObject` with them, and pass them back as every
native's first argument) and the world state `WORLD.*` accumulates. It is
headless-safe: with no renderer attached the registry still hands out real
handles, which is what `lua <DataRoot> [frames] [level]` exercises; the
windowed path attaches `EntityRenderer` and the same natives put things on
screen.

```
PainfulEngine game <DataRoot> [level] [--shot f]
```

boots the scripts, calls `Game:Init()` and `Game:LoadLevel(level)`, and
renders what they built: the map they asked for via `WORLD.LoadMap`, fog and
ambient from `WORLD.SetupFog`/`AmbientColor`, the layered sky through
`LoadSky`/`SetupSkyLayer` (the layer count is read out of the dome mesh's
own object names), light coronas through `BILLBOARD.SetupCorona`, particle
effects through `PARTICLE.AddEmitter`/`SetupEmitter` (the effect resolution
stays script-side in `LoadParticleFX`), and every entity the class scripts
created. On Cathedral: 958 files, **631 entities created by the scripts**
(571 live after the cache pass), zero script errors, from the archives or a
loose tree alike. The script path even runs MORE effects than the
hand-driven one - it creates the item-bound flames (`bindFX`) the batch
loader never resolved. Not yet on this path: sound, player.

**Physics is on this path.** `WORLD.LoadMap` builds the Jolt static world the
moment the scripts ask (entity bodies follow through `PO_Create` in the same
level load and need something to rest on), `WORLD.Init` sets the surface, and
`ENTITY.PO_Create(e, bodytype, scale, group)` creates each body bare - the
scripts then dress it through `PO_SetMass`/`PO_SetFriction`/
`PO_SetRestitution`/`PO_Set*Damping`, exactly the division of work
`CObject:PO_Create` writes out. A scale of -1 means "the entity's own scale".
Body shapes reuse the hand-driven path's rule: FromMesh variants become the
mesh's convex hull, the rest a sphere. Engine quaternions cross into Jolt as
their CONJUGATE - the row-vector/column-vector transpose, expressed on the
quaternion. Two traps cost real debugging time: `MapMesh::Load` reports
success as "no error recorded", so a reused mesh must be reset or a previous
failure poisons the next load; and settled bodies are ASLEEP, which the
active-only per-frame sync skips - one full sweep after the load-time settle
puts furniture where it came to rest.

`Game:LoadLevel`'s own pipeline (all script-side, Game.lua:933): find the
`.CLevel` via `FS.FindFiles`, `LoadObj` it, preload the level's templates,
`LoadObjectsDirectory` over the entity instance dirs, `Lev:Apply()`,
`GObjects:Apply()`, `SetupMapEntities` (binds `EMesh` scripts to named world
objects via `WORLD.FindEntityByName`). `LoadObj` works by setting the global
`o` and `DoFile`-ing the property file - instance files ARE Lua. It depends
on `FS.GetBaseObjInfo(path)`, which pre-scans the file for its `BaseObj`
line: instances reference template fields on line 1 while declaring the base
at the bottom, so the clone must happen before the file runs.

## Signatures recovered (authority: call sites + instrumentation)

```
WORLD.LoadMap("../Data/Maps/"..map, name, scale, overbright, rtCubeMap, shadowSize, shadowCount)
WORLD.Init(activeMeshesMassScale, defMeshFriction, defMeshRestitution, deactDelay, deactMaxPosDiff)
WORLD.SetupFog(mode, start, end, density, packedColor)   -- start/end pre-scaled by Cfg.ClipPlane
WORLD.SetFarClipDist(dist)
WORLD.AmbientColor(r, g, b, gunAmbientMultiplier)        -- 0-255
WORLD.SetDirLight(dx, dy, dz, packedColor, intensity)
WORLD.LoadSky(path) -> layerCount ; WORLD.AddEntity(e, hidden)
WORLD.FindEntityByName(name) -> handle
ENTITY.Create(etype, source, nameTagOrMesh, scale [, translateToZero]) -> handle
ENTITY.SetRotationQ(e, w, x, y, z) / GetRotationQ(e) -> w,x,y,z
ENTITY.GetVelocity(e) -> vx,vy,vz,speed
MDL.SetAnim(e, anim, loop, speed, blend, mcurve, hasMovingCurveRot) -> animIndex (<0 = missing)
MESH.SetDefaultDetailMaps(tex, tileU, tileV)
FS.FindFiles(pattern, wantFiles, wantDirs) -> {bare child names}
FS.GetBaseObjInfo(path)          -- sets o.BaseObj from the file
LANG.ParseLangFile(path)         -- calls Languages_ParseLangLine per line
Color:Compose() == R3D.RGBA(r,g,b,a)  -- packing is ours on both ends
```

## The player

`Game:OnPlay(true)` is the transition into gameplay - the `game` command
calls it after the level loads, the way the original's menu flow does. It
runs `CreatePlayerSP` -> **`CreatePlayer("player_box", false)`**, wraps the
returned handle in `CPlayer`, launches the level's OnPlay actions and sets
`Game.Active` - at which point the whole gameplay loop ticks: actors,
weapons (`CWeapon:Tick` polls `MDL.GetAnimTime`), and the pickup poll
(`PLAYER.GetDistanceFromPoint` against every item's takeDistance).

The pawn is ENGINE-side, as in the original: native code moves the player
from the `Tweak.PlayerMove` constants (PlayerSpeed 8.0, JumpStrength,
air-control...) and the scripts only read the results.
`Source/Game/PlayerPawn` rebuilds that on the collision world's queries - a
body sphere slid with gravity, ground detection and jumping, anchored at the
HEAD (which is what `ENTITY.PO_SetPawnHeadPos`/`PO_GetPawnHeadPos` address;
`PO_GetPawnFloorPos` reports the feet that the scripts' `_groundx/y/z`
track). `PO_Enable` on the player is the walk/fly switch, the scripts' own
`SwitchPlayerToPhysics` semantics; on a prop it wakes or sleeps the body.
The jump velocity and air-control curve are approximations pending the
native's own numbers. `ENTITY.GetDimensions` returns the model's world-space
size - the Slab ambush plates sink by their own height to hide, so it must
be real.

## Triggers and events

Level triggers come in two shapes, and both work:

- **CBox ambush triggers poll in pure Lua**: `AmbushForPlayer.CBox` and
  friends test `self:IsInside(PX, PY+1, PZ)` against the player-position
  globals every tick. The load-bearing detail: `Game:Tick` branches on
  `MOUSE.IsLocked()` - unlocked runs the EDITOR tick and `PX/PY/PZ` never
  update - so the mouse-lock natives must be truthful. With them in place,
  walking into an ambush box launches its `MonstersSpawnPoint`s and monsters
  spawn through the full `CActor` chain (`PO_SetMonsterType`,
  `PO_SetMonsterMovementConst`, `PO_SetSightParams`, ...).
- **Engine regions** (`CArea`, teleport boxes): `REGION.BuildFromPoint(e,
  points)` builds a volume (stored as the points' AABB), and
  `ScriptEngine::TickTriggers` posts **`REGION_ENTERED(region, enterer)`** /
  `REGION_LEFT` into `Game_GetMsg` on the transitions - `OnEnter` fires,
  which is teleports and checkpoints.
- The pawn posts **`PLAYER_HIT_GROUND(player, fallSpeed)`** on landings
  above the engine's threshold of 20 - fall damage is script-side
  (`OnHitGround`).

The camera tick (`Game:Tick2`) is script-driven in the original: it reads
`MOUSE.GetDelta`, accumulates `CAM.GetRawRotation`'s degrees, and steers
`CAM.SetPos`. While the C++ loop drives the camera, `GetRawRotation` mirrors
its state and `GetDelta` reports zero, making the script-side accumulation a
faithful no-op; handing the camera over entirely means feeding real deltas.
`INP.GetActionStatus(e)` returns the pressed-actions bitmask - zero until
real key bindings land.

The `lua` command's fourth argument runs an arbitrary chunk after
`Game:OnPlay`, which is how gameplay is tested headless:

```
PainfulEngine lua <DataRoot> 120 C1L1_Cathedral "ENTITY.SetPosition(Player._Entity,-142.7,8.1,-2.3)"
```

teleports the player into Cathedral's first ambush box and the monsters
spawn within the ticked frames.

## Input and the player action path

The player's controls are a script path in this engine, and the port now
takes it rather than going round it. `CPlayer:Tick` does:

```lua
local action = INP.GetActionStatus(self._Entity)   -- Actions bitmask
... overrides: weapon select, switched fire, rocket jump ...
ENTITY.PO_SetAction(self._Entity, self.CurAction)
PLAYER.ExecAction(self._Entity, 0, fv.X,fv.Y,fv.Z, rv.X,rv.Y,rv.Z)
```

and `PLAYER.ExecAction` is the entry to `PhysicsObject::PlayerAction`. So the
mover runs *inside* `Game_Tick`, on the mask the scripts built, and nothing
on the C++ side decides how the player moves. `Source/Game/Input` holds the
keys and the bindings; `PlayerPawn` is only the mover.

Recovered along the way:

- **Keys are Windows virtual-key codes.** `Definitions.lua`'s `Keys` is the
  standard VK list plus three the engine synthesises: Numpad Enter 252,
  Mouse Wheel Forward 253, Mouse Wheel Back 254.
- **`INP.Key(k)` is tri-state**: 0 up, 1 pressed this frame, 2 held.
  `Game.lua` pairs `==1` for a toggle with `==2` for a modifier held
  alongside it, so a boolean breaks both halves.
- **Bindings live in the scripts' own `Cfg` table**, as
  `Cfg.KeyPrimary<Action>` / `Cfg.KeyAlternative<Action>` holding engine key
  names ("Left Mouse Button", "Right Ctrl", "None"). `Cfg.lua` carries the
  defaults and `DoFile`s `config.ini` over them. `INP.LoadBindings()`
  therefore reads them straight back out of the Lua state, and the options
  menu calls it again after a rebind. The engine's key-name table sits in
  `Engine.dll` beside its short names ("LMB", "RCtrl", "WheelFwd").
- **`PlayerAction` builds the ground direction from the RIGHT vector alone**,
  deriving forward as `(right.z, -right.x)`. It is handed both vectors and
  uses the second. That is why looking up or down neither slows walking nor
  drives the player into the floor - the forward vector carries a Y
  component and is not used for this.
- **The angle convention differs from ours and it is load-bearing.** The
  scripts rebuild the whole movement basis from `CAM.GetAngRad` in pure Lua
  (`CPlayer:SetupAction`, via `Quaternion:New_FromEuler`). Reading their
  maths back out gives, at turn `a`, `right = (cos a, 0, -sin a)` and
  `forward = (-sin a, 0, -cos a)`: it starts down -Z and runs the opposite
  way round from our yaw. Matching it against our
  `right = (-sin yaw, 0, cos yaw)` gives **turn = -(yaw + pi/2)**, with the
  elevation passing through unchanged. `CAM.GetAng`/`GetAngRad`/
  `GetRawRotation` all apply it. Get it wrong and the player walks at ninety
  degrees to where the camera points. The check that settles it: run the
  scripts' own `SetupAction` round trip and compare the vector it produces
  against `CAM.GetForwardVector`, which computes the same basis in C++ -
  they agree to float epsilon.

`ENTITY.PO_SetAction` / `PO_AddAction` / `PO_IsActionState` keep the mask on
the entity, the way `PlayerAction` keeps it at `this+0x78`. The mover
consumes only the five movement bits (it masks to `0x3e`); the rest is the
scripts talking to themselves - which is how the weapon code learns that
fire was held this tick.

**The headless `lua` command now attaches physics, the pawn and the input
too.** Headless means no renderer, not a hollow game: the tick chain there is
the one the windowed run takes, so the call report measures the real thing
and gameplay can be tested without a window. Overriding a native from the
`exec` chunk is how a situation gets staged - `INP.GetActionStatus = function
(e) return Actions.Forward end` walks the player.

## Next stages

1. Camera handover: `MOUSE.GetDelta` fed with real motion, `CAM.SetPos` /
   `SetAng` / `SetPositionDisplacement` accepted, so `Game:Tick2` steers the
   view as the original does. The angle conversion above is its own inverse,
   which is what the handover needs.
2. Weapons: `WORLD.LineTrace` / `LineTraceFixedGeom` off Jolt, the
   intersection-solver registry, `ENTITY.SetPosAndRotRelativeToCamera` for
   the view model.
3. Animation and the actor clock - see
   [`Gameplay_Roadmap.md`](Gameplay_Roadmap.md) for the full order.
