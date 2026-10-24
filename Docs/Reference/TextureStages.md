# Texture stages - `texop`, and the constant colour

How a `.shader` pass's `texop[N]` lines become D3D texture-stage state, and the
one per-model input they can read: the constant colour, whose alpha is
`MDL.SetBlendAlpha`. Recovered from the pass parser (`FUN_1009A3B0`), the op
names (`FUN_10096E90`), `D3Dev.dll`'s stage setup (`FUN_10004890`),
`AnimatedMeshMatPal::RenderDefault` (`0x100041D0`) and `Model::SetMaterial` /
`SetBlendAlpha` (`0x101DF4F0` / `0x101DD600`).

## Parsing

`texop[N] = <first> [<op> <arg>]`, and `texop[N].a` (or `.alpha`) the same for
the alpha side; a stage is `0xb0` bytes from the pass's `+0x40`, colour op at
`+0x40`, its argument `+0x41`, alpha op `+0x42`, its argument `+0x43`.

| first word | op |
|---|---|
| `disable` | 1 |
| `texture` alone | 2 |
| `diffuse` | 3 |
| `color` | 4 |
| `texture <op> <arg>` | the op below, and `<arg>` |

| op name | op | D3D colour op | ARG1 |
|---|---|---|---|
| `modulate` | 5 | `MODULATE` | texture |
| `modulate2x` | 6 | `MODULATE2X` | texture |
| `add` | 7 | `ADD` | texture |
| `addsigned` | 8 | `ADDSIGNED` | texture |
| `addsigned2x` | 9 | `ADDSIGNED2X` | texture |
| `blenddiffuse` | 10 | `BLENDDIFFUSEALPHA` | texture |
| `blendtexture` | 11 | `BLENDTEXTUREALPHA` | texture |
| `blendcolor` | 12 | `BLENDFACTORALPHA` | texture |
| `blendprevious` | 13 | `BLENDCURRENTALPHA` | texture |
| `modulatealphaadd` | 14 | `MODULATEALPHA_ADDCOLOR` | **current** |
| `previousmodulate` | 15 | `MODULATE` | current |

Ops 1 to 4 are `DISABLE`, or `SELECTARG1` of texture, diffuse and the texture
factor. `<arg>` is ARG2: `previous` current, `diffuse`, `color` the texture
factor, `texture`, and code 5 current with its alpha replicated. The alpha
side maps the same way through ops 1 to 9.

So `texop[1] = texture modulatealphaadd texture` is
`previous + previous.a * texture`: the second texture added in proportion to
the alpha the first stage left.

## The constant colour

`RenderDefault` draws each part with the part's override material (what
`MDL.SetMaterial` put in the model's parts, `+0x79c`) or the mesh's own, and
sets that material's constant (`Material+0x5dc`, the texture factor) to
`round(Model+0x700 * 255) << 24`: black, with the blend alpha as its alpha.
`Model+0x700` is 1 from the constructor and `MDL.SetBlendAlpha(e, a = 0)`
(`0x1012E450`, models only) sets it.

`Model::SetMaterial(name)` does nothing for the name the model already has
(`+0x7bc`) and otherwise points every part at `FindMaterial(name)`, which for
`""` is nothing: the meshes' own materials are back.

## The frost

`palskinned_freeze` (`skin.shader`) is the one material that reads it:
`texop[0].a = color` makes the first stage's alpha the blend alpha, and
`texop[1] = texture modulatealphaadd texture` with `map[1] = decals/freeze`
adds the frost by it. The scripts drive the weight:

- `FrozenObject` (the freezer, `CActor:Freeze`): 0 to 1 over `FadeInTime`,
  1 while frozen, back to 0 over `FadeOutTime`, then `SetMaterial("")`.
- Thor's hammer and the Stone Golem (`ThorMlot.lua`, `StoneGolem.lua`) flash
  it on a hit: `SetMaterial(effect)` with the alpha at 1, falling at
  `BlendOutSpeed` a second, `SetMaterial("")` at 0.

## The port

`MaterialState` keeps the second stage's op (`stage1Op`), and
`shared_entity.sh` draws op 3 as `lit + blendAlpha * stage1`, the blend alpha
passed per instance in `u_stage1.y`. The entity keeps the alpha and the
override name, the renderer instance gets both again when it is rebuilt, and
the world save carries them (`WsModel`, `+0x700` and `+0x7bc`).

`Model::Tick` (`0x101E2630`) also writes `+0x700`: while `+0xb0 < +0xb4` it
sets `+0xb0 / +0xb4` with `blendpalskinned` and render layer 4, a fade. What
starts it is not traced, and the port has no such fade.
