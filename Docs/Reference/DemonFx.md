# Demon Morph

The render path the game switches to when the player turns demon - 66 souls,
or the `pkdemon` cheat. The world goes to hard black and white, the monsters
burn in a red fresnel glow, the picture warps when the demon strikes and
trails behind itself. Recovered from `View::RenderDemonFXWorld`
(`0x100B5D30`), `AnimatedMeshMatPal::RenderDemonFX` (`0x10005D10`), the four
`WORLD` natives, and the shaders the pass names. The port's pass is
`Render/DemonFx.{h,cpp}` with `Shaders/fs_demon_gray.sc`, `fs_demon_warp.sc`,
`fs_post_copy.sc`, and the model side in `EntityRenderer` with
`fs_demon_entity.sc`.

## The scripts' side

| what | where |
|---|---|
| the switch: `Game:EnableDemon` spawns `DemonFX.CProcess` | `LScripts/Main/Game.lua` |
| `DemonFX:EnableFX` -> `WORLD.EnableDemonFX(on)`, `ENTITY.EnableDemonic(actor, on, true)` for every actor (every enemy player in MP), `demonflame` bound to the death joints | `Templates/Processes/DemonFX.CProcess` |
| the FOV swell 115..125 on a 3 s sine scaled by speed, the white fade-out 0.2 s / fade-in 1 s, 20 s duration, the weapon hidden | same |
| `DemonFXWarp:Tick` -> `WORLD.DemonFXWarp(w)`: `w` rises over 0.2 s on a raised cosine to `MaxWarp` 0.1, then falls over 1 s as `(cos(pi t)/2 + 1/2) * MaxWarp * cos(3.5 pi t)` - the back-and-forth | `Templates/Processes/DemonFXWarp.CProcess` |
| `WORLD.DemonFXParams(Scale, Bias, 1 - MBlur, MBlur)` from `CLevel.DemonFX`, defaults 4, -2.7, 0.7 | `Classes/CLevel.lua`, `ReloadFX` |
| the cheat | `HUD/Console.lua`, `Cmd_PKDEMON` |

The natives (all `GEngine+0xe8` = the World):

| native | address | field |
|---|---|---|
| `WORLD.EnableDemonFX(on)` | `0x10120720` | `+0x6dc` |
| `WORLD.EnableSuperDemonFX(on)` | `0x101207A0` | `+0x6dd` - never set by a shipped script |
| `WORLD.DemonFXParams(a, b, c, d)` | `0x10120820` | `+0x6e0`, `+0x6e4`, `+0x6ec`, `+0x6f0`; defaults 1, 0, 0.3, 0.7 |
| `WORLD.DemonFXWarp(w)` | `0x101209E0` | `+0x6e8` |
| `ENTITY.EnableDemonic(e, on, alsoChildren)` | | the model's demonic flag, read by `RenderWorld` mode 1 |

Ten levels set their own `DemonFX.Scale` (3 to 4.15) and `Bias` (-2.4 to -3.1).

## The chain

`View::Render` takes this branch instead of the ordinary one (and instead of
bloom) whenever `+0x6dc` is set:

```
world     RenderWorld(mode 1) -> 512x512 texture A: everything but the demonic models
gray      A -> B through hud_grayscaleinv (neg_gray.pso), see below
monsters  the demonic models drawn over B in colour with palskinned_fresnel
warp      B through hud_embm (embm_mblur.pso): texbem by special/warp_dudv scaled
          by the warp amount, times (1 - MBlur), plus last frame's result times MBlur
          -> the other 512x512 texture
out       that texture over the screen through the hud material
```

The two 512x512 textures alternate by frame parity (`World[(frame & 1) + 0x62b]`),
which is how last frame's result is there to trail from. `SuperDemonFX` would
add a `ComposeTextures` step with weights 0.3 / 0.7 between the warp and the
output; no level or script turns it on and the port records the flag only.

**The world.** `neg_gray.pso`:

```
mul r0.xyz, t0, v0        ; v0 is the quad's white
dp3 r0.xyz, r0, c0        ; c0 = (0.3, 0.59, 0.11, 0)
mad_x4 r0.xyz, r0, c1, c2 ; c1 = Scale / 4, c2 = (1 - Bias - Scale) / 4
```

so the pixel is `Scale * lum + 1 - Bias - Scale`, clamped by the 8-bit
target: 4 * lum - 0.3 at the class defaults. Black below a luminance of 0.075,
white above 0.325, a straight ramp between. The constants are set in
`RenderDemonFXWorld` at `0x100b6002..0x100b6059` (the 1/4 is the float at
`0x102b49a4`). Despite the material's name nothing is inverted.

**The monsters.** `palskin_fresnel.vso` computes, per vertex, with `V` the
direction to the eye (`c10`) and `N` the skinned normal:

```
R = 2 (N.V) N - V           ; c11.w = 2
f = (1 - V.R)^2 * c11.z     ; = (2 sin^2 theta)^2 * c11.z = 4 sin^4 theta * c11.z
oT1 = (f, f)
```

and the pass is `texop[0] = texture` (`special/fresnel_detail`, the model's
own uv, a bright grey noise) times `texop[1] = texture modulate previous`
(`special/fresnel_func`, a 512x1 ramp: black to u = 0.4, then red rising to
(255,126,0) at u = 1, clamped). Facing surfaces are black, the rim goes red
then orange. The port does the same per pixel (`fs_demon_entity.sc`), from
`v_normal` and `v_wpos`, with the models' fog.

`c11.z` is read from the mesh's material record (`+0x94`, `RenderDemonFX`
at `0x10005e36`); no writer of that field was found in `AnimatedMeshMatPal`
(`RenderInitialize`, `SetupMaterials`), so **its value is not recovered**.
`DemonFx::kFresnelScale` = 0.5 was set by eye against the original's
screenshots: facing black, the body red at 45 degrees, orange from 60. What
would settle it: the constructor of the material record, or a runtime read of
`c11` in the original.

**The warp.** `hud_embm` is `map[0] = warp_dudv` (256x256, signed
V8U8: 128 is no offset, mostly flat with ripples), `map[1]` the grayscale
frame, `map[2]` last frame's result, and `embm_mblur.pso` is `texbem` -
the scene fetched at `uv + dudv * BUMPENVMAT` - times `c0` plus `map[2]`
times `c1`. The bump matrix is `WORLD.DemonFXWarp`'s amount on the diagonal
(the `FUN_1000dbb0` scale matrix built beside the stage setup), `c0` and `c1`
are `DemonFXParams`' third and fourth arguments, `1 - MBlur` and `MBlur`.
The dudv map is sampled at the screen uv, so its 256 texels stretch over the
whole frame. At `MaxWarp` 0.1 the offset reaches a tenth of the screen.

**The trail.** `MBlur` (0.7 by default) is the weight of LAST FRAME'S output
in this one, per frame. That is a frame-rate: at 60 fps the trail's half-life
is two frames, at 30 it is twice as long on the clock.

## Deviations

**Resolution, and the softness.** The original's five effect textures are
512x512 (`World::Init`, `+0x18ac..`), so the picture is squashed to a square
and stretched back, and that stretch is a large part of the look: Demon Morph
is soft. The port draws the scene to the full-size target
([`Bloom.md`](Bloom.md), "Targets") with the backbuffer's multisampling, the
grayscale into a second full-size one that shares the scene's depth (so the
demonic models are depth-tested against the world they were left out of),
then runs the warp and the trail at HALF size and puts the result up
bilinearly - the same softness, at the screen's aspect and without the point
sampling. The warp pass fetching the full-size grayscale at half size is a
2x2 average.

**The dudv bytes are signed.** `warp_dudv` ships as a 24-bit TGA and the
original loads it into a bump format, which reads the bytes as two's
complement: 0 is no offset, 246 is -10, 54 is 54. Read that way the map is a
smooth radial field - `du` runs from about -37 left of centre to +54 right
of it and dies to +-10 at the edges, `dv` the same down the screen, ripples on
top - the "centre distorts back and forth" the strike shows. Read as unsigned
with 128 at rest it is four plateaus of the full warp, the screen cut in four;
reinterpreted in the shader but filtered as unsigned it blends THROUGH 128 at
every zero crossing, a full-warp step along each. So `DemonFx::LoadDudv`
builds the texture itself as signed RG8 (`RG8S`) and the sampler filters it
in signed space. The bump matrix is the warp amount on the diagonal, and the
map peaks at about 0.4, so a strike moves the centre by up to 4% of the
screen at `MaxWarp` 0.1.

**The fresnel is per pixel**, not per vertex. On the original the term is
interpolated across a triangle, which is what puts the glow's shape on the
mesh density; here it follows the normal.

**The trail is frame-rate independent.** The per-frame weight is
`MBlur ^ (dt * 60)`, so the trail lasts the same time at any rate and matches
the original at 60 fps. This frame's weight keeps the level's ratio to it.

**No first-frame trail.** The original's other texture holds whatever was
rendered last, which on the first demon frame is stale; the port starts the
trail from this frame alone.

**Bloom is off** while the pass runs, as in the original (it is the other
branch of `View::Render`). The DimScale on sprites stays, as there.

`PAINFUL_DEMONFX=0` turns the path off for a comparison; `pkdemon` in the
console toggles it in play. The `--shot` log line `demon fx: on/off`.
