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
- Shadows (`WorldMesh::RenderShadowPass`, `MDL.CreateShadowMap`). A dynamic
  light lights through walls within its range.
