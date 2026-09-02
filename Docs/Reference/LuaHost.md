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

`SOUND2D`, `EDITOR` and `LANG` exist in scripts but not in the recovered
tables; they are created as auto-stub modules. Every module table carries an
`__index` that materialises a logging stub for any unlisted name, so a
mismapped native surfaces in the call report instead of as "attempt to call
nil". Bare globals (49, registered individually) include `DoFile`, `Log`, the
quaternion/vector helpers (flat multi-value: `EulerToQuat(ax,ay,az) →
w,x,y,z`, engine order `(w,x,y,z)`), and the build/edition/CD-check flags.

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
bind list:  grep -oE '\{"[A-Z0-9_]+", *"[A-Za-z0-9_]+"' Source/Game/ScriptBind.cpp
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
PainfulTools lua <DataRoot> [frames]
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
straight onto `CAM.GetRawRotation`'s degrees. Both axes come back negated
from ours, because the engine's turn angle runs opposite to our yaw and
screen-down is a downward look; `Cfg.InvertMouse` is applied script-side and
must not be applied again in the native. The pitch clamp is the engine's own
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

## Next stages
2. Damage: the shot lands but nothing takes it yet. `ENTITY.ExplodeItem`,
   `ENTITY.EnableGunPass`, `ENTITY.SetRotationCAM`, and whatever the hit
   handlers need to reach an actor's health.
3. Animation and the actor clock - which is also what gates melee damage and
   the actors' whole event loop. See
   [`Plan.md`](../Plan.md) for the full order.
