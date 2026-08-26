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

## What this port does

Water surfaces take the **`tnl`** variant. It is the only one expressible
without render targets or FX bytecode, and it is fully specified in the script,
so nothing is invented: diffuse times lightmap, doubled.

Everything above it is still open:

- **`FXWater_20` / `FXWater2`** live in `Shaders/effects/Water.fxo`, compiled
  D3D effect bytecode — a format not yet decoded.
- **The EMBM pass** (nv20) needs cube-map sampling, which `TextureCache` cannot
  load yet, plus a bump-perturbed reflection lookup.
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
