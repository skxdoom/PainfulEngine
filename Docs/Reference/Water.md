# Water — decoded

Two maps make good references, and they turn out to be different problems:
**City on Water** floats on world geometry, **Swamp** on a placed model.

| What | Symbol | Address |
|---|---|---|
| sets the water flag from the mesh name | `WorldMesh::SetupFlags` | `0x101d7050` |
| picks the water material family | `WorldMesh::SetupMaterials` | `0x101db660` |
| the water draw path | `WorldMesh::RenderWater` | `0x101d8bb0` |
| planar reflection plane | `WorldMesh::GetReflectionPlane` | `0x101d85c0` |
| the swamp surface's wave animation | `Model::SetWaterImpact` | `0x101de8e0` |

## A surface is water because of its name

`WorldMesh::SetupFlags` sets the object's flags from plain `strstr` tests on its
name — case-sensitive, so all-lowercase:

| substring | flag |
|---|---|
| `water` | `0x8000000`, and the mesh is stored in `World+0x778` |
| `trans` | `0x2` |
| `phys` | `0x1000004` |
| `tele` | `0x800000` |
| `noclip` | `0x400000` |
| `2sided` | `0x10000000` |
| `atest` | `0x20000000` |
| `nolight` | `0x100000` |

Terrain (`0x4000000`) is the one exception: not name-based, but "two UV channels
**and** a non-empty blend-map name".

Worth stating plainly, because it looks like it should be file data: it is not.
`Engine.dll` contains the string `"water"` exactly once — the linker folded the
substring test and the material name together — which is easy to misread as
proof that no name test exists.

Eight shipped maps carry world-geometry water: City on Water, Docks, Monastery,
Orphanage, LoonyPark, Fallen1, Fallen2 and Mine. Their objects are named
`water_noclipshape`, `watershape_noclip`, `water_noclip_ashape` and so on.

## The material family

`SetupMaterials` looks first for a material named after the object itself, and
only then falls back to the water family, chosen by device tier and the quality
bits at `World+0x730`:

| condition | material |
|---|---|
| device tier ≤ 4 | `water` |
| quality bits 1 and 4 | `water2_refl_refr` |
| bit 1 | `water2_refl` |
| bit 4 | `water2_refr` |

It also **hardcodes** the two textures the higher tiers need, rather than taking
them from the map: `special/ripples_00` as `$normalmap` and
`special/cube_wenecja` as `$cubemap` (*wenecja* — Venice).

## The tiers are different constructions, not the same picture drawn better

This is the part that matters for porting, and it is why the port pins water to
one variant rather than taking the usual "best available" preference:

```
nv30   one pass, fx = FXWater_20, compiled bytecode inside Water.fxo
nv20   TWO passes:
         1. the lightmap alone, blend none, depthwrite true
         2. blend modulate, depthwrite false, an EMBM pass sampling $cubemap
            through $normalmap tiled 17.5x10 and scrolling at 0.00172 0.003
tnl    one pass: $colormap * $lightmap with modulate2x, xform[0] = $identity
         - no cubemap, no normal map, no tiling, no scroll
```

Note what the `tnl` variant proves: on that tier the map's **own** texture is
what gets drawn (City on Water's `a_woda_to_co5`). On the higher tiers there is
no `$colormap` at all — both `map[0]` and `map[1]` are `$normalmap` — so that
same texture is dead data. Binding `ripples_00` as a colour map, which is what
naively "implementing the water shader" produces, renders a normal map as if it
were albedo: bright green.

## o.Water — where the numbers actually live

Not in `water.shader`, and not in `Engine.dll`. Each level's `.CLevel`
carries an `o.Water` block, and 21 of them do. City on Water's:

```
o.Water.BumpHeight        = 0.2          o.Water.WaveAmplitude = 0.2
o.Water.FresnelBias       = -0.2         o.Water.WaveFrequency = 0.3
o.Water.FresnelExponent   = 5            o.Water.ReflectionAmount = 0.8
o.Water.DeepWaterColor    = Color:New(72,23,0,0)
o.Water.ShallowWaterColor = Color:New(66,79,70,0)
```

`CLevel.lua` gives the class defaults, and two of them settle a loose end:
`Pan = Vector:New(0.00172, 0.003, 0)` and `Tile = Vector:New(17.5, 10, 1)` are
exactly the `pan[0]` / `tile[0]` in `water.shader`. The script is restating the
defaults, so `o.Water` is the authority and levels override it. It also declares
`ReflectScene` and `RefractScene` — the quality bits that make
`SetupMaterials` reach for `water2_refl` / `water2_refr`.

## ripples_00 is a WORLD-space normal map

Worth checking rather than assuming, because it is not the usual thing. Sample
it anywhere and green is pinned at 255, red sits near 128, and blue swings the
full 0..255:

```
(  0,   0)  R 158  G 255  B 254
(128,   0)  R 150  G 255  B   0
(  0, 128)  R 112  G 255  B  73
(128, 128)  R 118  G 255  B 136
```

A tangent-space normal map saturates its THIRD channel. This one saturates the
second, so the texel is already a world-space normal for a horizontal plane -
(x, y, z) with y up - and needs no basis change at all. That is also why the 3x3
water_ref.vso builds is constant.

Read as tangent-space with blue as up, the VERTICAL component swings across
-1..1 and the surface comes out violently bumpy. BumpHeight then scales the two
horizontal components, which is what tilts the normal.

## The nv20 programs, decoded

`water_embm.pso` is five instructions:

```
tex          t0             sample $normalmap at the tiled, panned UV
texm3x3pad   t1, t0_bx2     three rows of a tangent-to-world 3x3, each dotted
texm3x3pad   t2, t0_bx2       with the BIASED normal (2*n - 1)
texm3x3vspec t3, t0_bx2     ...then reflect the eye vector about it and sample
                            the cube map
mad          r0, t3, v0, v1  cube * diffuse + specular
```

and `water_ref.vso` feeds it:

```
dp4 oT0.x, v1, c24        normal-map UV through the stage-0 matrix
add oT1, c4, -v0.wwwx     xyz = a row of the tangent basis, w = eye.x - pos.x
add oT2, c5, -v0.wwwy
add oT3, c6, -v0.wwwz
```

The tangent basis is **constant** — a water surface is a flat horizontal plane —
so only the eye vector travels per vertex.

That same program also displaces `position.y` by a sine, and the constants make
it unambiguous: `c18` is (π, 2π, 1/2π, 0.5), `c19` is (1, −1/6, 1/120, −1/5040),
the Taylor series for sin, and `c13`/`c14` are two wave directions,
`(-1,0,0)` and `(-0.7,0,0.7)`. That is the vertex wave motion, driven by
`WaveAmplitude` / `WaveFrequency` / `WaveSpeed`.

## What this port does

Water surfaces take the **`nv20`** construction, folded into one draw. Its two
passes multiply out — the lightmap, then `blend modulate` over it — so one
shader gives the same result:

- `special/ripples_00` sampled at `(uv + Pan * t) * Tile`, one scrolling layer
  (the nv20 pass declares `tile[0]`/`pan[0]` only; the two-layer sampling is an
  nv30 thing), with `BumpHeight` scaling how hard the normal bends the reflection
- the bumped normal taken to world space through the constant flat-plane basis,
  the eye vector reflected about it, and `special/cube_wenecja` sampled
- multiplied by the lightmap, which is what pass 1 draws

`TextureCache::GetCube` loads the cube; `bimg` already handled cube DDS, there
was simply no call for it.

**Where it deliberately stops.** `mad r0, t3, v0, v1` scales the cube by a
diffuse term and adds a specular one, and `water_ref.vso` builds both from a
`lit()` chain over engine constants. `o.Water` plainly supplies the ingredients,
but *which property feeds which term* is not recoverable:
`WorldMesh::RenderWater` computes those registers from a `TWater` struct and the
decompiler loses the register numbers across that run of setter calls. Guessing
the mapping was tried and produced water that was confidently wrong — too dark,
then a flat tint with the reflection swamped — so the combine stays at what is
decoded. The values are parsed and handed to the shader (`u_water`,
`u_waterDeep`, `u_waterShallow`) ready for whoever pins the mapping down.

Against the reference capture the surface is right in structure — reflective,
correctly tiled, correctly scrolling — but reads darker, because at a grazing
view the reflection samples the cube's side faces rather than its bright top.
Whether the original closes that gap through the missing diffuse/specular terms
or through scene reflection is exactly the open question above.

Everything else is still open:

- **`FXWater_20` / `FXWater2`** live in `Shaders/effects/Water.fxo`, compiled
  D3D effect bytecode — a format not yet decoded.
- **The vertex wave.** The sine chain is decoded but its amplitude and phase
  constants are uploaded per frame from the `TWater` struct; `o.Water` carries
  `WaveAmplitude`, `WaveFrequency` and `WaveSpeed` for it. The surface is flat
  here.
- **Reflection and refraction** (`water2_refl`, `_refr`) need render targets and
  `$fbtex1`, a framebuffer copy. `WorldMesh::GetReflectionPlane` exists, which
  supports the planar-reflection reading of the Swamp reference shot rather than
  a purely cube-mapped one — worth settling before building either.

## Swamp is not world geometry

Its water is a placed `CActor`:

```lua
o.Model = "swamp_dirtywater"
o.Scale = 40
o.waterImpJoint = "root"   o.waterImpAmplitude = 0.2   o.waterImpPeriod = 5.1
o.waterImpRange = 190.0    o.waterImpSpeed = 3.0
o.s_SubClass.RefractFresnel = { dirtywater = { Refract = 2.0, Fresnel = 0.8, … } }
```

`Swamp_dirtywater.pkmdl` holds a single mesh named `dirtywater`, which is the
`skin.shader` entry that gives it its scroll — `palskinned_water` plus
`pan[0] = 0.0 0.03`, with `map[1]`/`map[2] = $envcubemap` and, on the `tnl`
variant, `texgen[0] = reflect worldorient`.

Two general bugs kept it off screen entirely, both now fixed: `o.Model` was
resolved only through the template chain (this instance names its model directly
and inherits from no template), and material overrides were keyed off the model
*file* name instead of the mesh name.

Still missing for Swamp: the cube reflection, the `RefractFresnel` tinting, and
the `waterImp*` vertex waves driven by `Model::SetWaterImpact`.
