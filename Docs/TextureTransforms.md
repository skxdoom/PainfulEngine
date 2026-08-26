# Texture transforms — pan, tile and the named xforms

Decoded from `Engine.dll` and the shipped DX8 vertex shaders. This settles the
two things the roadmap listed as unconfirmed: what `pan[N]` is measured in, and
how the detail map is sized.

| What | Symbol / file | Address |
|---|---|---|
| accumulates elapsed seconds, drives every stage | `MaterialSystem::Tick` | `0x100983f0` |
| builds one stage's texture matrix | (per-stage animator) | `0x1009e5a0` |
| matrix multiply, `acc = acc * m` | | `0x100021e0` |
| translation matrix builder | | `0x1009e400` |
| applies a pass, uploads the constants | (material apply) | `0x100980b0` |
| `.shader` pass-key parser | | `0x1009a3b0` |
| detail tiling from Lua | `World::MeshesSetDefaultDetailMaps` | `0x1005a740` |
| the shader that consumes it | `Data/Shaders/tu2_detail.vso` | — |

## The stage struct

Material stages are `0xb0` bytes apart. The `.shader` parser and the runtime
address the same memory from bases `0x4c` apart, which is how the two halves
below line up.

| runtime | parser | script key | meaning |
|---|---|---|---|
| `+0x08 / +0x0c` | `+0x54 / +0x58` | `tile[N] = u v` | scale |
| `+0x10 / +0x14` | `+0x5c / +0x60` | `pan[N] = u v` | scroll, **units per second** |
| `+0x18` | | | base matrix, 16 floats |
| `+0x58` | | (named xforms only) | rotation speed, radians per second |
| `+0x5c` | | | the result matrix, 16 floats |
| `+0x9c` | `+0xe8` | `xform[N]` | transform source, see below |

`xform[N] = $name` selects where the stage's scale/pan/rotation come from:

| value | source |
|---|---|
| 0 | `none` |
| 1 | `identity` |
| 2 | the stage's own `tile[N]` / `pan[N]` |
| 3 | `$blendxform` |
| 4 | `$alphaxform` |
| 5 | `$detailxform` |

For 3, 4 and 5 the apply function copies pan, scale and rotation out of a
context object the caller supplies — the three live at pass pointers `[0xb]`,
`[0xc]` and `[0xd]` — with the same field layout: pan at `+0x10/+0x14`, scale
at `+0x18/+0x1c`, rotation at `+0x20`.

## Composition order

Every frame, `MaterialSystem::Tick(dt)` does `time += dt` and hands that running
total to each stage. The stage matrix is then built by multiplying, in this
order, against **row** vectors (`acc = acc * M`):

```
acc = identity, or the base matrix when the flags byte bit 0 is set
acc = acc * Rotate  (rotation * time)      only when rotation != 0
acc = acc * Translate(panU * time, panV * time)   only when pan != 0
acc = acc * Scale   (tileU, tileV)          only when tile != (1,1)
```

which is, for a texture coordinate `uv`:

```
uv' = ((uv * base) + pan * time) * tile
```

**`tile` multiplies the panned coordinate, not just the coordinate.** A stage
with both scrolls `tile` times faster than the `pan` figure alone suggests.
That is not a corner case: the lava shaders (`lawa*` in `lm.shader`) and
`water.shader` are exactly the ones that set both.

The finished matrix is then **transposed** in place — the shuffle at the end of
`0x1009e5a0` is a plain 4×4 transpose — because the vertex shader reads it with
`dp4` against columns.

## Constant registers

Each stage owns **three** consecutive vertex-shader constants starting at
`c24`, and the apply function uploads matrix rows 0, 1 and 3 into them. So
stage 0 is `c24/c25/c26`, stage 1 is `c27/c28/c29`, and so on.

`tu2_detail.vso` decoded:

```
m4x4 r0, v0, c0           ; position through the world-view-projection matrix
mov  oPos, r0
mad  oFog, r0.z, c9.y, c9.x
dp4  oT0.x, v1, c24       ; diffuse UV  = texcoord0 through stage 0's matrix
dp4  oT0.y, v1, c25
dp4  oT1.x, v1, c27       ; detail UV   = texcoord0 through stage 1's matrix
dp4  oT1.y, v1, c28
mov  oT2, v2              ; lightmap UV passes through on UV set 1
```

Two things fall out of this. The detail map rides **UV set 0**, the same input
as the diffuse — the `tnl` variant of `defaultNTUdetail` says so a second time
with `coordIndex[1] = 0`. And because both are `dp4` against a full 4-vector,
each stage transform is a general 2×2 plus translation, not just a scale.

## The detail map

`CLevel.lua` calls `MESH.SetDefaultDetailMaps(tex, DetailMap.TileU,
DetailMap.TileV)`, defaulting to `8.2` and `7.1`. That reaches
`World::MeshesSetDefaultDetailMaps`, which writes the two floats to every
`WorldMesh` at `+0x6d8` and `+0x6dc`.

Those offsets are exactly where a transform context based at `+0x6c0` puts its
**scale** (`+0x18/+0x1c`), and `+0x6c0` is where the mesh's detail texture name
sits. So `$detailxform` is that context, with pan and rotation left at zero:

```
detail UV = texcoord0 * (DetailMap.TileU, DetailMap.TileV)
```

`WorldMesh::SetupMaterials` (`0x101db660`) also resets the pair to `8.2 / 7.1`
whenever it creates a detail material, so a mesh always has usable tiling even
if the level never called the Lua setter.

## Bonus: texture animation

The second half of `MaterialSystem::Tick` advances animated textures:

```
frame = round(fps * elapsedSeconds) % frameCount
```

That is the formula the particle emitters' `TexAnimFPS` needs too — see the
open item in [`Particles.md`](Particles.md).

## Where the port stands

**Confirmed correct, unchanged:** `pan[N]` in units per second, and detail UV as
`texcoord0 * (TileU, TileV)`. Both were already right; they are now established
rather than assumed, which is what this decode was for.

**Not implemented: `tile[N]`.** A first attempt at it — parsing the key into
`MaterialState` and multiplying it in after the pan, plus routing the stage-1
pan to the blend slot instead of the lightmap — regressed world texturing badly
(`C1L1_Cathedral` came out with garbled tiling on walls and niches) and was
reverted. The *decode* above is believed sound; the fault was somewhere in
wiring it through `fs_world.sc` and the two renderers, and was not identified
before the revert. Anything trying again should:

- check the uniform is set on **every** path that binds `fs_world` (the world
  pass, the entity pass, and any future one) — a stale or unset value there
  would corrupt materials that never mention `tile`;
- verify a map that uses no `tile` at all is pixel-identical before and after;
- only then look at the lava maps.

**Not implemented: the rotation term.** No shipped `.shader` sets a rotation —
it can only arrive through `$blendxform` / `$alphaxform` / `$detailxform`, whose
contexts leave it at zero — so nothing in the data exercises it.

**Open question, separate from all of the above.** `tasmashape` (the Factory
conveyor strip) renders as pale grey mush where the original shows crisp ridges.
Its `texcoord0` spans 58 x 1.6 with an identity slot transform against
`sciana8`, the corrugated wall texture, and 58 repeats across ~300 screen pixels
averages to exactly what we draw. Ruled out: the pan (identical at t≈0), texture
resolution (all 91 diffuse refs resolve, none to another level), a UV channel
mix-up (index 3 of the 2-UV vertex layout is always 0.0, genuine padding), and
transposed u/v (tried; it breaks the walls and leaves the strip unchanged). The
strip is also too *bright*, which points at its lightmap: `texcoord1` spans
u 0.34–0.94, v 0.63–0.87 into an atlas whose upper region is a pale machine
panel and whose lower band is dark.
