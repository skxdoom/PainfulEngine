# Decals — decoded

Read out of `Engine.dll` with Ghidra and cross-checked against the shipped
data. Addresses are absolute at the `0x10000000` image base.

| What | Symbol | Address |
|---|---|---|
| spawn by position and normal | `ENTITY.SpawnDecal` (Lua binding) | `0x10135A00` |
| spawn with the box's own axes | `ENTITY.SpawnOrientedDecal` | `0x10135BE0` |
| the "static" definition with a texture | `ENTITY.SpawnStaticDecal` | `0x1013D420` |
| re-cut an existing decal | `ENTITY.UpdateDecal` | `0x10135E60` |
| drop the definitions | `ENTITY.ReloadDecalSystem` | `0x1011DFE0` |
| the pkkeepdecals cheat | `R3D.KeepDecals` | `0x10123B20` |
| ageing rate from `Cfg.DecalsStayTime` | `R3D.ApplyVideoSettings` | `0x1013F610` |
| definition defaults | `DecalEffect` factory | `0x1008A740` |
| `.ini` reader | `DecalEffect::Load` | `0x1008A290` |
| box from a normal | `Decal::Spawn(Entity*, Vector, Vector)` | `0x101CF930` |
| box from three axes | `Decal::Spawn(Entity*, Vector x4)` | `0x101CFBE0` |
| projection and clip | `Decal::Spawn(Entity*, Matrix)` | `0x101CDE70` |
| the clock and the fade | `Decal::Tick` | `0x101CDD70` |
| submit | `Decal::Draw` / `Decal::SetState` | `0x101CF5C0` / `0x101CF310` |
| constructor | `Decal::Decal(DecalEffect*)` | `0x101CF7B0` |
| vertex cap | `Decal::MAX_ELEMS` | `0x102AE52C` = 2048 |

Port: `Source/World/Decals.*` (definitions, projection, clock),
`Source/Render/DecalRenderer.*` (drawing), `Source/Game/ScriptDecal.cpp`
(the natives).

## Who spawns them

Every impact mark in the game is a script call. `WORLD.LineTrace` hands the
weapon scripts a point, a normal and an entity, and the script calls
`ENTITY.SpawnDecal(e, 'bullethole', x,y,z, nx,ny,nz)` — `Shotgun.lua:326`,
`MiniGunRL.lua:383`, `Rocket.lua:156` (`rockethole`), `Stake.lua:387`
(`stake`), `BoltStick.lua:383`, `DriverElectro.lua:617`, and the monsters'
`HitDecal` / `walkDecal` for the big feet.

The stake and the bolt are the odd ones: they spawn nothing on the hit
itself. `OnHitSomething` re-traces 1.2 units through the stuck projectile
with `ENTITY.PO_LineTrace(e, ...)` (`0x101318B0`) — a trace against ONE
entity, the world mesh through `PhysicsWorld::LineTraceStaticMesh` or a body
through `PhysicsObject::LineTrace`, same ten values as `WORLD.LineTrace` —
and puts the `stake` crack, the `stake_imp` splinters and the `stakeHitWall`
sparks where that lands. Only when the hit was a fixed mesh (`mode 0`); a
stake in an actor shatters instead.

**Blood is not spawned by the wound.** `CActor:BloodFX` (`CActor.lua:3279`)
throws one or two `Blood.CItem` — a 0.2-scale physics body in the Particles
group — and *those* spawn the decal when they land:

```lua
function Blood:OnCollision(x,y,z,nx,ny,nz,e)
    local size = FRand(1,2)
    ENTITY.SpawnDecal(e, b[math.random(1,3)], x,y,z, nx,ny,nz, size)   -- bloodSmall / 2 / 3
    if ny<0.1 and ny>-0.1 then      -- a wall: add a drip
        ENTITY.SpawnOrientedDecal(e,'bloodLeak', x,y-size/3,z, nx,ny,nz, 0,-2.5,0, size*0.7)
    end
    GObjects:ToKill(self)
end
```

`BloodFX` runs on a hit and on a **ragdoll limb landing**:
`CActor:StdRagdollOnCollision` plays the joint's fall sound and calls
`BloodFX` for every joint listed in the template's `RagdollCollisions.Bones`
(82 templates; `{"k_szyja", "bodyfalls", true}` = joint, sound, bleeds). That
handler only runs when `ENTITY.EnableCollisionsToRagdoll` armed the joint —
see [`Physics.md`](Physics.md), "Contacts". The German build swaps in the
`_German` definitions (`Tweak.GlobalData.GermanVersion`).

## The `.ini`

`Data/Scripts/Decals/<name>.ini`, section `[General]`, read by
`DecalEffect::Load`. Defaults are the factory's (`0x1008A740`), and a missing
file leaves them all in place.

| Key | Default | Meaning |
|---|---|---|
| `Texture` | white | `Decals/<Texture>`; a name of one character or none is the white texture |
| `Scale` | 1 | the **full width** of the projection box, in world units, times the spawn scale |
| `ZScale` | 0 | the box's depth; 0 = no depth planes at all |
| `CullBackFaces` | -1 | reject a triangle whose normal dots below this with the decal normal; -1 = keep all |
| `LifeTime` | 6 | seconds; negative = immortal |
| `DecayTime` | 1 | the fade at the end; also sets `AnimationTime` |
| `AnimationTime` | = DecayTime | read into the effect; nothing recovered consumes it |
| `FPS` | 0 | > 0: the texture is `<base>_00`.. played once, and the decal ages at real time |
| `BlendMode` | `invmodulate` | the material-script names (`FUN_100973A0`): none, alpha, add, modulate, filter, translucent, invmodulate, subtract, revsubtract, desttranslucent, destalpha, modulate2x |
| `CutTris` | 1 | 1 clips triangles to the box; 0 takes any triangle that reaches into it whole |

The shipped set: `bullethole` (0.3 wide, 0.1 deep, cull 0.5), `rockethole`
(3 / 1 / 0.5), `stake` (1.1 / 1 / 0.5), `blood` (2 / 3 / 0.1), `bloodSmall`
1..3 (1 / 3 / 0.1), `bloodLeak` (1 / 3, 15 fps over `blood_ani_00..30`),
`molotov` (3 / 0.1, 30 fps, translucent), `splash` / `splash_big` /
`splashMutaNemo` (translucent, 20–25 fps over `chlup_128_00..32`, lifetime =
the animation), `electro`, `boltstick`, `shadow` and `static` (immortal,
`CutTris 0`). `shadow.ini` spells its depth `xZScale`, which the reader does
not match, so it projects without depth.

### Why inverse-modulate

The default blend is `invmodulate` — `dst * (1 - src)`. The textures explain
it: `bullethole.tga` and `rockethole.tga` are 24-bit with **black edges and a
white centre**, and `blood.tga` is opaque everywhere (alpha 255 in every
corner) with a cyan-leaning centre (B 251, G 251, R 170). Under an additive
or alpha blend those would be bright blobs; under inverse-modulate the texture
is what the surface *absorbs*: black edges leave the wall alone, a white
centre burns it black, and cyan takes the green and blue out and leaves dark
red. The fade multiplies the texture by `life / DecayTime`, so a decal
dissolves to "absorbs nothing". `translucent` decals (the splashes, the
molotov flame) fade through their alpha the same way.

## Spawn

`ENTITY.SpawnDecal(e, name, x,y,z, nx,ny,nz [, scale = 1])`:
`World::CreateEntity(ETypes.Decal = 6, name, "", scale)`, `Decal::Spawn(e,
pos, n)`, `World::AddEntity`; returns the decal's handle. The oriented form
takes `up` in arguments 9–11 and the scale at 12, and builds
`right = n × up` (normalised). `SpawnStaticDecal(e, texture, pos, n, up,
right)` uses the `static` definition and `Decal::SetTexture`.

The box (`Decal::Spawn(Entity*, Vector, Vector)`, row-vector matrix):

- `Z = -n`, times `ZScale` when it is positive;
- `X = Z × p` normalised, `Y = X × Z` normalised, both times
  `Scale * scale` — so `Scale` is the full width;
- a **mortal** decal is then spun about Z by `rand() * 2π / RAND_MAX`
  (`FUN_100A12E0` with `_DAT_102CA1C0` = 1.9175e-4). Immortal ones keep the
  frame — which is what makes a placed shadow reproducible.

The oriented form scales `up` and `right` without normalising them: the blood
drip passes `up = (0, -2.5, 0)` and comes out 2.5× taller than wide, hanging
down the wall.

### Projection (`Decal::Spawn(Entity*, Matrix)`)

Refused unless the entity is a **Mesh** (`entity+0x20 == 1`) that is not
being deleted (`+0x24 != 2`). The decal registers as the entity's child
(`RegisterChild(e, decal, true, -1)`), so it dies with what it is on.

Six clip planes in the box's space, `x`, `y` in `[-0.5, 0.5]` and `z` too
when `ZScale > 0` — a `ZScale 0` decal has no depth at all and takes every
triangle of the object in front of and behind the point. Then, over the
entity's own index/vertex buffers (`+0x770` / `+0x774` / `+0x7A0`):

1. **Back faces** — only when `CullBackFaces > -1` (`_DAT_102B0754`): the
   triangle's geometric normal is dotted with the decal normal and the
   triangle is skipped below the threshold. The `.mpk` winds so that
   `(v2 − v0) × (v1 − v0)` points out.
2. **`CutTris 0`**: the triangle is taken whole if, for every plane, at least
   one vertex is on the inside.
3. **`CutTris 1`**: a triangle with all three vertices outside any one plane
   is skipped; otherwise it is split by each plane in turn
   (`ConvexPolygon::Split`, tolerance 0.001) and the remainder is fanned.
4. Each vertex is stored as position, white colour, and `u = x + 0.5`,
   `v = y + 0.5` — the box-space position is the texture coordinate.
5. The vertex count stops at `MAX_ELEMS` (2048; the loop breaks past 2044).

The bounding box is rebuilt from the vertices; `Spawn` answers "true" only
when something was cut.

**The port and "which mesh".** In PainEngine every `.mpk` object is a Mesh
entity, so a trace or a contact names the object and the decal cuts that
one. This port reports the static world as entity 0, so
`ScriptEngine::BuildDecalGeometry` finds the object itself: a world-object
entity (an active mesh, a destructible's twin, a water surface) is cut from
its own map object in its current pose; otherwise a 0.5-unit trace along
the normal through the spawn point names the object by the triangle's
user data (`RayHit::worldObject`, set per triangle in `BuildStaticWorld`);
and when that finds nothing, every collidable object the box overlaps is
cut. A decal on a **model or a pack mesh** — blood on a crate — gets no
geometry, which is the original's answer for a non-Mesh entity as well.

## The clock (`Decal::Tick`)

```
if immortal: return
if FPS <= 0:
    if R3D.KeepDecals: hold
    else life -= dt * Cfg.DecalsStayTime        (Renderer+0x5d6b50)
else:
    life -= dt                                   (animated ones always age)
if life < 0: colour 0, World::DeleteEntityDelayed
elif life < DecayTime: colour = 0x01010101 * round(life / DecayTime * 255)
```

`Cfg.DecalsStayTime` is what the Video Options "decals" slider writes —
`Cfg.lua` defaults it to 0.4 and the presets run 1000 (off), 2.0 (x1), 1.0,
0.6, 0.4, 0.2 (x5); `R3D.ApplyVideoSettings` copies it into the renderer.
`R3D.KeepDecals` is the `pkkeepdecals` cheat and holds only the unanimated
ones. Both are honoured here; the animated frame is `floor(age * FPS)` clamped
to the last frame, which is the one thing the recovered code does not show
(the texture object plays the sequence in the original).

## Drawing

`Decal::Draw` copies the vertices into the shared dynamic buffer
(`Renderer+0x5d6b40`, the one the billboards use too) and draws a triangle
list with the decal's texture and the fade colour as the texture factor.
The port draws each decal from a transient buffer with the particle shader,
depth-tested (`LEQUAL`), no depth write, the `.ini` blend through
`BlendModeState`, and the surface triangles lifted 4 mm along the decal normal
so they win the test against the wall they were cut from. Fog uses the
blend's own fog colour (white for the modulate family), so a far decal fades
to nothing rather than to the fog.

## Checking it without a window

`PAINFUL_DECAL_TRACE=1` logs every spawn with the object count and the
vertex count it cut; `PAINFUL_CONTACT_TRACE=1` logs each limb contact with
its joint, closing speed and gate, and every `EnableCollisionsToRagdoll`
call. Headlessly (`PainfulTools lua <Data> <frames> <level> "dofile(...)"`),
a bullethole on a flat floor cuts 12 vertices (two floor triangles, each
clipped to a quad); on Cathedral's floor 9–24, a wall `bloodSmall2` 162 and
its `bloodLeak` 144. A monster killed and dropped from 2.5 units reports its
armed joints with closing speeds of 6–12 against `MinStren` 6, and
`MDL.GetJointFromHavokBody` on the handle gives the joint the template's
sound table is keyed on. Note `WORLD.LineTraceFixedGeom` never hit the world
before this work: its layer filter asked the pair table whether static meets
static, which is never (`PhysicsWorld::RayCast`, `StaticLayerFilter`).

## Not done

- `AnimationTime` is read and unused, as in the recovered code.
- Decals on models and pack meshes (the original cuts an item's mesh).
- A save does not carry decals ([`LuaHost.md`](LuaHost.md), "Saving").
- `MaxDecals`, if the original caps the count, was not recovered; nothing
  here caps it beyond the per-decal vertex limit.
