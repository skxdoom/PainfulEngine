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
| the script test for "did I hit water" | `ENTITY.IsWater` (Lua binding) | `0x10136050` |

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

A map can still swap the family and both textures per object through its
`.EMesh` - "Which water a surface gets", below. Without one, it
**hardcodes** the two textures the higher tiers need, rather than taking
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

## Which water a surface gets

Three layers decide what a water object draws with, and the level's `o.Water`
is only the bottom one.

**The map's `.EMesh`.** `Levels/<name>/MapEntities/<object>.EMesh` is a script
object bound to a named `.mpk` object (`WORLD.FindEntityByName`), and
`CItem:Apply` hands its fields to the mesh natives: `MESH.SetDefaultMaterial`
(the `DefaultMaterial` key), `MESH.SetCubeMap`, `MESH.SetNormalMap`,
`MESH.SetDetailMap`. This is the per-map switch. Every water level with an
`.EMesh` on its water, from the shipped data:

| level | object | DefaultMaterial | CubeMap.Tex | NormalMap.Tex |
|---|---|---|---|---|
| Orphanage | water_noclipshape | `water_ntu_refl` | dm_fragenstein1_cubemap | ripples_00 |
| Lab | woda_noclipshape | `water_ntu_rr` | - | ripples_00 |
| Colosseum | wodashape | `water_ntu_rr` | - | ripples_00 |
| Colosseum | woda2shape | `water_ntu` | dm_mine_cubemap | ripples_00 |
| Docks | watershape | - | skies/wenecja_sky4 | special/ripples |
| City on Water, Monastery, LoonyPark, Fallen1/2, Mine | water_* | - | - | - |

`SetupShaders` (0x101d6850) keys the effect off the material name -
`water_ntu` is `SimpleWaterNTU`, `water_ntu_refl` is `FXWaterNTU_Refl` with
`$fbtex1`, `water_ntu_rr` is `FXWaterNTU_RR` with `$fbtex1` and `$fbtex2` -
and a material that samples a frame-buffer texture carries the flags (8 and
0x10) that `View::Render` reads back as "render a reflection", "render a
refraction". With `Cfg.WaterFX` off (`R3D.SetWaterQuality` to `World+0x1900`)
the two reflecting names fall back to `water_ntu`. A water object with no
`.EMesh` keeps the level-wide family of the section above.

**The environment's Water block.** `CEnvironment.lua` carries the same
`Water` table as `CLevel.lua` plus the reflection fields, and
`ENVIRONMENT.SetWater` (FUN_1013aeb0) stores it per zone at `Zone+0xfb4`;
`WorldMesh::Draw` (0x101daa70) hands `RenderWater` the zone's block instead of
the world's when the mesh sits in one. Three levels author one - Orphanage
(`ReflectDist 275`, `ReflectSky true`, a 70-entry `ReflectList` of islands and
trees, `Tile1 28x7`, `Tile2 22x5`, both pans 0.001), Lab, Colosseum. The
class defaults differ from the level's: both layers start at pan 0, tile 1.

**`ReflectScene` is never set.** Not in any of the 57 `.CLevel` files, not in
any `.CEnvironment`, and the class defaults are `false`, so `World+0x730`
stays 0 and the `water2_refl` branch of `SetupMaterials` is dead in the
shipped game. Every planar reflection you can see comes through an `.EMesh`.

## The four techniques, decoded

`Water.fxo` disassembles (d3dcompiler_47 on the size-prefixed blobs). The
names ARE the effect parameter names, and `RenderWater` (0x101d8bb0) uploads
them by register (`SetVertexShaderConstant`, vtable +0x8c), so the constants
were read from the disassembly of that function:

| register | name | value |
|---|---|---|
| c4..c6 | GBumpSpace | rows of Scale(BumpHeight, 1, BumpHeight) + 1; the shader takes the diagonal - 1 |
| c10 | GEye | the eye in object space, w 0.05 |
| c11 | GRefParams | (1, 1, ReflectionAmount, FresnelExponent) |
| c12 | GFresBias | (1 - FresnelBias, FresnelBias, 1, 1) |
| c13, c14 | GDirs | (-1, 0, 0), (-0.7, 0, 0.7) |
| c15 | GPhase | phase * (0.5, 1.3), phase = WaveSpeed * time |
| c16 | GFreq | (WaveFrequency, 2 WaveFrequency) |
| c17 | GAmpli | (WaveAmplitude, 0.5 WaveAmplitude) |
| c21 | GReflTint | WaterAmount * (Deep - Shallow), w 1 |
| c22 | GRefrTint | WaterAmount * Shallow, w 0 |
| c24.., c27.. | GTexForm0/1 | Scale(Tile) x Translate(Pan * time), per layer |
| c45 | GAspect | the half-texel-corrected 0.5 for the projective coordinates |
| ps c1 | GRefScales | (ReflectScale, RefractScale) |

The colours arrive as `oD0 = Shallow tint`, `oD1 = Deep tint` (c22 + c21).
Every vertex program lifts the vertex in object space before the transform:

```
y += sin(dot(p, dir0) f - 0.5 phase) a + sin(dot(p, dir1) 2f - 1.3 phase) 0.5 a
```

and the pixel programs all start the same way - two samples of the same
world-space normal map at `(uv + pan t) * tile` per layer, `n = n0 + n1 - 1`,
NOT normalised, scaled by `(BumpHeight, 1, BumpHeight)`, and a fresnel
`f = (1 - bias) (1 - n.e)^exp + bias` against the normalised eye vector. Then:

- **FXWater_20** (`water`, ps @23096): `R = 2 (n.E) n - E`, cube map at R,
  `lerp(Shallow, cube * amount + (1 - amount) * Deep, f)`, times the lightmap
  times 2.
- **SimpleWaterNTU** (`water_ntu`, ps @8436): the same without the lightmap.
- **FXWaterNTU_Refl** (`water_ntu_refl`, ps @13292): the reflection target
  sampled projectively at the vertex's own clip position, bent by `n.xz *
  ReflectScale / w`; `glint = sat((f - 0.75) 4 refl)`;
  `f * Deep * (refl * amount + (1 - amount) * Deep + glint) + (1 - f) * Shallow`.
  No lightmap.
- **FXWaterNTU_RR** (`water_ntu_rr`, ps @10720): the same, with the refraction
  target bent by `RefractScale` multiplying the `(1 - f) * Shallow` term.

The `FXWater2_*` techniques (the dead `water2_refl` family) add the reflection
to the cube-map construction and are not ported.

## The planar reflection

`View::Render` (0x100b6720) picks the nearest water mesh, takes its plane
(`GetReflectionPlane`: the mesh's first triangle) and, when a mesh's material
asked for one, renders `RenderReflection` into `World+0x18d0` and
`RenderRefraction` into `+0x18d4` - both created at half the back buffer
(`World::Init`). Past the zone's `ReflectDist` it calls `RenderFakeReflection`
instead, which clears the target and draws nothing. `RenderReflection`
mirrors the view with `Scale(1, -1, 1)`, keeps the zone's `ReflectList`
alone when it has one (`SceneRender::OccludeReflection`) and draws the sky
only with `ReflectSky`; `RenderRefraction` negates the six clip planes and
draws the world under the water from the camera itself.

The port does the same with the tools it has: `Camera::Mirrored` reflects the
eye and the pitch and keeps +Y as the up hint (so the image comes out
mirrored top to bottom and the cull mode swaps for the pass); the world
shader discards the wrong side of the surface (`u_clip`), which is what the
original's user clip planes amount to. An oblique near plane was tried
first and its skewed far plane cut the distant, grazing part of the
reflection off in a ring around the camera. `Camera::Clipped` keeps the
other side for the refraction. `WaterReflection` owns the half-size
targets, views 47-49 draw them before the frame, and the water shader
samples with `v = 0.5 + 0.5 ndc.y` for the mirrored target - the effect's
unflipped `oT5`, not its `oT6` - and the usual `0.5 - 0.5 ndc.y` for the
refraction. The bend uses the RAW normal sum, before BumpHeight (ps @13292
keeps r0 for it and scales a copy for the fresnel); scaling first gave
Orphanage a fifth of its wobble. `PAINFUL_WATER_REFLECT=0` turns the passes
off.

## What this port does

Each water object draws with the technique its `.EMesh` names, its own cube
and normal maps when it names them, and the Water block of the CEnvironment
box it stands in when one carries it - all four techniques above, in one
shader keyed by `u_waterMode`. A reflecting family without its target this
frame degrades one step (rr to refl to ntu).

Still open, and said so:

- `PlaneShift`, `ReflectRadius` (the clip box around the eye) and the
  half-texel term in `GAspect` are read but not applied.
- The refraction target is the world only; the flags `RenderWorld` gets there
  (0x10000f) are not decoded, so what else it includes is not known.
- Models with water materials (`palskinned_water`, the Swamp) are the section
  below, still.
- `Cfg.WaterFX` off (`water_ntu` for everything) is not honoured; the port has
  no video option for it.

## Water the scripts can hit: `ENTITY.IsWater`

`ENTITY.IsWater(e)` (`0x10136050`) checks two things about an entity: that its
type is **1**, `ETypes.Mesh`, and that **bit 27** of its flags is set. Bit 27 is
`0x8000000` — the flag `SetupFlags` sets from the name, above. So the native
answers "is this a world-mesh object whose name says water", and nothing about
it is file data.

Fourteen call sites depend on it, almost all weapons deciding whether a shot
splashed rather than exploded: `Rocket`, `Grenade`, `HeaterBomb`, `MiniGunRL`,
`BoltStick`, `ElectroDisk`, `RifleFlameThrower`, `PainHead`, `Shotgun`, and
`CAiBrain` for whether a bot may walk there.

**Water is not in the collidable world, and cannot be.** Every shipped water
object is *also* named `noclip` — `water_noclip_ashape` in City on Water — and
`noclip` is one of the tokens `MapObject::isCollidable` rejects. That is
correct, because you swim through it. But it means a trace can never come back
holding water, and until the surfaces were registered separately every
`if ENTITY.IsWater(e)` in the game was unreachable no matter what the native
answered. The original has the same problem and solves it the same way:
`SetupFlags` stores the water mesh at `World+0x778` rather than leaving it to
the general geometry path.

So each water object becomes a world-object entity at map load — a name and the
handle `WORLD.LoadMap` reserved for its object ([`LuaHost.md`](LuaHost.md),
"Handles"), no body, no renderer instance, the same kind
`WORLD.FindEntityByName` hands out — and `TraceCommon` tests the segment against the surface directly,
reporting it when it is nearer than the solid hit. Only a **crossing** counts:
a segment wholly above the plane has not hit the water, which is what stops a
shot fired across a lake from reporting one.

The surfaces are flat, so the test is a plane crossing inside the object's XZ
bounds. Measured on City on Water, whose single surface is `water_noclip_ashape`
at raw `y = -8.682` and level scale 0.3:

```
water surface "water_noclip_ashape" at y=-2.60, x[-1002..1217] z[-1173..924]

centre-down  hit=true y=-2.6045837 e=1 water=true     crossing downward
centre-up    hit=true y=-2.6045856 e=1 water=true     and from below
above-only   hit=false                                wholly above the plane
outside-xz   hit=false                                outside the bounds
player-down  hit=true y=1.5218496  e=0 water=false    the pier wins, being nearer
```

## Swamp is not world geometry

Its water is a placed `CActor`:

```lua
o.Model = "swamp_dirtywater"
o.Scale = 40
o.waterImpJoint = "root"   o.waterImpAmplitude = 0.2   o.waterImpPeriod = 5.1
o.waterImpRange = 190.0    o.waterImpSpeed = 3.0
o.s_SubClass.RefractFresnel = { dirtywater = { Refract = 2.0, Fresnel = 0.8,
    ReflTint = Color:New(179,171,149), RefrTint = Color:New(121,121,121) } }
```

`Swamp_dirtywater.pkmdl` holds meshes named `dirtywater` and `water_swamp`,
which are the `skin.shader` entries `dirtywater copy palskinned_water` (pan
0.0 0.03) and `water_swamp copy dirtywater` (pan 0.0 -0.21): `vshader =
palskin_water`, `fshader = skin_dirtywater`, `map[0] = $colormap`,
`map[1]/map[2] = $envcubemap`. Only nv20 and tnl variants exist; there is no
FX water for models, and no planar pass either. What reflects the bonfires
in the water is the cube map:

**`o.RTCubeMap = true`** (Swamp, Leningrad, CTF_Chaos, the two Trainstations)
makes `View::RenderCubemap` (0x100b4c80) draw the world into a 512x512 cube
(`World+0x18c4`, `World::Init`) every frame - six faces, FOV 90, black clear,
`RenderWorld(scene, 0)` each - from **the eye mirrored about
`o.Water.WaterLevel`** (`World+0x72c`; Swamp sets 59.9, the surface's own
height). A model's `$envcubemap` reflection looked up from the real eye then
lands where a planar reflection would, for every point on that plane. The
other levels keep `o.CubeMap.Tex` (`MESH.SetDefaultCubeMaps`) as a static
environment.

**The programs, decoded.** `palskin_water.vso` skins, adds the impact
ripples (four centres at c17..c20, frequency/phase/range/amplitude in
c12..c16 - `Model::SetWaterImpact`'s state, zero when nothing hit the water)
along the normal, and per vertex computes with `c11 = (Refract, Refract^2,
Fresnel, 2)`:

```
I = -e                              (e: vertex to eye, normalised)
T = Refract I + (Refract (n.I) + sqrt(1 - Refract^2 (1 - (n.I)^2))) n
R = 2 (n.e) n - e
f = Fresnel (1 - e.R)^2             (clamped by the colour register it rides in)
oD0 = (ReflTint - RefrTint) f + RefrTint,  oD0.w = f
```

`skin_water.pso` is `lrp(cube(T), cube(R), f) * oD0.rgb`; `skin_dirtywater`
then `lrp(that, colormap, colormap.a)`. `RenderDefault` (0x100041d0)
uploads c11 and the two tints (c21 = Refl - Refr with w 1, c22 = Refr with w
0) from what `MDL.SetMaterialRefractFresnel` stored per mesh
(`Model::SetMaterialRefractFresnel`, 0x101dea90, mesh entry +0x90..+0xac).
The engine negates z for every cube lookup (`mov oT1.z, -r.z` / `oT2.z`),
and its face table looks along -Z for the +Z slot and +Z for the -Z slot:
the cube-map convention (D3D's, which bgfx keeps on every backend) is
left-handed, so a right-handed world can only fill it by mirroring one
axis and undoing that on lookup. A capture with the textbook GL up
vectors mirrors every face instead, and the seams read as a cube-shaped
boundary that moves against the camera.

**What this port does.** `EnvCubeMap` renders sky and world into a 256 cube
(`PAINFUL_ENVCUBE` sets the face size, 0 turns it off) from the mirrored eye
on `RTCubeMap` levels, views 54-65, with the engine's face table (+Y up on
the side faces, +Z / -Z up on the poles, the Z slots swapped) and
`fs_entity_water` sampling `(x, y, -z)`. `EntityRenderer` draws every part
whose material's vertex program is `palskin_water` with `fs_entity_water`:
the two cube looks, the fresnel lerp, the tints and the dirty variant's colour
map, from `MDL.SetMaterialRefractFresnel`. Still missing: the `waterImp*`
ripples, and the cube renders only sky and world, not the models.
