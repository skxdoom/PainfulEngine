# Authoring a level from code

A Painkiller level is almost entirely plain Lua text. That is the surprise, and
it is what makes levels authorable without an editor.

## What a level is

```
Data/Levels/<Name>/
  <Name>.CLevel        Lua property assignments - the level's settings
  <Name>.lua           Lua class script - OnPlay, the magic card, the loadout
  CLight/*.CLight      one plain-text Lua file per placed light
  CItem/*.CItem                                     ... item
  CSpawnPoint/*.CSpawnPoint                         ... spawner
  CActor/, CSound/, CBox/, CArea/, ...
Data/Maps/<map>.mpk    the world mesh - the ONE binary
Data/Maps/<map>.wps    the waypoint graph, for AI pathing (optional)
```

The shortest shipped level is proof of how little is mandatory.
`C6L8_Galleon.lua` is a single line (`o.AddOn = true`) and its `.CLevel` is
four:

```lua
o.Overbright = true
o.BaseObj = "CLevel"
o.Map = "C6L8_galleon.mpk"
o.WayPointsMap = "C6L8_galleon.wps"
```

No entity directories at all. Everything a level *needs* is the `CLevel` base
object and a map.

Loading by name through `game <root> <level>` does not consult any registry -
the campaign level list in `LScripts/HUD/Levels.lua` only governs the chapter
select. A level directory that exists, loads. (A level directory that does
*not* exist also "loads", silently, as an empty world; the name is not
validated.)

Generated content goes in as **loose files under the data root**. `ReadFile`
mounts the `.pak` archives first and falls through to disk for anything they
do not serve, so a new name resolves and a shipped name cannot be shadowed.

## The world mesh

`MapMesh::Write` in [Mpk.cpp](../../Source/Assets/Mpk.cpp) emits what `MapMesh::Load`
reads. Two conventions have to be right, and neither is guessable.

### Vertex layout

Eight floats per vertex, and what they mean depends on `uvChannels`:

| uvChannels | 0,1,2 | 3,4,5 | 6,7 | normals array |
|---|---|---|---|---|
| 1 (dynamically lit) | position | normal | uv | empty |
| 2 (lightmapped) | position | uv0 | uv1 | separate, 3 floats/vertex |

Hand-authored geometry wants `uvChannels = 1`. `2` means the object carries a
baked `<name>_L_0000` lightmap in texture slot 1, which is a bake pipeline we
do not have.

### Winding

**The exporter winds triangles so the geometric normal OPPOSES the vertex
normal.** This is measured, not inferred - the `map` diagnostic reports it:

```
$ PainfulTools map Data/Maps/1x01_Chaos.mpk
  winding: 44 triangles agree with their vertex normals, 283457 oppose
```

Everything downstream is built on that. The renderer culls CCW to suit
([WorldRenderer.cpp:253](../../Source/Render/WorldRenderer.cpp)), and
`PhysicsWorld` feeds Jolt the *reverse* of each triangle because Jolt takes
counter-clockwise as the front face. Wound the intuitive way instead, a floor
is invisible from above **and** bodies fall through it - each half of the
mistake hiding the other.

For a grid laid out along +x and +z with normals pointing +y, going round each
quad the obvious way - `(a, b, c)` then `(a, c, d)` - already produces this,
because `cross(+x, +x+z)` points -y.

### Sizes

Indices and the per-material `firstIndex` / `triangleCount` are `u16`, so one
object caps at 65535 indices. Split into several objects past that, which is
what the shipped maps do (1x01_Chaos is 564 of them).

### Object names carry semantics

Portals, antiportals, zones, barriers, `noclip`, `2sided`, breakable glass and
volumetrics are all encoded as substrings in the object's *name* - see
`MapObject::isCollidable` and the "Pain Engine - MPK Substrings" document. A
plain name means plain solid geometry. A level with no zone objects falls
through to drawing everything, which the visibility code handles.

## The `mklevel` command

```bash
PainfulTools mklevel Data TestFloor 200 0 beton_tile_all
```

`mklevel <root> [name] [extent] [height] [texture]` writes a complete level
whose map is one big walkable floor: `Maps/<name>.mpk` plus the `.CLevel` and
`.lua`. The floor is `extent * 2` units square, centred on the origin at `y =
height`, tessellated 64x64 (4225 verts, 8192 triangles - comfortably inside
the `u16` ceiling) and tiled one texture repeat every 8 units.

`o.Scale` is written as `1`. It multiplies the **world mesh and nothing else**,
so 1 keeps mesh units and world units identical and makes the numbers above
mean what they say. The shipped levels use 0.3 to 1.2.

## Verification

Round-trip through the reader:

```
$ PainfulTools map Data/Maps/TestFloor.mpk
  1 objects, 4225 verts, 8192 tris, terminator OK
  bounds x[-200.0..200.0] y[0.0..0.0] z[-200.0..200.0]
  winding: 0 triangles agree with their vertex normals, 8192 oppose
  parser skipped 106 bytes across 1 regions
```

8192 of 8192 opposing matches the shipped convention exactly. The one skipped
region is the material block, which the loader peeks at without moving its
cursor and then resynchronises past - shipped maps show the same thing, one
skip per object.

Then walkability, by dropping the player from a height and seeing where they
come to rest:

```bash
PAINFUL_PLAYER_AT="60,40,-25" PAINFUL_SHOT_FRAME=250 PainfulEngine game Data TestFloor --shot out.tga
```

| dropped at | result |
|---|---|
| `0, 30, 0` | `camera 0.00 2.02 0.00, player on the ground` |
| `190, 30, 190` | `camera 190.00 2.02 190.00, player on the ground` |
| `-195, 30, 60` | `camera -195.00 2.02 60.00, player on the ground` |
| `250, 30, 250` | `camera 250.00 -36.03 250.00, player airborne` |

Resting at exactly 2.02 above a floor at 0 in three places inside the bounds,
and falling straight through outside them. The last row is the one that matters:
it proves the floor is real geometry with real extent rather than a plane the
physics happens to clamp against.

## Why this is worth having

Every test until now ran against shipped data, so when something looked wrong
there was no way to tell whether the engine was wrong or the level was doing
something exotic. A generated floor is the first fixture with a known right
answer: known height, known bounds, known winding, known scale. The two-worlds
scale bug and the physics winding trap would both have been one command each.
