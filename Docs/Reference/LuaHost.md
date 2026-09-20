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
   `Hud_OnSayToTeam` ([`Console.md`](Console.md)).

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

The 941 natives recovered from `Engine.dll` (the listing is kept with the RE
material, outside this repository) are grouped by registration table; the
binary stores only table addresses, so the Lua-visible names were recovered by
majority vote over the shipped scripts'
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

Nine module names the vote could not derive (`kExtraModules` in `Natives.cpp`:
`SOUND2D`, `EDITOR`, `LANG`, `BILLBOARD`, `MENU`, `VARRAY`, `REGION`, `FOGVOL`,
`FLOOR`) exist in scripts but not in the recovered tables; they are created
beside them, and most now carry real natives. Every module table carries an
`__index` that materialises a logging stub for any unlisted name, so a
mismapped native surfaces in the call report instead of as "attempt to call
nil". Bare globals (49, registered individually) include `DoFile`, `Log`, the
quaternion/vector helpers (flat multi-value: `EulerToQuat(ax,ay,az) →
w,x,y,z`, engine order `(w,x,y,z)`), and the build/edition/CD-check flags.


## The native families

`Source/Game/Script*.cpp`, one family each. Adding a native touches only its
own file.

Every native used to be a `static int L_*` member declared in
`ScriptEngine.h`, with one table in `ScriptBind.cpp`. Twenty translation
units include that header through `ScriptEngineInternal.h`, so adding a
single native recompiled 30 of them. It is 1 now.

Each family is:

```cpp
struct SoundNatives : ScriptNativesBase {     // in ScriptSound.cpp
    static int L_SND_Play(lua_State* L);
    ...
};

void BindSound(ScriptEngine& engine, LuaHost& host) {
    const ScriptNative natives[] = {
        {"SND", "Play", SoundNatives::L_SND_Play},
        ...
    };
    RegisterFamily(engine, host, natives);
}
```

`ScriptEngine::Bind` calls the eighteen binders and nothing else.

**Why the structs and not free functions.** The bodies reach engine state
through `self->` 632 times, so they need private access. A struct can be a
friend; a file-local free function cannot. `ScriptEngine.h` therefore carries
one `friend struct XNatives;` line per family — eighteen lines in place of
several hundred declarations.

**Why `ScriptNativesBase`.** As members the bodies used class-scope names
unqualified: `From`, `Entity`, the `EType` enumerators, `Route`, `LimbHit`,
`AnimSlotArg`, `TraceCommon`, `kLimbHandleBase`. The base forwards them, so
every body moved **unchanged** — no edit to recovered logic. Friendship is
not inherited, which is why each family still needs its own friend line rather
than the base covering them all.

**One call had to go first.** `ScriptPlayer` called `L_GetPosition` from
`ScriptEntity`; it now pushes the entity position directly, with the same
zeroes for a handle that is not one. That left the partition clean — no family
references another's natives.

Verifying a change here: a native dropped from a binder silently becomes an
auto-stub (`ModuleAutoIndex`), so it appears in the `lua` report's
unimplemented list. Diffing that list against a baseline is the check that
none went missing — the row count before equals the row count after.

## A missing native that INVERTS a test is worse than one that does nothing

An unimplemented native returns nothing. In Lua that is `nil`, and `nil` is
falsy — so any script that *tests* the result takes a branch it would never have
taken, confidently and silently. This has now cost four bugs, every one of them
found from the symptom rather than from the stub:

| native | the test | what the absence did |
|---|---|---|
| `ENTITY.GetChildByName` | `if quad ~= 0` | `nil ~= 0` is TRUE, so the player was holding every powerup and the damage loop ran on every shot |
| `ENTITY.KillAllChildrenByName` | `if KillAllChildrenByName(se,"stakeflame")` | returned nothing, so a burning stake never smoked |
| `PHYSICS.IsHavokBodyInWorld` | `if not IsHavokBodyInWorld(he) then he = nil end` | cleared the body handle on EVERY hit, so no weapon could shove an intact prop |
| `ENTITY.GetPtrByIndex` | `if not GetPtrByIndex(self._Entity) or timer <= 0` | read as "this entity is gone", so the electro shuriken detonated on its first tick |

The shape is always the same and it is worth searching for directly rather than
waiting for the symptom. Diff the natives the engine binds against the ones a
script family calls, then grep the unbound ones for a use inside `if` / `while`
/ `and` / `or` / `not`:

```
bind list:  grep -ohE '{"[A-Z0-9_]+", *"[A-Za-z0-9_]+"' Source/Game/Script*.cpp
call list:  grep -rhoE '\b(ENTITY|WORLD|MDL|PHYSICS|...)\.[A-Za-z0-9_]+' <scripts>
```

Run over the weapon scripts that was 52 unbound natives, of which **six** appear
inside a condition — a far shorter list to reason about than 52, and it is the
one that produces visible bugs rather than missing features.

Two corollaries:

- **A stub that returns a plausible value is not safer than one that returns
  nothing.** It is worse, because it cannot be told apart from a real answer.
  Prefer implementing the handful that are tested.
- **Turning a stub on will expose bugs behind it**, in the code that finally
  runs for the first time. Implementing `SND.Play` immediately surfaced two
  latent bugs in child management, one of them an access violation that had
  simply never been reachable.

## The instrumentation loop

Unimplemented natives are stubs that log their first three calls with
formatted arguments and count the rest; they return **no values** (nil is
falsy, so the `Is*`/`Get*` family reads as "no" by default, and any call site
that genuinely needs a value errors loudly — which names the next native to
implement). The loop:

```
PainfulTools lua <DataRoot> [frames] [level] [exec]
```

boots, calls `Game:Init()`, ticks N frames, and prints the report. It runs
with 0 script errors from either the .pak archives or a loose tree; the
current stub figures are in [`Stubs.md`](../Stubs.md).

Two rules from the first natives: `LANG.ParseLangFile` reads the file
natively while the parse rules stay Lua-side in `Languages_ParseLangLine`
(the original's split), and `FS.FindFiles` returns bare child names,
non-recursive, with FindFirstFile mask semantics where `*.*` matches
everything.

## The singleplayer no-ops

Five names were 43% of every stub call in a combat run and none of them has work
to do without a network layer. They are implemented as what the binary does
rather than as placeholders, so the report loses the noise and not the signal:

| native | in the binary | here |
|---|---|---|
| `CONSOLE.DemoIsPlaying()` | `0x10027bd0`: `gDemoRec && *gDemoRec == 2` | false — no recorder ([Console.md](Console.md)) |
| `NET.IsPlayingRecording()` | `0x10121db0` pushes a **constant** 0 | false, which is the shipped answer too |
| `PLAYER.SetMPByte(e, v)` | `0x101391d0` → `PhysicsObject::SetMPByte`, truncated to a uchar, dropped when the entity has no body | stored on the entity |
| `PLAYER.GetMPByte(e)` | `0x10139120` pushes the byte, and **0** when there is no body | the stored byte, 0 by default |
| `ENTITY.EnableNetworkSynchronization(e, on = true, b = false, a4 = 0, a5 = 255, a6 = 0)` | `0x1012f880` → `Entity::SetSynchroState(flags, a4, a5, a6)` with `flags = on ? (b ? 8 : 0) + 1 : 0`, and `(0, 0, 255, 0)` when off | validates the handle and returns |
| `ENTITY.SetSynchroString(e, s)` / `GetSynchroString(e)` | `0x1012f980` / `0x1012fa20`, `Entity+0x630`, `""` when unset | stored, and answered |

Two of them are not free no-ops:

- **`GetSynchroString` must answer `""`, not nil.** `Game_GetMsg`'s client
  collision path reads `if str ~= "" then tmp = FindObj(str) end`, and
  `nil ~= ""` is true — the inverted-test shape above.
- **The MPByte pair is not multiplayer-only, despite the name.** `CPlayer:Tick`
  writes it unconditionally (`CPlayer.lua:646`) and `PPlayerAnimation:Tick`
  reads it back as the animation state, so the third-person player body needs
  it. Its companion `PLAYER.GetPitch` is still a stub, and that one *divides*:
  `PLAYER.GetPitch(e) / -(32767*0.75)` raises the moment that process ticks,
  which is why nothing has reached `GetMPByte` yet.

The deviation to remember: the original keeps the byte on the `PhysicsObject`
and answers 0 for an entity that has none. Here it is a field on the entity, so
a bodyless one remembers what was written. No shipped script reads the byte for
anything but the player, which has a body either way.

## Script-driven level loading (Source/Game)

`ScriptEngine` is the seam where the natives meet the engine subsystems: the
entity registry behind `ENTITY.*` (integer handles - scripts store them in
`self._Entity`, key `EntityToObject` with them, and pass them back as every
native's first argument) and the world state `WORLD.*` accumulates. It is
headless-safe: with no renderer attached the registry still hands out real
handles, which is what `lua <DataRoot> [frames] [level]` exercises; the
windowed path attaches `EntityRenderer` and the same natives put things on
screen.

### Handles

A handle is the entity's slot in the world's array, and a save is only portable
if ours are numbered like the original's. `World::Release` (0x1005f160; what
`WORLD.Release()` and `WORLD.Release(true)` call) deletes every entity and sets
the count back to 1. `World::LoadMeshPak` (0x1005e8b0) then makes every map
object an entity, in file order, before any script runs: `World::AddEntity`
(0x1005dbf0) numbers an entity that has no handle yet. Zones, portals and
antiportals are classes of their own and get none. `WORLD.LoadMap` reserves the
same handles (`objectHandles_`). Active meshes and water take their object's
handle, and `WORLD.FindEntityByName` (0x1013dd70) hands one out on first ask.
Otherwise it answers with the newest entity of that name, compared without case,
or 0. `WORLD.Release(false)` is `ReleaseWithoutMap` (0x1005dc80): only what the
scripts made goes, and the count keeps going.

The class is read from the name (`zone`, `portal`, `antyp`). Measured by joining
`EntityToObject` at a fresh level start with an original save's: every script
entity's handle matches, 390 of 390 on C3L1_Train_Station and 279 of 279 on
C1L2_Atrium_Complex. Before the reservation they were 508 and 235 short, which is
the two maps' 511 and 235 entity objects less Train Station's 3 active meshes. A
pack with another class, or an object the parser skips, would shift the count;
the class field in the pack would settle that.

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
created. On Cathedral: 964 files, **796 entities created by the scripts**
(655 live after the cache pass), from the archives or a loose tree alike.
The script path even runs MORE effects than the hand-driven one - it creates
the item-bound flames (`bindFX`) the batch loader never resolved. Sound, the
player and the camera are all on this path now; what is still missing from it
is listed in [`../Status.md`](../Status.md).

**Physics is on this path.** `WORLD.LoadMap` builds the Jolt static world the
moment the scripts ask (entity bodies follow through `PO_Create` in the same
level load and need something to rest on), `WORLD.Init` sets the surface, and
`ENTITY.PO_Create(e, bodytype, scale, group)` creates each body bare - the
scripts then dress it through `PO_SetMass`/`PO_SetFreedomOfRotation`/
`PO_Set*Damping`, exactly the division of work `CObject:PO_Create` writes out
(`PO_SetFriction` / `PO_SetRestitution` are engine-side values that never reach
the solver — see [`Physics.md`](Physics.md)). A scale of -1 means "the
entity's own scale".
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
WORLD.FindEntityByName(name) -> handle, or 0
ENTITY.Create(etype, source, nameTagOrMesh, scale [, translateToZero]) -> handle  -- true: the pack mesh centred on its bbox (CenterGeometry 0x101D6F80)
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
`Source/Game/PlayerPawn` is the recovered `PhysicsObject::PlayerAction` run on
`PhysicsWorld` queries ([`PlayerMovement.md`](PlayerMovement.md)), anchored at the
HEAD (which is what `ENTITY.PO_SetPawnHeadPos`/`PO_GetPawnHeadPos` address;
`PO_GetPawnFloorPos` reports the feet that the scripts' `_groundx/y/z`
track). `PO_Enable` on the player is the walk/fly switch, the scripts' own
`SwitchPlayerToPhysics` semantics; on a prop it wakes or sleeps the body.
`ENTITY.GetDimensions` returns the model's world-space
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

  **Entry is a body overlap, not a point.** The engine builds these through
  `PhysicsWorld::CreateRegionFromPoints`, so a region is a physics volume and
  the player enters it by *touching* it. `TickTriggers` tests the pawn's
  extent - feet to head, widened by its radius - against the region's AABB.

  A single point at `feet + 1` (the script-side `CBox:IsInside(PX, PY+1, PZ)`
  convention) is the wrong model, and the jump pads are what proved it.
  `JumpPad:OnCreateEntity` sizes its region from the pad model and centres it
  `0.8` above the pad, so on DM_Cursed it spans `-13.688 .. -11.409`; a player
  standing on the pad has feet at `-12.397`, putting `feet + 1` at `-11.397`.
  It missed the volume by **12 millimetres**, and every jump pad in the game
  was inert. The pad's own dimensions were never the problem - they measure
  correctly at load.
- The pawn posts **`PLAYER_HIT_GROUND(player, -fallSpeed)`** on landings
  above the engine's threshold of 20 (`PlayerAction` negates it, and
  `OnHitGround` tests `speed < -min`) - fall damage is script-side
  (`OnHitGround`).

The camera tick (`Game:Tick2`) is script-driven in the original: it reads
`MOUSE.GetDelta`, accumulates `CAM.GetRawRotation`'s degrees, and steers
`CAM.SetPos` ("The camera is the scripts'" below).

The `lua` command's fourth argument runs an arbitrary chunk after
`Game:OnPlay`, which is how gameplay is tested headless:

```
PainfulTools lua <DataRoot> 120 C1L1_Cathedral "ENTITY.SetPosition(Player._Entity,-142.7,8.1,-2.3)"
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
  `right = (-sin yaw, 0, cos yaw)` gives **turn = yaw + pi/2** (the sign and
  the negated elevation are settled under "The rotation conventions" below). `CAM.GetAng`/`GetAngRad`/
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

## The play transition, and why the mouse lock seats the player

A level LOADS with the mouse unlocked, and that is not a detail.
`CLevel:Synchronize` branches on it:

```lua
if not MOUSE.IsLocked() then
    CAM.SetAng(self.Ang.X,self.Ang.Y,self.Ang.Z)   -- level -> camera
    CAM.SetPos(self.Pos.X,self.Pos.Y,self.Pos.Z)
else
    self.Ang:Set(CAM.GetAng())                     -- camera -> level
    self.Pos:Set(CAM.GetPos())
end
```

So `Lev.Pos` is the level's start camera, pushed out into the engine while
loading; once play begins the camera is authoritative and the level record
follows it. `Game:CreatePlayerSP` then seats the player at `Lev.Pos`.

Start locked and the synchronise runs backwards: `Lev.Pos` is overwritten
with wherever our camera happens to be before `CreatePlayerSP` ever reads it,
and the player spawns at the world origin and falls. Cathedral's `.CLevel`
declares `o.Pos = (-315.106, -2.36039, -2.90619)`; the symptom was reading
`Lev.Pos` back as `(0,0,0)` with `Lev.Ang.X` at exactly -90, which is our own
zero-yaw camera converted into the engine's turn convention - the giveaway
that the level was recording us rather than seating us.

The order, which `Game_DemoLoadLevel` spells out and the host now follows:
load the level (unlocked, so it seats the camera) → adopt that pose → lock
the mouse → `Game:OnPlay` → **`Game:SwitchPlayerToPhysics(true)`**.

That last step is the one the single-player starts share (`SaveGame.lua`'s
load path, the console): with the player's physics object still disabled it
puts the pawn's head at the camera, seeds `Player.Pos` from the entity, and
enables the object. So `CreatePlayer` makes the player DISABLED, or the
`onlyEnable` call returns before doing any of it.

Skipping it is not harmless. `Player.Pos` is `Clone`d from the class default
and `Clone` copies a `Vector` by reference, so a fresh player's `Pos` is
(0,0,0) until something writes it - and the first `Game:Tick` copies it into
`PX,PY,PZ` BEFORE `CPlayer:ClientTick` synchronises it. Every
`AmbushForPlayer` box tests `IsInside(PX,PY+1,PZ)` that same tick.
C2L1_Bridge's box 008 spans the whole bridge through the origin, so its nine
spawn points fired at load and the level opened with ninjas on the deck.
Measured: `EXEC PX=-0.2 -11.1 -304.1 Player=0.0 0.0 0.0`, then seven spawn
points launched; after the switch call the Bridge loads with zero actors.

## The camera is the scripts'

`Game:Tick2` steers the view whenever there is a player, `Game.CameraFromPlayer`
(true by default) and the mouse is locked. `Game:UpdateViewFromPlayer` is the
whole of it:

```lua
local mdx,mdy = MOUSE.GetDelta()
local crx,cry = CAM.GetRawRotation()
if Cfg.InvertMouse then mdy = -mdy end
destPos = ENTITY.PO_GetPawnHeadPos(Player._Entity)
destPos.Y = destPos.Y - PLAYER.GetCameraFix(Player._Entity)
crx = crx + mdx ; cry = cry + mdy
CAM.SetPos(destPos.X, destPos.Y, destPos.Z)
crx = math.mod(crx,360) ; cry = math.mod(cry,360)
if cry > 80 then cry = 80 end
if cry < -80 then cry = -80 end
CAM.SetAng(crx, cry, 0)
```

So **`MOUSE.GetDelta` returns DEGREES**, not pixels - the results are added
straight onto `CAM.GetRawRotation`'s degrees. Both axes pass through with
their sign (findings 3 and 4 below). `Cfg.InvertMouse` is applied script-side
(`Game:UpdateViewFromPlayer`), and `MOUSE.SetInverse` also reaches
`Input::TakeLookDegrees` - see Plan.md, open questions. The pitch clamp is the engine's own
±80°, and the eye is the pawn head less `PLAYER.GetCameraFix` (the bob, still
0 here). `CAM.SetPositionDisplacement` is an offset added after that, which
is how the view shakes without moving the player; it is kept apart from the
camera position so the `CAM.GetPos` the scripts read stays the true eye.

The C++ loop now only follows: it feeds the window's mouse motion to `Input`,
skips its own look while the scripts own the view, and adopts the pose after
`Game_Tick2`. The free camera (noclip, or before a player exists) keeps its
own look and mirrors itself into the `CAM` reads as before, because the
scripts still derive the movement basis from them.

The pixels-to-degrees constant is **calibrated, not recovered**: the mouse
natives are registered Lua thunks rather than named functions in `Engine.dll`,
so there was no constant to read. `Input::kDegreesPerPixel` reproduces the
free camera's old feel at the shipped `Cfg.MouseSensitivity` of 40.

Verified by injecting a delta headlessly: `MOUSE.GetDelta` returning `2,1`
advances the reported turn by exactly 2° and the elevation by 1° per frame,
from the level's own starting angle, and `CAM.GetPos` matches
`ENTITY.PO_GetPawnHeadPos` exactly.

## Traces, and the intersection solver

`WORLD.LineTrace(x1,y1,z1, x2,y2,z2)` returns ten values, and every caller
unpacks all of them:

```lua
local b,d, x,y,z, nx,ny,nz, he,e = WORLD.LineTrace(...)
```

hit, distance, the hit point, the surface normal, the physics body and the
entity. A world hit reports entity 0, and that is what makes
`ENTITY.IsFixedMesh` answer true for it - the test every weapon uses to
decide between a wall effect and damage. `LineTraceFixedGeom` is the same
trace restricted to the world mesh, which is what the actors' ground and step
probes want.

**The "intersection solver" is a trace-visibility set**, not a solver. Every
trace in the game is bracketed:

```lua
ENTITY.RemoveFromIntersectionSolver(entity)
local b,d,... = WORLD.LineTrace(cx,cy,cz, dx,dy,dz)
ENTITY.AddToIntersectionSolver(entity)
```

so a shot does not hit the thing that fired it. The pair has to be exact - a
leaked Remove leaves something permanently unhittable. The ragdoll variants
say the same about an actor's ragdoll, which is the same body here, and they
are the busiest natives in the whole report at 15,600 calls in a 400-frame
run. The camera's own probe body is always excluded, since it sits exactly
where a shot from the player starts.

Verified by tracing 200 units from the spawn in six directions: the floor
comes back at **exactly 2.00 below the eye** with normal (0,1,0) - an
independent confirmation of the pawn height - walls report inward normals at
1.76 to 32.5, and a prop 111.8 out reports its body and entity with
`IsFixedMesh` false.

## The view model

`ENTITY.SetPosAndRotRelativeToCamera(e, x,y,z, ax,ay,az)` parks the held
weapon each frame at a fixed offset in CAMERA space plus Euler angles.
Camera space is the scripts' own, **-Z forward** - which is what their
forward vector reduces to at a turn of zero. Measured against the shipped
offsets: the weapon sits 1.167 ahead, 0.495 down and 0.382 to the side, for
authored values of 1.2 / 0.49 / 0.39 (the forward difference is the pull-back
`CWeapon:ClientTick2` applies above 90° FOV).

## The rotation conventions, settled by three findings that agree

Three separate symptoms turned out to be one question - how the engine spells
a rotation - and the answer only holds together when all three agree.

**1. Euler composition.** `EulerToQuat` had a standing TODO against
`0x1011C390`. Its maths is `FUN_1011bea0`, and with half-angles it builds

```
w = cz*cy*cx + sx*sz*sy      x = cz*cy*sx - sz*sy*cx
y = sy*cz*cx + sz*sx*cy      z = sz*cy*cx - sy*sx*cz
```

which is **qz * qy * qx** - X applied first, `Rz*Ry*Rx` as a matrix - output
as `[w,x,y,z]`. The port composed `qx * qy * qz`, the reverse. A reversed
quaternion product is a different rotation rather than the inverse of one, so
every scripted Euler rotation was wrong away from the axes. `QuatToEuler` is
re-derived as its true inverse and the round trip verified exact.

**2. Vector rotation is the CONJUGATE of textbook.** The engine rotates a
vector as `q^-1 * v * q`, so the scripts' `VectorRotateByQuat` is our inverse
rotation and vice versa - the two were swapped. This is the same transpose the
Jolt bridge already had to undo, and it is consistent with `EngineQuatToRot9`
being applied to ROW vectors: the rows of that matrix are where the local axes
land.

**3. The turn angle is `yaw + pi/2`, not its negation.** `CAM.GetAng`
round-tripped either way, so the read/write pair could not settle it. The
level data could: Cathedral authors `Lev.Ang.X = 91.174`, which is "90 plus a
small yaw", and under the negated mapping the spawn faced a wall 1.76 away
with 108 units of open space behind. Un-negated it faces the open space. That
also made the scripts' own movement basis disagree - until finding 2 fixed the
rotation direction, at which point camera, movement basis and level angle all
agree at once. That mutual agreement is the real check, and it is worth
re-running after any change here: the scripts derive the movement basis from
`CAM.GetAngRad` in pure Lua, and it must equal `CAM.GetForwardVector`.

**Finding 2 binds every native that PRODUCES a quaternion, not just the ones
that consume one.** That is the part it is easy to miss, and missing it sent
every shot backwards: the weapons build their fire direction as
`Quaternion:New_FromNormalZ(forward)` and then fire along that quaternion's
local +Z, so `NormalZToQuat` has to return the CONJUGATE of the textbook
shortest arc for the round trip to come back to the forward vector. The test
is exactly that round trip - `NormalZToQuat(n)` then `TransformVector(0,0,1)`
must give back `n`, and its dot with the camera forward must be +1, not -1.

**The quaternion natives read a missing argument as 0.** `EulerToQuat`
(0x1011C390) and `NormalX/Y/ZToQuat` (0x1012AAB0/ABC0/ACD0) all take their
three arguments through `Script::GetFloat(i, 0.0f)` (0x10146880), so a nil is a
zero, not an error. The shipped scripts rely on it: `Zombie_Soldier:
IfMissedPlaySound` builds its ground-hit puff from `nx, ny, nz`, three globals
nothing ever sets. Under `luaL_checknumber` that raised "bad argument #1 to
`NormalYToQuat`" out of `Game_Tick` on every miss - eleven times in one
Cemetery fight - and each one unwound the rest of `Game:Tick` for that frame,
so every actor after the zombie skipped its update. The natives
use `luaL_optnumber(L, i, 0)` now; whether the original also tolerates a
non-number string (its reader is FUN_10149F10) is not settled and does not
matter to any shipped script.

**Finding 4: the engine's ELEVATION is positive-DOWN**, the opposite of our
pitch. Also not arbitrary - the scripts feed the elevation into the X slot of
the engine Euler (`FromEuler(elevation, turn, 0)`), and a positive rotation
about X in a Y-up, Z-forward frame tilts forward toward -Y. Miss this and the
horizontal aim is perfect while every shot goes as far wrong vertically as
the player was looking, which is invisible at the spawn pitch of ~0. Test at
a STEEP pitch or not at all.

Findings 3 and 4 together bind the mouse: `MOUSE.GetDelta` passes BOTH axes
through with their sign - X because the turn runs the same way as our yaw, Y
because the engine's elevation and screen-down already agree. Negating either
inverts that axis of the look.

Because of finding 2, anything composing a rotation has to be careful which
way round it goes. The view model sidesteps the question entirely: it builds
the camera's rotation as its basis written out as matrix rows, multiplies in
row-vector order, and converts once at the end (`EngineRot9ToQuat`).

Still unverified against the binary, and suspect for the same reason:
`RotateQuatByAxisAngle` composes `r * q`, which under this convention would
apply them in the other order. Nothing measured has needed it yet.

## Saving and loading

Sources: `Main/SaveGame.lua`, `HUD/PainMenu.lua` (`ReloadSaveGameList`),
`Main/Utils.lua` (`SaveFullObj`); Engine.dll `PCFSystem::SaveGame`
0x100518a0 / `LoadGame` 0x10051700, `GFileManager::CreatePAK` 0x1017f0e0 /
`ClosePAK` 0x1017e6d0 / `CreateFileWriter` 0x1017e8f0, the FS natives at
0x10123c10..0x10124390; the two shipped saves under `SaveGames/`.

A save is a folder, `SaveGames/NNN/`, and the split of work is the game's
usual one: the SCRIPTS serialise their own tables, the ENGINE serialises the
world, and the file layer stitches them into one container.

**The container.** `SaveGame:Save` writes `SaveGame.Info` as a loose file
(the menu lists saves by reading only that), then `FS.CreatePAK("Save.dat")`,
and every `FS.File_Open` / `File_Write` / `File_Close` and the
`WORLD.SaveGame` that follow land INSIDE that pak under their basename until
`FS.ClosePAK`. `Save.dat` is an ordinary `.pak`
([`Formats.md`](Formats.md), including the name seed this work recovered).
`File_Write` strips one trailing `\n` or `\r` and writes CRLF, which is why
the shipped `.Info` files are CRLF. Loading is the mirror:
`FS.RegisterPack("Save.dat", "<dir>/")` mounts the pak over the save's own
folder so `DoFile("<dir>/LevelStart.Info")` and `FS.FindFiles("<dir>/*.CLevel")`
read out of it, and `UnregisterPack` drops it. In the engine this is
`FileSystem::MountPack` / `BeginPak` / `WriteFile`; the natives are in
`Natives.cpp` and resolve paths through `LuaHost::ResolvePath`, so
`../SaveGames` is the folder beside `Data`.

What goes in, in `SaveGame:Save`'s order: `LevelStart.Info` and
`CurrState.Info` (the level-state snapshots), then for a real save the level
as `<name>.CLevel`, every live object as `<name>.<Class>` (`SaveFullObj` -
tables recursively, functions and `s_` statics skipped, object references as
`"ref:<name>"`), `OtherData.State` (the `EntityToObject` map, handle to
object name), `Game.State` (the whole `Game` table), and last
`<level>.World` from `WORLD.SaveGame`. The save TYPES matter: `StartLevel`,
`NewLevel` and `AutoNewLevel` stop after the `.Info` files and load by
starting the level fresh through the map screen; `Normal`, `Quick` and
`CheckPoint` carry the world. Slot 000 is rewritten as `StartLevel` at every
`Game:OnPlay(true)`, so a level start always produces it.

**The world file.** The original's is `"C^"`, a version (3), then glass,
`"AUDIOv01"` plus the Miles state, physics, pathfinding, entities,
portal state and zone state - and `SaveGame:AfterLoadEntities()` is called
from C++ between the entities and the portals. Its layout is decoded
([`Formats.md`](Formats.md), "The original world save") and `WORLD.LoadGame`
reads it ("Loading an original save" below). `WORLD.SaveGame` writes it too
([`Formats.md`](Formats.md), "Writing the original world save"), which is what lets
the original load our saves. Our own file goes beside it as
`<level>.World.pksv` (`PKSV`, version 6; `Source/Game/ScriptSave.cpp`), carrying
what the original format has no room for. `WORLD.LoadGame` prefers ours when it is
there. The original never sees it: it lists only `*.C*` object files. Both files
follow the same contract: every entity comes back at the HANDLE it had, because the scripts
saved those handles in `EntityToObject` and in every `_Entity` field, and
`Cache:PrecacheLevel` is deliberately run only after `LoadGame` so the counter
is not disturbed ("indeksy musza isc od zera").

The rule the file follows: an entity is stored as what MADE it, not as the
slots it holds. The natives now retain their own arguments on the entity -
`PO_Create`'s body type and scale and the mass / rotation freedom / damping /
pinned / gravity dressing, `PARTICLE.AddEmitter`'s file with `SetupEmitter`'s
transform and the `SetEvolve` / `Die` state, `BILLBOARD.SetupCorona`'s
fourteen arguments, `SND.Setup3D`'s sound and whether `SND.Play` happened -
and `LoadWorld` replays them through the same subsystem calls the natives
make, then applies the live state on top (pose, velocity, animation slot and
time, hidden meshes, the ragdoll pose as a seed for `EnableRagdoll`). Active
meshes are remade from their map object, the player's pawn is respawned at
its saved head position with the walk/fly state, and the camera pose is set
so the app's next frame seats on it. `LoadWorld` first releases EVERY
entity, including the active meshes and water the preceding `WORLD.LoadMap`
just made (their handles belong to the save), and water surfaces are then
re-pointed at the restored entities of the same name.

**The app side.** `SaveGame:Load` runs inside a tick or a menu action, so
the level renderer it invalidates is rebuilt afterwards: `WORLD.LoadMap`
bumps a serial, `ScriptEngine::TakeLevelChange` reports it at the top of the
next frame, and `GameApp` tears down and brings up the level with
`fromSave` set - no `Settle`, no `Game:OnPlay`, no `SwitchPlayerToPhysics`,
since `SaveGame:Load` sets `Game.Active` and the world is already where it
was. A `StartLevel` save goes to the empty level and the map screen instead,
which the same hook handles as a teardown. `PMENU.Activate(false)`, which
`SaveGame:Load` and `PainMenu:LoadLevel` end with, clears the screen and
unpauses without the `CloseMenu` hook Escape runs.

Verified headless (`PainfulTools lua <root> N C1L1_Cathedral "<chunk>"`):
a save taken forty frames after teleporting into the first ambush, and its
load in a fresh process, agree on the live-actor count, the player position,
the object count and the player's health, with no script error on either
side; the pack written is decoded independently (a PowerShell parse of its
directory with the seed formula) to the same 665 names.

**The music is carried** (version 5; version 6 adds the 2D and 3D sounds and both
ID counters - [`Sound.md`](Sound.md), "Handles are Miles IDs"): each music slot's file, byte, volume, and
whether it plays. The shipped scripts delete both streams when a save loads
(`CLevel:Delete`) and start one only when the music changes, so without this a
loaded game stayed silent. The original keeps the same state in its audio chunk
([`Formats.md`](Formats.md), "The audio chunk carries the music").

Not carried over: the animation cross-fade in progress (the new run starts on
the current animation), angular velocity of free bodies (`angVel` is kept,
the solver's own spin is not read back), the particle systems' live
particles (emitters restart), the decals on the walls (engine
entities the scripts never see; [`Decals.md`](Decals.md)), and anything in
the stub natives. The bookkeeping
`WORLD.SwitchToState` / `LateVBsBegin` / `LateVBsEnd` / `UpdateAllEntities`
are no-ops here.

**Lights ARE carried, and have to be.** The world file's version went to 2 to
add them (a version 1 save still loads, without them). The shipped `CLight` has
no `RestoreFromSave`, so nothing re-runs `LIGHT.Setup` on a restored entity -
`CEnvironment`, which does have one, re-`Apply`s itself instead. The original
gets away with it because `Light::SaveEntity` / `Light::LoadEntity` put the
light in the save's own world data. Without this a loaded level came back with
exactly ONE light, the flashlight `Game:OnPlay` makes fresh, and every placed
`CLight` was gone. [`Lighting.md`](Lighting.md)

**Version 4 carries the centred-mesh flag and a child's local transform** (2026-09-13,
Physics.md "The stake"). **Version 3 carries the parent joint INDEX** (2026-09-12). `BindFX` hangs an
effect on a joint by the number `MDL.GetJointIndex` returns, and
`PARTICLE.SetParentOffset` / `ENTITY.RegisterChild` keep a number as
`parentJointIndex` with the name field empty; the file carried only the name.
Every restored effect therefore sat on its parent's ORIGIN: a checkpoint's
three `checkpoint_fx1` and the end-of-level teleport's five flames collapsed
into one spot instead of riding the joints the idle animation moves. A version
2 save still loads that way; the next save is right. The same pass fixed the
moved active mesh: `RebuildEntity` re-based the rebuilt world object on the
SAVED position instead of `activeOrigin`, so a gravestone knocked over before
the save drew displaced from its body by however far it had moved.

**Loading an original save** (2026-09-14). `LoadWorld` hands a file that starts
`C^` to `LoadWorldSave` (`Source/Game/ScriptWorldSave.cpp`), which reads it
with `Assets/WorldSave` ([`Formats.md`](Formats.md), "The original world save"),
turns each record into the Entity fields our own save keeps, and calls
`RebuildEntity`: one rebuild path for both formats.

- The level's own objects stay. `World::SaveEntities` never writes a map active
  mesh, and the saved decals hang off level handles (9, 17 and 235 in Train
  Station), so only script-made entities and whatever sits on a saved handle are
  released. A level object on a saved handle fails a check.
- Models: the pose; draw flag 0x40; one visibility byte per `.pkmdl` mesh; the
  animation slots, with slot 0 left empty because it is the model's own and never
  written, while the scripts' `_CurAnimIndex` counts from it; each slot's
  movement-curve mask, which `Model::LoadEntity` hands the animation on `ROOOT`
  (without it a walk has no root motion); the playing channel's time, speed and
  loop; `PO_Create`'s type and scale argument, mass, friction, restitution,
  damping and collision group; the monster block (`PO_SetSightParams` 0x10131210
  stores range, 360 range, yaw at degrees x pi/360 and pitch at degrees x pi/180
  in that order at +0x24..+0x30; then move wish, move const, and flying 0x800). The
  player is the model whose body carries the mover. Its pawn spawns 2 above the
  entity, which sits at the feet as ours does.
- An item pack's mesh body (`CreatePhysicsObjectFromMesh`, types 4-8 and 11-13)
  takes the entity's scale. The 1.0 its PhysicsObject carries is not a scale, and
  building from it gave every destructible unit-scale collision.
- A body's freedom-of-rotation mode is re-applied when it is not 1, the
  `CreatePhysicsObject` default. Every `CObject` item carries 3 (`PO_Create` sets it
  after `PO_SetMass`), broken debris 2. Without it the body kept the inertia of its
  creation mass under a mass of 1000: benches in Train Station save 009 sank
  through the platform, then fell. With it they settle at y 11.473, the same as a
  fresh level start.
- A body's `^Q` header is `ENTITY.EnableCollisions`' minimum time and strength, and
  turns its collision reports back on.
- A limb's extra floats are its `EnableCollisionsToRagdoll` pair.
- A ragdoll's damping and its moved-by-explosions flag are restored before
  `EnableRagdoll` applies them.
- An animation slot keeps its own loop flag: `SetAnim`'s third argument, the last
  time the slot played, which `Model::LoadEntity` hands to `LoadAnimation`.
- Paths: a `PATH` handle is a Pathfinder2 slot counted from 0 (`PATH.IsFinished`
  0x1013aa20 indexes the array with it), and ours now count the same way. A live
  slot's next point and destination are routed again, as `WaypointGPath2::Load`
  does. Without them every walking monster held a dead handle and kept its saved
  move wish.
- Item packs: `CreateEntity`'s last argument is header flag 0x80, which centres the mesh.
- Lights: where `LIGHT.Setup` / `SetFalloff` / `SetIntensity` store (colour
  +0x678, intensity +0x67c, direction +0x7e4, cone +0x7f0 outer and +0x680 inner,
  range +0x7f8, start +0x7fc, type +0x7f4); dynamic is flag 0x400000.
- Coronas: `SetupCorona`'s arguments from where 0x10137b70 stores them. The blend
  comes back from the material enum (1, 2, 4, 5 to 1, 2, 3, 4); sprite-only is
  corona flag 0.
- Particle effects: each emitter's file with `SetupEmitter`'s offset, rotation and
  scale (0x10139cb0). A child that follows is placed on its parent's joint (+0x118).
- Ragdolls: limb poses matched by bone name. A saved position is the Havok
  body's, DISPLACEMENT times the scale short of our part frame (C3L1_LampA's
  joint6: 124.1 x 0.72 = 89.4 units). Fixed parts (`.hke` mass 0) keep the seed.

The music streams are restored: each is reopened at its byte. Those in the pause
set play; the others stay paused. The 2D and 3D sounds come back at their IDs,
before the entities ([`Sound.md`](Sound.md), "Handles are Miles IDs"). Not
state, and the portal and zone flags (the antiportal flags are applied). `WORLD.LoadGame` logs the counts.
state, and the portal and zone blocks. `WORLD.LoadGame` logs the counts.

KNOWN DEVIATION, not specific to loading: our `PATH.GetShortest` is A* between
the waypoints closest to each end, whereas `Pathfinder2::GetShortestPath`
(0x1016c070) searches by area and picks the portal waypoints that minimise
from-to distance. So a monster already beside the player can be routed on a detour:
after loading the Train Station save, two that started 4.6 and 5.6 units away ran
down onto the tracks and ended 22 units off.

Verified headless with `SaveGame.Save` stubbed so no slot is written
(`PainfulTools lua <root> 30 <level> "PROBE_SLOT=9 dofile([[probe]])"`). The Train
Station Quick save restores 599 of 798 entities: the player at its saved position
with 5 weapons and 107.6 health, 14 live monsters where their scripts have them,
50 ragdolls posed, 0 script errors. An Atrium checkpoint restores 476 of 584. The
worst moving ragdoll part lands 6.4 units from its animation seed, on a toppled
stand.

## The time multiplier

Sources: `PCFSystem::TickEngine` 0x10051110, `PCFSystem::SetTimeMultiplier`
0x10001760, the natives `WORLD.SetWorldSpeed` 0x10120470, `INP.SetTimeMultiplier`
0x1011CD90, `INP.GetTime` 0x1011CBE0; `MilesEngine::SetSpeed` 0x101F2090 and
`SetLowPass` 0x101F13C0; `Templates/Processes/PBulletTimeControler.CProcess`.

One double, at `GEngine+0x100`, is the world speed. `TickEngine` multiplies the
frame delta by it, caps the product at one second, and hands that to
`EngineGame::Tick` - so everything inside the game tick, the scripts' `delta`
included, runs on scaled time, and the bullet-time controller recovers real
time as `delta / INP.GetTimeMultiplier()`. `INP.GetTime` is
`SystemDriver::GetTickCount`, real time: a checkpoint's launch delay and every
other `INP.GetTime` timer keeps its wall-clock length in slow motion.

Two natives write the field. `INP.SetTimeMultiplier(x)` writes it and nothing
else (F3/F4 halve and double it in Game.lua's debug keys). `WORLD.SetWorldSpeed(x)`
writes it AND drives the audio: Miles is set to a speed of `1 + (x - 1) * 0.5`
(both constants read from the binary: 1.0 at 0x102AEA58, 0.5 at 0x102C5530),
every sample's low-pass cut-off to the square root of that speed (clamped to
0.2..1 by `SetLowPass`), and when the speed returns to exactly 1 from anything
else the engine pauses and resumes every sound to reseat the rates. At the
scripts' quarter-speed bullet time the audio plays at 0.625 with a cut-off of
0.79 - a drop of a few semitones and a light muffle, not half speed. Only a
`SOUND.Play2D` voice started with `sameSpeedInBulletTime` keeps its own rate
(`Sound2D_SetAlwaysSameSpeed`); `SOUND2D.Play` ignores its second argument, so
the bullet-time loop itself slows with the rest.

Here the field is `ScriptEngine::timeMultiplier_` and `GameApp` scales what
the original's game tick covers: the script deltas, animations, monsters,
projectiles, the physics accumulator,
lifetimes, bound-sound timers, collisions, particles and billboards. NOT the
player: the mover keeps real time, as PlayerAction does with its `/ s` terms
([`PlayerMovement.md`](PlayerMovement.md), "Slow motion").
`AudioEngine::SetWorldSpeed` is the Miles half: every voice not flagged
`sameSpeed` advances at the rate, and a one-pole low-pass at `cut` of Nyquist
sits on the voice mix (not the streams - the scripts pause the music for the
duration). The mix reseat on return to 1 is not needed with a per-frame rate.
The audio clock (`Advance`), the console, the menus and the post-process
effects keep real time, as they do in the original's frame outside
`EngineGame::Tick`.
