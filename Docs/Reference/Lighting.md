# Dynamic lights — decoded

The lights the scripts make at runtime: the flashlight, the torch a Leper or an
EvilMonk carries, the flash an action fires off, and — because `CLight:Apply` is
what places them — every light the level itself authored.

Read out of `Engine.dll` with Ghidra and cross-checked against the shipped data.
Addresses are absolute at the `0x10000000` image base.

| What | Symbol | Address |
|---|---|---|
| `LIGHT.Setup` | type, colour, direction, intensity | `0x101375F0` |
| `LIGHT.SetFalloff` | start, range, cone angle | `0x10137720` |
| `LIGHT.SetIntensity` | | `0x10137820` |
| `LIGHT.SetDynamicFlag` | `Light::EnableDynamic` | `0x101378C0` / `0x101D5F00` |
| `LIGHT.SetFakeSpecularFlag` | `Light::EnableFakeSpecular` | `0x10137970` |
| `LIGHT.SetLitParentFlag` | bit `0x80` at `Entity+0x1a` | `0x10137A20` |
| `LIGHT.SetImportant` | `Light::SetImportantDynamic` | `0x10137AD0` / `0x101D4260` |
| `LIGHT.SetProjector` | `Light::SetProjectorTexture` | `0x1013ED90` / `0x101D4CE0` |
| the attenuation, and the slot score | `Light::GetAttIntensity` | `0x101D4380` |
| what a model's shader constants get | `Light::GetDirAndAttCol` | `0x101D4640` |
| the projector matrix | `Light::UpdateProj` | `0x101D4D90` |
| the world's additive pass | `WorldMesh::RenderLightPass` | `0x101D9740` |
| point lights batched into one | `WorldMesh::RenderUberLightPass` | `0x101D8030` |
| which lights get a pass | `WorldMesh::Draw` | `0x101DAA70` |

Port: `Source/World/Lighting.*` (the light list and the model-side evaluation),
`Source/Game/ScriptLight.cpp` (the natives), `Source/Render/WorldRenderer.cpp`
plus `Shaders/fs_world.sc` (the world mesh), `Source/Render/EntityRenderer.cpp`
plus `Shaders/fs_entity.sc` (models).

The MODEL side — ambient, the environment directional, which lights get a slot —
is documented at the top of
[`Source/World/Lighting.h`](../../Source/World/Lighting.h). The lights
themselves are shaded by the same function as the world mesh; see "Deviations".

## The fields, and the two argument orders that do not match

```lua
LIGHT.Setup(e, type, colour, dirX, dirY, dirZ, intensity)
LIGHT.SetFalloff(e, startFalloff, range, coneAngle)
```

`Setup`'s colour is one packed D3D ARGB int, as `R3D.RGBA` composes it;
`GetDirAndAttCol` reads it back a byte at a time as `(R, G, B)` from
`+0x67a, +0x679, +0x678`. The eighth argument some call sites pass
(`CreateLight` passes `""`) is not read.

**`SetFalloff` takes the start first and stores the range first.** The struct is
range at `+0x7f8`, start at `+0x7fc`; the native calls `Light::SetFalloff(arg2)`
then `Light::SetRadius(arg3)`. Getting that backwards inverts every falloff.

**Type 0 is OFF.** `CLight`'s class default is `Type = 0` and `PlayerLight`
toggles between 0 and 3 to switch the flashlight, so a type outside 1
(directional) / 2 (point) / 3 (spot) lights nothing.

## One cone angle, two cosines

`SetFalloff` derives both cone edges from its single `coneAngle` argument:

```
+0x7f0  cos(a * pi/180)         the OUTER edge
+0x680  cos(a * pi/180 * 0.8)   the INNER edge
```

`a` goes through in degrees, unhalved, and it is measured from the axis — so
`PlayerLight`'s `ConeAngle = 30` is a 60-degree beam. Full brightness inside
`0.8a`, ramping to nothing at `a`. There is no `ConeOuterAngle` property; the
port used to read one and it does not exist in the shipped data.

## The two light passes, decoded from the shipped ps_1_1 binaries

`Data/Shaders/proj.pso` and `ps_atten_dst2.pso` are the pixel shaders behind
`tu2_spotpass` and `tu2_pointpass`. Both are 152 bytes and decode cleanly.

```
; ps_atten_dst2 - a point light
tex t0..t3                          ; atten(xy), atten(z), normcubemap, colormap
mul   r0.rgb, t3.a, v0              ; surface alpha x LIGHT COLOUR
dp3_sat r1, v1_bx2, t2_bx2          ; N.L, the normal against the normalized L
mul   r0.rgb, r0, r1
+add  r0.a, 1-t0.a, -t1.a           ; att = 1 - (x^2+y^2)/R^2 - z^2/R^2
mul   r0.rgb, r0, r0.a
mul_x2 r0.rgb, r0, t3               ; x the surface texture, DOUBLED

; proj - a spot
tex t0..t3                          ; cookie(projected), flashfalloff, normcube, colormap
mul_x2 r0.rgb, t0, v0               ; cookie x LIGHT COLOUR, DOUBLED
+mul  r0.a, t3.a, t1.a              ; surface alpha x the falloff ramp
mul   r0.rgb, r0, r0.a
dp3_sat r1, v1_bx2, t2_bx2          ; N.L
mul   r0.rgb, r0, r1.a
mul_x2 r0.rgb, r0, t3               ; x the surface texture, DOUBLED again
```

So the light pass is **`gain x colour x att x N.L x albedo`** with

```
gain  = 2 for a point light, 4 for a spot
colour = lightColour x min(intensity x 0.5, 1)
```

The `0.5` is `_DAT_102ae5b0`, applied with that `min` in
`WorldMesh::RenderLightPass`; net, a point light lands at `colour x intensity`
and a spot at `2 x colour x intensity`, and both stop climbing past intensity 2.
**Missing the spot's second `mul_x2` is what made the flashlight read as half
as bright as the original's.**

The attenuation is the `add r0.a, 1-t0.a, -t1.a` line over two lookups of
`special/atten.tga`, which is 512x512 and holds `(r/R)^2` — 0 at the centre, 255
at the edge, and 63 at 128 texels out against a predicted 64.3. So

```
att = saturate(1 - (d / Range)^2)
```

quadratic, with no `StartFalloff` term at all. This port evaluates the curve
rather than binding the texture; the texture is exactly a squared radius, so
there is nothing else in it.

`Light::GetAttIntensity` is a different curve — a **linear** ramp, 1 inside
`StartFalloff`, 0 past `Range` — and in the original it is a point light's whole
falloff for a MODEL. Here it is kept only as the ranking function it also is,
the score `Entity::AddLight` sorts slots by; the shading uses the world pass's
curve for models too. See "Deviations".

## The flashlight

`PlayerLight.CLight` is the only template in the shipped data with a
`Projector`, and `PlayerLight:Tick2` is the whole mechanic: it rides
`Player:BindPoint(0.3, 1.6, 0)`, re-aims down `CAM.GetForwardVector()` every
tick, toggles `Type` between 0 and 3 on `UIActions.Flashlight`, and randomly
drops out for 0.05-0.2 s every few seconds — that is the flicker, not a bug.
Cemetery and Asylum call `_r_FlashLight:On()` from their `OnPlay`; everywhere
else the light exists at Type 0 and lights nothing.

`light.shader`'s `tu2_spotpass` is the pass it draws:

```
map[0] = "special/flashlight"    xform[0] = $proj    the cookie
map[1] = "special/flashfalloff"                      the distance ramp
map[2] = $normcubemap                                normalize(L)
map[3] = $colormap               xform[3] = $identity
blend add
```

`Light::UpdateProj` builds `$proj`: a look-at down the light's axis with up
`(0,1,0)`, then a perspective whose **half-fov is `acos(coneAngleCos)`** — the
outer cone exactly — with aspect 1, near `0.1` and far `Range`. The FOV is read
straight off `+0x7f0` (`MOV EAX,[EBX+0x7f0]` at `0x101d4d9f`, through the CRT
`acos` at `0x10287550` into the projection at `0x100b7320`, whose `m[0]` is
`1/tan(halfFov)`).

**Only the COOKIE rides that matrix.** `tu2_proj_1.vso` emits stage 0 as three
`dp4`s against `c24..c26` — `oT0.xy` over a shared `oT0.zw`, so the divide is
the perspective one — but stage 1 is

```
dp4 oT1.x, v0, c14      ; ONE plane row
mov oT1.y, c8.w         ; a constant, the 512x1 texture's row
```

one plane equation, so the falloff ramp is **linear in distance along the beam**,
scaled by `1/Range` (the `1.0/range` `RenderLightPass` builds its texture
matrices from). Reading it at the projected z instead puts it at ~0.97
everywhere, because `near` is `0.1` against a `Range` of 12-18 — which lands in
the texture's fade-out tail and all but extinguishes the beam. That was a real
bug here, and it is what "the flashlight is barely visible" looked like.

`special/flashfalloff.tga` is 512x1: 0 at u=0, full by u≈0.06, holding until
u≈0.78, then down to 0 at u=1. So the beam fades in at the lens, runs at full
strength through most of its length, and dies at `Range`.

`special/flashlight.dds` is 256x256 DXT1: a broad, soft disc peaking at 167/255
in the middle and covering roughly four fifths of the cone. Decode it with
`PainfulTools texdump <DataRoot> special/flashlight out.tga` rather than
inferring it from block endpoints — reading a handful of DXT1 colour pairs
suggested a much tighter disc than it has.

On a FLOOR the beam is dim regardless, because the light sits at eye height and
shines forward: `N.L` on the ground ahead is about 0.16. Judge it against a
wall, not a path — Cemetery, Asylum and Catacombs all spawn you facing open
ground.

## Which lights reach the world mesh

First, what the world mesh has to offer them: a normal. A 2-UV `.mpk` object
(the lightmapped kind) carries no inline normal, and the slot `MapObject::normal`
falls back to holds a packed colour, whose bits read as `NaN`. Nothing noticed
until the dynamic lights - the only thing that reads a world normal - reached
such a chunk, at which point `N.L` was `NaN`, the sum was `NaN`, and the whole
chunk drew black the moment the flashlight came on (City on Water's quays).
`WorldRenderer::Upload` now rebuilds those normals from the faces
(`RepairNormals`), area-weighted, with the winding's sign taken from the
objects that do carry normals (`WindingSign`); the log says how many.

`WorldMesh::Draw` keeps a per-mesh light list (`this+0x528`, count at `+0x608`)
and, after the base pass, issues one `RenderLightPass` per light on it — spots
individually, points batched through `RenderUberLightPass` on shader model 6 and
up. The gate is:

- flag `0x400000` at `Light+0x18`, which is what `Light::EnableDynamic` sets
  (it also files the light in the world's dynamic list at `World+0xb4`); and
- the dynamic-lights video option, which `Light::SetImportantDynamic`
  (`+0x7e0`) overrides — `PlayerLight` sets `Important = true` so the
  flashlight survives it.

**A light that is not dynamic never touches the world mesh**, and it must not:
the level's placed `CLight`s are already in the baked lightmap, so adding them
again would double every torch alcove. `IsDynamic` is exactly the flag that says
"the lightmap does not contain me".

## Where the lights come from in this port

With the script layer running there is no `Level` object, and `CLight:Apply`
places every authored light through `LIGHT.Setup`. Measured on Cemetery: **61
lights from the scripts against the file's 60**, the extra one being the
flashlight. So `EntityLighting::Build` is told to skip the file's `CLight`s
(`lightsFromScripts`) and read only the `CEnvironment` boxes; reading both would
count each placed light twice. The `run` viewer and the `level` report have no
script layer and take the file.

`ScriptEngine::CollectLights` rebuilds the list every frame from the light
entities, at wherever their entity ended the frame — which is what makes a torch
follow the hand that carries it, since `ENTITY.RegisterChild(actor, light, true,
joint)` puts the light on a joint and `UpdateAttached` places it.

A chunk of world takes at most `kMaxDynamicLights` = 8, picked by attenuated
intensity at the chunk centre with `Important` outranking brightness; a model
takes the same eight, picked at its origin. The original has no cap on the world
(it draws one pass per light) and a hard four on models; the passes are folded
into one draw here, so a fixed count is needed, but four was a vertex-constant
budget and there is no reason to inherit it.

## Deviations

The original's dynamic lighting is the weak part of its renderer, and these are
the places this port deliberately does better rather than reproducing it. Each
one is a decision, not a gap.

**Models and the world are lit by the same function.** `Shaders/shared_lights.sh` is
called by `fs_world` and `fs_entity` alike. The original ran two unrelated
paths: the world got the projected per-pixel pass above, while
`Entity::ComputeVSLights` (`0x101D1DC0`) handed the vertex shader four lights
already attenuated **at the entity origin**, with a linear falloff, no cookie,
and a cone edge that cannot follow the beam. A monster standing in a flashlight
was therefore lit flatly, on a different curve, with a different cone, from the
wall a foot behind it — which is what "pasted on" looks like. Here both get
`gain x colour x att x N.L`, per pixel, cookie included.

**Eight lights, not four.** `Entity::MaxLights` is four because that is what
fits `c12..c23`. Every light now arrives through the script layer and a chapel
can place a dozen candles, so the limit buys nothing.

The count is written in exactly one place, `PAINFUL_MAX_DYN_LIGHTS` in the
top-level `CMakeLists.txt`. It reaches C++ as a compile definition on `World`
(`kMaxDynamicLights`, with an `#error` if it is missing) and the shaders as a
shaderc `--define` (`PAINFUL_MAX_DYN`, not defaulted, so an absent one fails on
the array sizes). It has to be one number: bgfx pairs a C++ uniform with a
shader array **by name at runtime**, so two values that disagreed would be
silently wrong lighting rather than a build error.

**A light is scored against a model's SPHERE, not its origin.** The slot score
decides whether a light is handed to the shader at all, so a zero score is not
"no light here", it is "no light on this model anywhere". Evaluated at the
origin, a spot's cone is a knife edge: the instant a monster's origin leaves
the beam the whole model goes dark, with every other slot free, and sweeping
the beam past it pops rather than fades. `LightAttenuation` takes a radius and
tests the bounding sphere against the cone; the per-pixel shader then decides
what is actually lit. `Light::GetAttIntensity` is a point function and is
kept as one - radius `0` reproduces it exactly. The world's chunks are scored
the same way, for the same reason at a larger scale.

**The environment boxes blend in space, not in time.** `Entity::GetEnvironment-
DirLight` snaps to the tightest box the entity is in and lerps toward it over
`DirLight.FadeTime`, so a doorway is a timed fade - and a model standing still
on the line still fades. Here every box is applied outermost first, weighted by
how far inside its faces the point is (`kEnvBlendMargin`, 1 unit - 2 read as
too wide - capped at the box's half-extent so a thin corridor still reaches
full weight), and the
timed fade is gone. `FadeTime` is read and unused. The same blend runs in
`fs_world` for the model shadows' strength, so a shadow and the light it
belongs to cross a box edge together.

**The environment directional does not compete for a slot.** `Entity::ResetLights`
(`0x101D2C70`) `AddLight`s it, so in the original it can be crowded out by four
nearer point lights. It has no position and no falloff, so here it is a term of
its own and always applies.

**Specular is per pixel, from the real eye.** `ComputeVSLights` builds ONE
half-vector per entity out of an unnormalised `(camera - entityPos) + lightDir`,
so `N.H` tracks the view far more than the light and everything facing the
player picks up a sheen. This port uses `normalize(L + V)` at the pixel. The
`lit`-gate on `N.L > 0` is kept but ramped over `u_specular.z`, because per
pixel the step draws a hard line along the `N.L = 0` contour where the
original's per-vertex interpolation smeared it.

**The world takes the analytic attenuation curve** rather than binding
`special/atten` twice: the texture is exactly `(r/R)^2`, so evaluating it is the
same number. The projector's two maps ARE sampled, because their shapes are
authored and not derivable.

**A projector beam is bounded along its axis, not by a sphere**, and nothing
about it ends on a hard edge. Its falloff is the axial ramp, so a spherical cut
at `range` stops the light dead while the ramp is still bright — a curved line
across the floor wherever the sphere is narrower than the beam. Every boundary
of the beam is now a place where a smooth term reaches zero on its own: the ramp
at `zAxial = range`, the cookie's black border at the cone rim (sampled with
clamp, so there is no bounds test to step on), and `N.L` at the terminator. The
same reach feeds the chunk cull (`LightReach`) and the model's slot score — cull
by the sphere and the beam is instead cut off at whichever chunk boundary the
sphere crosses. `Light::GetAttIntensity` measures a spot radially for both; this
does not.

The original has the same artefact, and the user's judgement is that it is
harder to see there than here. It is still an artefact, so it is fixed rather
than reproduced.

## Shadows

The flashlight casts real shadows: the world mesh and every model, into one
depth map, tested per pixel by both. This is a deviation with nothing to
recover behind it. The original's shadows are `MDL.CreateShadowMap(e, size)`,
a `size x size` blob projected under an actor whose template sets `shadow`
(128 where it is set, 0 in `CActor`'s default), and `WorldMesh::RenderShadowPass`
draws those; its dynamic lights shine through walls. Both natives are kept as
switches only: `R3D.EnableShadows` (the menu's Shadows option, called with
`Cfg.Shadows`, a 0/1 number) gates the map, and `CreateShadowMap` is recorded
and not acted on because every model already casts.

**The pass.** `Render/ShadowMap.h`. One view (`Renderer::kShadowView`, ordered
before the sky) renders into a depth-only target through `vs_shadow` /
`fs_shadow`, from the flashlight's own projection - `Light::UpdateProj`'s
half-fov `acos(coneAngleCos)`, aspect 1, near `0.1`, far `Range`, the same
frustum the cookie already covers, so the map and the cookie are bounded alike.
`painful_config.ini` owns it: `FlashlightShadows` (1/0) switches it and
`ShadowMapSize` sizes it (512 by default - 2048 read as too crisp for a torch
beam); `PAINFUL_SHADOWMAP=<size>` overrides both for a run, 0 being off. The
format is
the first of `D24S8`, `D32F`, `D16` the backend can both render and compare
against, and a backend without hardware depth compare logs and runs without.

Casters are whatever writes depth: a chunk or a part whose material blends or
has `depthwrite false` is glass, glow or smoke and is skipped, water is skipped,
and the alpha test is replayed per batch so a grate or a bush casts its holes.
The world pass runs after `WorldRenderer::Draw` and reuses its zone set - the
light sits at the camera, so the camera's rooms are the beam's rooms - with the
light frustum on top. Models are posed for the camera OR the beam, because the
beam reaches past the screen edge (a 30-35 degree half-angle against a
~29 degree vertical half-fov), and a caster just off-screen has to be in this
frame's pose. The view model does not cast: it sits in front of the light and
would black out the beam. `ENTITY.SetPosAndRotRelativeToCamera` clears its
flag.

**Both faces cast.** The level meshes are one-sided, so the usual trick of
rendering back faces alone into the map leaks light through every wall. The
acne that trick avoids is handled on the receiver instead.

**The receiver.** `shared_lights.sh`, in the projector branch, so the shadow
rides exactly the beam: `att *= ShadowTerm(p)` where `p` is the pixel lifted
off its surface by `1.5` texels along the normal and `1.0` texel toward the
light, **in world units at that depth** - a shadow texel is
`2 * zAxial * tan(outer) / size` wide, so the bias grows with the beam and
stays the same fraction of a texel near and far, where a constant depth bias
would be far too large at the lens and useless at range. Nine hardware-compared
taps a texel apart (`Pcf3x3`) give a continuous edge: each tap is bilinear and
ramps over one texel, so a texel apart the ramps overlap. Four taps two texels
apart read as four visible steps, on this map and the models' alike. Outside
the map, or past its far plane, reads as lit; the cookie is black there and the
ramp is zero.

`u_shadowMtx` carries world -> shadow uv/depth with the backend's crop already
applied (y flipped unless `originBottomLeft`, z remapped when
`homogeneousDepth`), and `u_shadowParams` is `(on, normal offset, light offset,
1/size)`. Both go through `LightUniforms` so the world and the models cannot
disagree.

Off (Type 0, `R3D.EnableShadows(0)`, `PAINFUL_SHADOWMAP=0`, or no flashlight in
the level) costs nothing: the view is not touched and the receivers read
`on = 0`.

### Model shadows

The models also cast from the environment directional - the `DirLight` every
`CEnvironment` box carries, which is what lights them. A second `ShadowMap`
(`Renderer::kModelShadowView`, `ShadowMap::BeginOrtho`) is an orthographic box
48 units wide and deep, pushed half its width ahead of the camera, aimed down
the directional the camera's own box gives (`EntityRenderer::DirectionalAt`),
and snapped to its texel grid so it does not shimmer as the camera moves.
**Only the models cast into it.** The world's shadows are baked into its
lightmaps; putting the world in the map would double every one of them and
darken everything under a ceiling.

**Only the static world receives it.** The models cast and are never darkened
by each other or by themselves: the original lit a model from its box alone.
Receiving was tried twice and judged not worth its artefacts. The world is
darkened by
`ModelShadowStrength` percent of its baked light - a synthetic darkening, since
the world is not lit by that directional at all, kept because a figure that
casts nothing floats on the lightmap. That is what the original's
`MDL.CreateShadowMap` blob was reaching for. The shadows fade out over the last
`kEdgeFade` (15 percent) of the box at every edge, so they do not stop on a
line where the box ends in view.

**The world's own occlusion is not consulted.** A figure standing in a
building's baked shade still throws a shadow, and one on a balcony throws it
through the floor onto the wall beneath, the world not being among the
casters. That is how the original's blobs and Half-Life 2's dynamic shadows
behaved, and it is accepted. A third depth map holding the world's own depth
from the light - a gate saying where the light reaches at all - was built and
worked, and was taken out again as a pass too many for what it bought; the
lightmap has no separate shadow term (no shadowmask) that could gate it for
free. The `CEnvironment` factor below is what remains of "is this place in
the sun".

**The shadow is as strong as the boxes say the directional is.** The levels
already carry lit-versus-shade outdoors: a `CEnvironment` in a building's
shadow gives the models a weaker or absent `DirLight`. The world has no
directional term, so `fs_world` blends the same box list the models are lit by
(`DirectionalFactor`, outermost first, the same edge ramp) and scales
`ModelShadowStrength` by the directional's strength there relative to the
level's brightest (`EntityLighting::DirectionalBoxes`). Two earlier answers to
a shadow inside a building's shade - fading with the caster-to-receiver gap,
and gating on the lightmap's own brightness - were tried and dropped, the
second because it varied wildly from map to map, and so was the world-depth
gate above. The box list reaches the shader as
`PAINFUL_MAX_ENV_BOXES` (64, top-level CMakeLists, the same one-number rule as
the lights); a level with more hands the nearest to the camera.

The shipped directions are slanted - `(0.05,-0.05,0.1)` is 66 degrees off
vertical - so the shadows are long.

`painful_config.ini`: `ModelShadows` (1/0), `ModelShadowMapSize` (1024 over 48
units, a texel of about 5 cm), `ModelShadowStrength` (60). `PAINFUL_SHADOWMAP=0`
turns this map off with the flashlight's. `PAINFUL_SHADOWVIEW=1` draws the term
alone - white lit, black shadowed, models at 0.8 - which is how a sign error
in the direction or the depth shows up at a glance; the Cemetery spawn's cart,
bench and cross all throw slanted shadows in it.

The volume lights are untouched by any of this and are still shadowless.

## Shadows from the placed lights

The lamps, torches and candles - every positional light the scripts place -
shadow the models, each other and themselves, and the world beneath them.
`Render/LightShadowAtlas.h`.

**On the world the shadow subtracts.** The lightmap already holds a placed
light, so its shadow cannot be added and a flat darkening would be a guess.
Instead the chunk gets the light packed as BAKED (`u_dynCone.z`): the shader
adds nothing for it and computes what it WOULD contribute at the pixel - the
recovered world-pass arithmetic, gain x colour x `(1 - d^2/R^2)` x `N.L` - and
takes the occluded part of that off the lightmap, clamped at zero. Where the
lamp dominates the texel its shadow is deep; where other light dominates it is
faint; a texel the lamp never reached is untouched. `LightShadowWorldStrength`
(100 percent) scales it, because how the bake's magnitude compares to the
additive gains is not recovered - the two paths were written for different
hardware and only the additive one is decoded. A shadowed light that is
dynamic rather than baked (a carried torch) is already in the chunk's slots
and simply gets its map.

**A budget per frame, not per level.** Cemetery places 60 lights and only a
handful matter to what is on screen, so each frame `EntityRenderer::
PickShadowLights` takes the lights within `LightShadowRadius` (40 units) of
the camera, scores them by colour x intensity weighted by a fade over the
outer third of that radius (`Important` first), and gives the strongest
`LightShadowLights` (8) a slot in the atlas. The rest light without shadows,
as before. The fade also reaches the shader (`u_dynShadow.w`), so a light on
its way out of the radius thins its shadows rather than dropping them; the
first version scored by the slot score at the models in view, which swapped
the set as models moved. The flashlight is left out - it has its own map -
and so are directionals and the fake-specular lights.

**One atlas, six faces a point light.** A slot is a 3x2 block of
`LightShadowMapSize`-texel faces in one depth texture; a point light renders
six 90-degree faces, a spot one face down its cone, each its own bgfx view
with its own rect (`Renderer::kLightShadowViewBase`, 48 views). Casters are the
models inside each face's frustum, opaque parts only, the view weapon excluded
as everywhere.

**The lookup is analytic.** The receiver never sees a face matrix: from the
light-relative vector it picks the face by major axis, takes the face's
forward and up from the same table `LightShadowAtlas.cpp` renders with (right
is `cross(forward, up)` on both sides), and gets uv from the two off-axis
components over the distance along the axis, and the map depth as
`A + B / dist` - bx's right-handed projection rearranged, remapped when the
backend's clip depth is -1..1. So a shadowed light costs the shader one
`vec4` (`u_dynShadow[slot]` = atlas slot, A, B, fade) and the atlas one more;
a spot's half-fov cotangent comes from its cone and a point light's from the
guard band, both derived on both sides. The receiver is lifted off its surface
by texels at its own distance, as the flashlight does it, and BEFORE the face
is chosen: chosen first, a lifted point could step off its face and read as
lit, which drew a straight seam along the face boundary. The faces themselves
render `kGuardTexels` (2) wider than 90 degrees each side, so a lookup at a
face's edge still has neighbours to filter over, and the 3x3 taps are kept
inside the face's cell so a neighbour's face is never read. `Init` runs the same
arithmetic on the CPU against the matrices the faces render with and logs
`lookup check ok` or `FAILED` with the error, which is where a sign slip would
show.

**Two model-lighting mixes.** With the shadows in, the original's model
lighting shows its seam: a barrel's shade side keeps the box's full ambient
and directional while the shadow it casts on the floor loses the whole lamp,
so the two never agree. The old `ModelLighting = 1` answered it by halving the
box ambient and directional on every model (`ModelAmbientScale` /
`ModelDirectionalScale`). `RendererType = 1` took that switch over and dropped
both scales: it replaces the box terms with the traced ambient and the occluded
sun ("Distance field ambient"), and where a model falls back to the box, outside
the window, halving them left it at half of type 0's light (the user's report).
Only `ModelLightScale` (100 percent) remains, on the positional lights. The
world is untouched by the type; the flashlight and directional shadows work in
both. The other half of that seam
is `LightShadowWorldStrength`: where the bake stored less of a lamp than the
analytic term says, the subtraction clamps at zero and the floor shadow goes
black, and lowering it is the fix.

`painful_config.ini`: `LightShadows` (1/0), `LightShadowLights` (8),
`LightShadowRadius` (40), `LightShadowMapSize` (256 per face),
`LightShadowWorldStrength` (100).
`PAINFUL_SHADOWVIEW` darkens the grey models by this term and the world by
the share of its lightmap the subtraction keeps. Cost: up to six depth views per shadowed light with the
models in reach, and nine compares per shadowed light per model pixel.

## Shadows on the view model

The weapon in hand shadows itself from the environment box's directional,
through a map of its own (`Render/ViewModelShadows.h`): orthographic down
that directional, fitted to the weapon's bounding sphere, so
`ViewModelShadowMapSize` (512) texels span about a unit and the map is
densest where the eye is closest. It exists only where the box gives a
directional at all - the level designers' volumes decide whether there is sun
to shadow.

The weapon is the only caster - this is the one place it casts, and on
itself - and the directional the only light. Both were wider once: the world
and the nearby models cast into it, and the flashlight and the three
strongest placed lights had fitted maps of their own, each read by the
weapon in place of the coarser world maps. All of it was taken out again:
surroundings shading a thing held at the eye read as wrong, and the point
lights' self-shadow on a gun was not worth the maps. The receiver is the
view-model draw alone: `u_vmParams.x` marks it and the directional term takes
the map, with the bias in texels as everywhere else.

`painful_config.ini`: `ViewModelShadows` (1/0), `ViewModelShadowMapSize`
(512). `PAINFUL_SHADOWVIEW` darkens the weapon by these terms.

## Distance field ambient

`Pf.RendererType = 1` (console `pfrenderertype 1`) is a prototype and a
deviation with nothing to recover behind it. The original lights a model from
its CEnvironment box's flat ambient, one directional and the lights, so a model
never sees the bright and dark patches, the bounce off a torch-lit wall or the
open sky that the lightmap and sky show around it. `0` is the original and is
left untouched: the shader takes the same term it always did.

**What a model gets.** Its ambient becomes the irradiance of the world around
its bounds centre, as L2 spherical harmonics evaluated at the pixel normal. The
box's directional is a sun inside the window: a model takes only the share of it
that three rays toward it see, from a quarter, half and three quarters up its
bounds' vertical axis, each through the window by the trace's rule (`March`)
and on through the collision mesh (`SdfLighting::SunVisibility`). It is
re-measured whenever the model re-traces and eased over 0.2 s, so walking into
shade does not pop. Outside the window, by the same weight as the ambient, the
box's directional applies whole as before. (Unoccluded it read as artificial
next to the traced light - the user's report - and it went; the sky, drawn in
0..1, could not carry the sun's direction alone, so it came back occluded.) The
lights and their specular are unchanged, and the models' shadows on the world
still fall from the box's directional. The world's light is
baked, so what reaches a surface is its lightmap (x2 on an Overbright level)
and never changes; only the window has to move.

**The field holds the light arriving, not the light leaving.** Storing
`albedo x lightmap`, what a surface sends, is the physical bounce, and it did
not match the world: a model's ambient came out near the on-screen colour of
the walls about it, and the model multiplied that by its own albedo, landing
darker than the wall by the wall's albedo - a dark stone wall passes on a fifth
of its light, and these lightmaps sit at 10-40 of 255. Monsters beside a
torch-lit wall went nearly black (the user's report). So a surface voxel holds
its lightmap by default, what the lightmap says arrives there, the way a light
grid of the Quake 3 kind is lit: a model beside a wall takes the light the wall
takes. `SdfAlbedo` (0 percent) tints it back toward the surface's albedo, 100
being the physical bounce; a change rebuilds the window. An unlightmapped
surface is drawn at full albedo and sends its albedo either way. The placed
lights still reach the model directly, and a lightmap beside a torch already
holds that torch, so near one it is counted twice.

**The cascades.** `Render/SdfLighting.cpp`: three cascades about the camera,
each 128³ voxels and twice the size of the last - 0.25-unit voxels over 32
units, 0.5 over 64, 1 over 128 - the way light propagation volumes cascade (the
user's call; one 64-unit window of 0.5 came first). Each is rebuilt on its own
on a worker thread, finest first, when the camera is a quarter of its size from
its centre, and snapped to four of its own voxels so the grids line up. Every
step of a trace reads the finest cascade holding the point and never strides
past that cascade's face, so a ray from a model starts fine and hands over to
coarser cascades as it goes out; the full-weight fade runs against the
outermost cascade holding the model. Below, "the window" is the union.
Measured on C5L1_City_On_Water's start (2026-09-14): 16,928 / 16,102 / 14,850
surface voxels, each cascade about 215 ms on the worker (21 / 37 / 41 ms
voxelizing, the rest distances), so all three are in about 0.65 s after the
level starts. The surface count stays level across cascades because the area
per voxel grows as fast as the area covered. The level's size costs only load-time data, built on
the first frame of type 1: the triangles of the drawn solid world (no helpers,
bodies, water, `trans` or `decal`), a per-material albedo from the smallest
mip of the diffuse (both terrains' on a blend), and every lightmap decoded.
Voxelizing samples each triangle in the window at half a voxel. A surface voxel
keeps its light in six bins by the facing of what wrote it, so the two sides of
a wall stay apart and a surface seen from behind sends nothing. Every voxel then
takes its nearest surface voxel in two sweeps over the 26 neighbours: an
unsigned distance field that also names the surface.

**The trace.** 32 rays in a spherical Fibonacci set, sphere-traced from the
model's centre, stepping the distance to the nearest surface voxel less one
voxel (at least half). A hit is a surface voxel within one voxel that is not
behind the ray's start. A ray that leaves the window takes the sky when nothing
in the level is in its way, and the model's box ambient when something is (see
"The sky" below). A model outside the window takes the box ambient: full weight
from 6 units inside the window's faces, none within 2. The sum reaches `fs_entity` as `u_sh[9]` with the
cosine lobe applied, and is evaluated per pixel with no texture fetch. A model
re-traces when it moves a tenth of a unit or a new window arrives, at most 64
traces a frame, and fades from the box ambient to its first trace over 0.3 s.
After that the SH it shows eases toward each new trace (0.25 s time constant):
applied whole, every re-trace of a walking monster was a visible jump in its
light (the user's report), because a ray changing outcome - surface to sky, one
voxel's light to the next - moves the sum by a thirty-second at once.

`SdfGain` (100 percent) scales the traced light. `PAINFUL_AMBIENTVIEW=1`
draws the models as their ambient term alone, under either type.

`SdfDebugClipmaps` (console `pfsdfdebugclipmaps 1`) draws the window's surfaces
themselves over the finished frame, under the 2D layer
(`Render/SdfClipmapDebug.cpp`, view `kSdfDebugView`). Each cascade is uploaded when
it first shows: the distance per voxel as a 3D R8 texture in quarter voxels (up
to 64), the nearest surface's index as a 3D R32F texture, and each surface's
six light bins as an RGBA32F texture. `fs_sdfclipmap` then steps a ray per
pixel with the CPU trace's own rule and colours a hit with the bin facing the
eye, so a pixel shows the light a ray from the camera would take there, voxels
and all. Inside the window a miss is dark blue, outside it near black. It runs
under either type.

`SdfDebugGrid` (console `pfsdfdebuggrid 1`) draws the field as probes: a lattice of
spheres 2 units apart, 11 x 5 x 11 about the camera's cell and aligned to the
world, so they stay put as the camera moves (`Render/SdfDebug.cpp`). Each is
lit per pixel by the trace a model at its centre would take, times the gain,
and nothing else. A sphere within a voxel of a surface, or outside the window,
is not drawn, so the lattice's edge is the window's. It runs under either type
(it keeps the window updating), re-traces when a new window arrives, 256 probes
a frame.

The spheres show the ambient at its real brightness, so one with light from
all sides reads flat. On Cathedral's start corridor a probe in the middle
varies about 10% from its top to its equator (47.7 to 52 of 255), while probes
beside the walls carry a first-order term of 32 to 76% of the average - the
field's directional content is there, the middle of a symmetric corridor just
has little of it.

Measured on C1L1_Cathedral from the level start (2026-09-14, Release): the
surface list builds in 140 ms on the first type 1 frame (282,724 triangles,
1609 materials, 55 lightmaps holding 44.6 MB). A window takes about 330 ms on
the worker: 60,061 surface voxels, 133 ms voxelizing, 194 ms of distances. A
trace costs 19 µs, so the budget of 64 is about 1.2 ms on a frame where that
many models need one, and nothing on a frame where none moved. Under
`PAINFUL_AMBIENTVIEW` the pixels that change between the types, the models,
average 59.5 under the box ambient, 45.2 traced with the light leaving
(`SdfAlbedo 100`) and 85.6 with the light arriving (`SdfAlbedo 0`, the
default), 0-255 and none black: the leaving light gave about three quarters of
the boxes' ambient there, the arriving light about half again more than it.

**The sky.** `Render/SkyCapture.cpp` draws the dome once a level, one 32x32
face a frame for six frames (`kSkyCaptureView`, blitted for read-back in
`kSkyCaptureBlitView`), and bins every texel by solid angle into a 64x32
latitude-longitude map of the light from each direction. Each texel's direction
comes back through its face's own inverse view-projection, so the face table
only has to cover the sphere. The sky counts as the light it is drawn with. A
ray that leaves the window is then tested once against the collision mesh
(`CollisionMesh::Occluded`, 4000 units on from where it left): clear, it takes
the sky averaged over its own share of the sphere - the map's cells nearer its
direction than any other ray's, by solid angle, computed once when the sky
arrives - so a small bright sun lands whole on the nearest ray instead of
falling between two; blocked, the box ambient. The trace counts what its rays
found (hit, sky, blocked by the level, out of steps, no sky) and logs the
shares with its timing. A courtyard open
above takes the sky from above and its walls' light from the sides. Until the
six faces are in, and on a level with no sky, every leaving ray takes the box
ambient. A new sky bumps the field's generation, so every model re-traces.
Measured on C5L1_City_On_Water (2026-09-14, 64x64 faces): the map's mean light
is 0.370 0.259 0.178, its luminance 0.603 over the top eighth of rows, 0.332 at
the horizon and 0.000 under the dome's rim - the order a read-back flipped
upside down would reverse - and the sky over each ray's share runs 0.000 to
0.768. With `pfsdfdebuggrid`'s 605 probes traced against that sky, 59% of the
rays hit a surface, 37% took the sky, 4% were blocked by the level and none ran
out of steps, at 10.3 µs a trace. The sky is drawn in 0..1 while an Overbright
level's lightmap reaches 2, so the sky's share of a model's light, and its
direction, is small next to the surfaces': the painted sun carries no more than
white. `SdfSkyGain` (100 percent) scales the sky's light and `SdfSkyHighlight`
(0) lifts its brightest cells - luminance above 0.6, by up to that percentage at
white on a square ramp - before each ray's share is averaged; either change
re-traces every model. On City on Water's start (2026-09-14) the per-ray sky ran
0.000 to 0.768 at highlight 0 and to 1.249 at 200. Against the frame before the
occluded sun and the per-ray sky, 6.5% of the pixels changed, the changed ones -
the models - brightening from 87.5 to 120.4 of 255; highlight 200 moved a
further 4%, from 130 to 139. Before
this, the user saw a blue cast from above on every level under type 1; the box
ambient every upward ray took is the likely cause, not a measured one.

Not handled yet: alpha-tested foliage (solid), a model much larger than a voxel
lit from one point, the sky below the dome's rim (black, as the capture draws
it), and nothing re-voxelized when a destructible's intact twin hides.

## What the scripts do with them

| Who | What |
|---|---|
| `PlayerLight.CLight` | the flashlight, above |
| `Leper:OnTick`, `EvilMonkV3` | a carried torch, flickering: `SetIntensity(i * FRand(0.67,1))` and `SetFalloff` scaled by the same number, every tick |
| `CAction:Action_Light` | a flash — `CreateLight` at intensity 0, then `PFadeInOutLight.CProcess` drives `SetIntensity` up and back down and releases the entity |
| `DriverElectro`, `ElectroDisk`, `Thor` | `CreateLight` per bolt, re-`Setup` each tick at `FRand(1,3)` intensity |
| `CEnvironment:SetDependentLights` | `ENVIRONMENT.RemoveLights` / `AddLight`, only where a level authored `DependentLights`. No shipped level does; both are no-ops here |

## A carried torch sits ON the joint, not beside it

`Leper` and `EvilMonkV3` both do the same three lines: clone the light
template, `obj.Pos:Set(1.0, 0.0, -0.1)` (EvilMonk `1.1, -0.6, -0.2`), then
`ENTITY.RegisterChild(actor, light, true, MDL.GetJointIndex(actor, joint))`.
They never call `PARTICLE.SetParentOffset`.

`Entity::RegisterChild` (`0x101D3250`) records only the parent, the follow flag
and the joint index - **no offset**. The offset is the child's own position
field (`Entity+0x620`), which in the original is a LOCAL translation once the
entity has a parent, so `ENTITY.SetPosition` before the bind is what places it
on the joint.

This port keeps entity positions in WORLD space throughout, so a child bound to
a joint with no explicit offset lands exactly on the joint. For a torch that is
the hand itself, and the authored offsets are in MODEL units (scaled by the
model's own scale, so tenths of a world unit), which is why this is recorded
rather than special-cased: making `RegisterChild` adopt the current position as
a local offset would also catch `BindSoundToEntity`, which sets a WORLD position
before binding.

## Lights and saves

A save has to carry the light state itself. `CLight` has no `RestoreFromSave`
in the shipped scripts, so a restored light's Lua table comes back but nothing
re-runs `LIGHT.Setup` and the engine-side light never exists; `CEnvironment`,
which does have one, re-`Apply`s itself. The original does not need the
script's help - `Light::SaveEntity` / `Light::LoadEntity` are its own.

The symptom, before the world file learned to carry them: a level loaded from a
save had exactly ONE light, the flashlight `Game:OnPlay` creates fresh, and
every placed `CLight` was missing. Models still looked lit because the
flashlight is the light you notice; the world mesh, which only takes the
dynamic ones, had nothing at all to draw. See
[`LuaHost.md`](LuaHost.md), "Saving".

## Not carried

- `LIGHT.SetLitParentFlag` is recorded and not acted on: nothing here asks
  whether the entity a light hangs off is lit by it.
- `ENTITY.AddLight`, which names a light for one entity regardless of the slot
  competition, is still a stub. `PainMenu` is its only caller.
- `WORLD.SetDirLight` is still a stub, so the level's directional comes from the
  file rather than from the script that sets it.
- The original's per-actor blobs (`WorldMesh::RenderShadowPass`,
  `MDL.CreateShadowMap`) - replaced by the flashlight's shadow map and the
  model shadows above. Every OTHER dynamic light still lights through walls
  within its range.
