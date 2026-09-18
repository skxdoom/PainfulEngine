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

## Environment boxes

A `CEnvironment` is an axis-aligned box that overwrites the ambient and the one
directional of the ENTITIES inside it; the world mesh keeps its lightmap.
`CEnvironment:Apply` hands the engine every field whether the level authored it
or not - `ENVIRONMENT.SetDirLight(e, Overwrite, Dir.X, Dir.Y, Dir.Z,
Color:Compose(), Intensity, FadeTime)` (`0x1013ac70`: flag 1 at `+0x674`, the
light at `+0x678`, FadeTime at `+0xeac`) - so an unstated field is the class
value in `CEnvironment.lua`, not the level's: Ambient `(30,30,30)`, DirLight
Color `(100,100,100)`, Dir `(-0.7,-0.7,-0.7)`, Intensity 1, FadeTime 1. Of the
shipped boxes that overwrite the directional, 30 leave Dir to the class and 8
the colour (Cathedral's `Dark001`: Intensity 0.5 alone). No box authors a
FadeTime of 0.

**Which box.** `World::CreateEntity` re-sorts the environment list by name
(`World::SortEnvironmentByNames`, `0x1005da90`: unnamed first, then `stricmp`
order), and `World::UpdateEntity` (`0x10058f60`) takes the FIRST box whose
`PointTest` holds the entity. Where boxes overlap, the name decides - not the
size.

**The fade is in time, per entity.** Entering a box, `Environment::AddEntity`
sets the entity's fade length to that box's FadeTime and its progress and step to
0 (`Entity+0xe0/+0xe4/+0xe8`); leaving into no box, `RemoveEntity` does the same
with the box left. `Entity::Tick` (`0x101d1200`) adds `dt / FadeTime` to the
progress while the length is above 0. `GetEnvironmentAmbient` /
`GetEnvironmentDirLight` (`0x101d0ca0` / `0x101d0ec0`) then move the entity's
own ambient, colour, intensity and direction `step / (1 - progress)` of the
way to the box's values - a linear fade over FadeTime - and snap once the
progress reaches 1. An entity's first update snaps. The target is the box's
light when its Overwrite is set, else the level's (`World+0xdf4`, ambient
`World+0x1604`). By that arithmetic a FadeTime of 0 would never move at all.

The port keeps that state in `EntityLightFade` (`EntityLighting::UpdateFade`),
one per model instance, advanced once per frame whether the model is in view or
not. A character's shadow takes its direction from its own fade block, so the
shadow turns with the light on the character that throws it ("Character
shadows"); the view model's maps use one more, kept for the camera
(`EntityRenderer::CameraDirectional`). The specular on a model follows the same
faded directional, per model.

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

**A light that is not dynamic never lights the world mesh**, and it must not:
the level's placed `CLight`s are already in the baked lightmap, so adding them
again would double every torch alcove. `IsDynamic` is exactly the flag that says
"the lightmap does not contain me". With Dynamic Lights at 2 a placed point
light still adds a glint to a mesh that has fake-specular lights ("World
specular").

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
draws those; its dynamic lights shine through walls. `R3D.EnableShadows` (the
menu's "Character Shadows", `Cfg.Shadows`) gates the character shadows below,
not this map ([`Menu.md`](Menu.md), "Video options"), and `CreateShadowMap`
picks their casters.

**The pass.** `Render/ShadowMap.h`. One view (`Renderer::kShadowView`, ordered
before the sky) renders into a depth-only target through `vs_shadow` /
`fs_shadow`, from the flashlight's own projection - `Light::UpdateProj`'s
half-fov `acos(coneAngleCos)`, aspect 1, near `0.1`, far `Range`, the same
frustum the cookie already covers, so the map and the cookie are bounded alike.
`painful_config.ini` owns it: `FlashlightShadows` (1/0) switches it and
`FlashlightShadowMapSize` sizes it (512 by default - 2048 read as too crisp for
a torch beam); `PAINFUL_SHADOWMAP=<size>` overrides both for a run, 0 being off. The
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
would be far too large at the lens and useless at range. Both lifts are scaled
by N.L: a lift slides the lookup along the surface by lift x tan(angle between
the normal and the light), so unscaled they detached a grazing surface's
shadows by 14 texels at 80 degrees and 29 at 85 (peter panning, the user's
report, 2026-09-17); scaled, the slide is lift x sin(angle), under the lift at
any angle. What the lifts no longer cover at grazing angles, the receiver
plane does: the shadow-map depth per uv comes from the position's screen
derivatives (taken before the light loop - D3D has no gradients in flow
control), each of the nine taps compares at the depth the surface has under
it, and half a texel of that slope is taken off for the bilinear compare. The
slope is capped at the depth a slope of 20 gives, `40 * tan(outer) * (1 - z)`
per uv, because at a silhouette the derivatives straddle two surfaces. Nine hardware-compared
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

Off (Type 0, `FlashlightShadows` false, `PAINFUL_SHADOWMAP=0`, or no flashlight in
the level) costs nothing: the view is not touched and the receivers read
`on = 0`.

### Character shadows

**Who casts.** A character is what the original gave a shadow: a model entity
the scripts passed to `MDL.CreateShadowMap(e, size)` with a non-zero size.
`CActor:Apply` does, with the template's `shadow` field (0 in `CActor`, 128
where a template sets it); props, items and the level's placed models never do.
The native (`0x1012e9a0`) reads the size with a default of 128 and calls
`Model::CreateShadowMap(size != 0)`, which keeps the flag at `Model+0x6ac` and
builds the blob only while render flag 2 (`R3D.EnableShadows`) is set.
`Model::SaveEntity` writes that byte, so the flag comes back with a save. The
flashlight's and the placed lights' maps keep every model.

**What the original draws: a blob per character.** `Model::CreateShadowMap`
takes a 128-texel texture from a pool (`FUN_1001b180`). Every tick
`Model::Tick` (`0x101e2630`) fits an orthographic box to the character's bounds,
aimed down a direction of its own - the level's `DirLight`
(`World+0x15d8`), unless the character has lights on it, in which case the
first light's vector, blended toward the second one's by that light's weight.
`View::RenderShadowmaps` (`0x100b2640`) takes up to 24 casters from the scene;
`AnimatedMeshMatPal::RenderShadowmap` (`0x10006160`) draws each one flat white
into its texture over black, a texel of border kept black, and softens it with
four blended passes (weights 0.25 / 0.75). `RenderWorld` lists, per world mesh,
the casters whose box it meets (at most 16), and `WorldMesh::RenderShadowPass`
(`0x101d9f10`) draws that mesh again with the silhouette projected on,
darkening. The fade is a plane through the bounds' max corner along the
caster's direction, scaled by `1 / (0.5 x height x 8)`: the shadow is gone four
heights beyond the character (constants `0.5` at `0x102ae5b0`, `8` at
`0x102c8698`).

**What this port draws: a depth slot per character**
(`Render/CharacterShadows.h`). The same shape - a box per character, the
nearest `CharacterShadowCasters` (24) whose reach is in view, a fade along the
light over `kFadeHeights` (4) heights from the bounds' corner nearest the light
- with three decisions of its own:

- **Depth, not a silhouette.** A white mask also darkens whatever lies between
  the light and the character - the ceiling over it, the wall behind the light.
  The slot stores depth and the receiver compares it, filtered 3x3.
- **The direction is the character's own environment directional**, faded as
  its lighting is ("Environment boxes"), and nothing else. The original's
  steering by the nearest placed light is not kept: the placed lights already
  cast real shadows of their own ("Shadows from the placed lights").
- **One strength everywhere.** Every caster darkens the world by
  `CharacterShadowStrength` (60 percent), whatever its box or level; the
  original's blob did not depend on the light either. Scaling it by the
  caster's directional against the level's brightest was tried and dropped:
  the reference differed from map to map, and so did the shadows.

All the slots live in one square depth atlas drawn in one view
(`Renderer::kCharacterShadowView`): each caster's draw carries its slot's whole
view-projection and crop in the transform, and a scissor keeps it in its cell.
The box is the bounding sphere (radius quantised to a quarter unit, so animation
does not rescale it) snapped to its texel grid. `Add` checks that the caster's
own centre lands inside its cell in front of the far plane (a `PAINFUL_CHECK`).
A world chunk takes up to `PAINFUL_MAX_CHAR_SHADOWS` (8, top-level CMakeLists,
the one-number rule) of the casters whose reach overlaps it, nearest the camera
first; `fs_world` takes the darkest of them at each pixel, so overlapping shadows
merge into one shade rather than stacking darker, as the original's blob passes
did (each a modulate over the last).

**Only the static world receives them.** No model is darkened by them or by
itself: the original lit a model from its box alone, and receiving was tried and
judged not worth its artefacts. The world is darkened, a synthetic darkening
since it is not lit by that directional at all, kept because a figure that casts
nothing floats on the lightmap - which is what the blob was for.

**The world's own occlusion is not consulted.** A figure standing in a
building's baked shade still throws a shadow, as the original's blobs did. A
depth map of the world from the light, as a gate, was built once and taken out
as a pass too many; the lightmap has no shadow term that could gate it for free.

The menu's "Character Shadows" (`R3D.EnableShadows`) is the only switch.
`painful_config.ini`: `CharacterShadowSize` (256 texels a side per character,
32 to 1024), `CharacterShadowStrength` (60), `CharacterShadowCasters` (24, up
to 64). `PAINFUL_SHADOWMAP=0` turns the atlas off with the flashlight's map.
`PAINFUL_SHADOWVIEW=1` draws the term alone - white lit, black shadowed, models
at 0.8. The `--shot` report lists each caster: where its shadow reaches, how far
from the eye, its direction and strength.

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
faint; a texel the lamp never reached is untouched. `ShadowMapStrength`
(100 percent) scales it, because how the bake's magnitude compares to the
additive gains is not recovered - the two paths were written for different
hardware and only the additive one is decoded. A shadowed light that is
dynamic rather than baked (a carried torch) is already in the chunk's slots
and simply gets its map.

**A budget per frame, not per level.** Cemetery places 60 lights and only a
handful matter to what is on screen, so each frame `EntityRenderer::
PickShadowLights` takes the lights within `ShadowMapLightsRadius` (40 units) of
the camera, scores them by colour x intensity weighted by a fade over the
outer third of that radius (`Important` first), and gives the strongest
`ShadowMapMaxLights` (8) a slot in the atlas. The rest light without shadows,
as before. The fade also reaches the shader (`u_dynShadow.w`), so a light on
its way out of the radius thins its shadows rather than dropping them; the
first version scored by the slot score at the models in view, which swapped
the set as models moved. The flashlight is left out - it has its own map -
and so are directionals and the fake-specular lights.

**One atlas, six faces a point light.** A slot is a 3x2 block of
`ShadowMapSize`-texel faces in one depth texture; a point light renders
six 90-degree faces, a spot one face down its cone, each its own bgfx view
with its own rect (`Renderer::kLightShadowViewBase`, 48 views). Casters are the
models inside each face's frustum, opaque parts only, the view weapon excluded
as everywhere - and, for a DYNAMIC light, the static world too
(`WorldRenderer::DrawLightShadows`): a light spawned at runtime is in no
lightmap, so a wall between it and the floor has to cast. A placed light's
faces take the models alone, its world shadows being baked.

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

**The model-lighting seam.** With the shadows in, the original's model
lighting shows its seam: a barrel's shade side keeps the box's full ambient
and directional while the shadow it casts on the floor loses the whole lamp,
so the two never agree. Models keep the original's shading. Two replacements
were built and removed: `ModelLighting = 1` (box terms halved, removed in
72cd7c4) and `RendererType = 1`, model light traced through a distance field of
the level (72cd7c4 to 61342bf, removed 2026-09-17; its notes are in that
history). The other half of that seam
is `ShadowMapStrength`: where the bake stored less of a lamp than the
analytic term says, the subtraction clamps at zero and the floor shadow goes
black, and lowering it is the fix.

**Placed and dynamic lights switch separately.** A light is dynamic when its
script set `LIGHT.SetDynamicFlag` - every light made at runtime is
(`CreateLight`, `AddLight` in `CLight.lua`: muzzle flashes, explosions, the
lightning bolt), and so are the few placed `CLight`s authored `IsDynamic` (4 of
the 143 in the level folders), which the engine's own "Dynamic Lights" option
treats the same way. `PickShadowLights` skips a kind that is switched off.

`painful_config.ini`: `ShadowMapPlacedLights` (1/0), `ShadowMapDynLights`
(1/0), `ShadowMapMaxLights` (8), `ShadowMapLightsRadius` (40), `ShadowMapSize`
(256 per face), `ShadowMapStrength` (100).
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

## Weapon normal maps

Every weapon template ends its setup with `MDL.EnableNormalMaps(self._Entity,
Cfg.WeaponNormalMap)` - the menu's "Hi-Res Weapon Model" writes that key. The
native (`0x1012e500`) sets a flag on the model's mesh (`+0xa4`) and rebuilds its
materials; `AnimatedMeshMatPal`'s setup then gives each mesh that names a
normal map - the `.pkmdl` stores one per mesh, after its name (Formats.md) - the
material `palskinnedperpixel`, unless the texture does not load. 158 meshes
across the weapon models name one (`ASG_PB`, `PKW_PB`, `KK2_2_PB` ...).

**The map is in OBJECT space, not tangent space.** `Skin.fxo`'s `FXSkinBump_30`
vertex shader reads no normal and no tangent: it turns the light and half
directions by the transpose of the vertex's FIRST bone's rotation - into the
model's bind space - and the pixel shader compares them with the map's
`2t - 1` directly. So the port carries the other way round: each posed vertex's
first-bone rotation rows go to the GPU as a second vertex stream
(`vs_entity_nm`), turned into world space there, and `fs_entity_nm` builds the
normal as `t.x * row0 + t.y * row1 + t.z * row2`. An unposed instance takes the
identity.

**The shading** (`FXSkinBump_30`'s pixel shader, up to two lights): per light
`N.L` into the diffuse and `pow(sat(N.H), 10) x sat(N.L) x colour` into the
specular, then `albedo x (ambient + diffuse) + map.alpha x specular`. The
specular mask is the normal map's alpha - which is why `Cfg.WeaponSpecular`
off swaps the normal map itself for `..._pb_no_specular` through
`MATERIAL.Replace` (still a stub here, so that switch does nothing yet). In the
port the directional takes exactly that; the dynamic lights run through the
shared per-pixel path with the same exponent, and all specular is masked by the
map's alpha. The half-vectors are the model's own, as on every model ("Model
specular").

## Model specular

`skin.shader`'s `palskinned` says only `specular true`; the numbers are the
mesh's. `SimpleMesh`'s constructor (`0x1005822c`) gives every mesh a specular
colour of 0.5 grey and a power of 20, `MDL.ResetMaterialSpecular`
(`Model::ResetMaterialSpecular`, `0x101de910`) puts them back, and
`MDL.SetMaterialSpecular(e, mesh, r, g, b, power)` (`0x1013c470`) sets one mesh
by name, the colour in 0-255. `CActor:ApplySpecular` and `CItem:ApplySpecular`
hand it the template's `Specular` table; six monsters carry one (Leper
`{30,30,30,20}` on its four bodies, Vamp `{128,128,128,20}`, Tank zeros on its
metal). palskin multiplies the summed specular by that colour
(`mul oD1, r5, c10`).

**The half-vector is the model's, not the pixel's.** `Entity::ComputeVSLights`
(`0x101d1dc0`) builds one per light per model: `normalize((camera - origin) + L)`,
the camera term UNNORMALISED and `L` the unit direction from the model's origin
to the light. A few units off, the camera term dominates and `N.H` follows the
view: a broad sheen over whatever faces the player on its lit side. The view
weapon sits about 1.35 units from the eye, so there the two terms weigh about
the same. The port builds the same vectors in the pixel shader from
`u_specOrigin`, for the directional and for every point light. It used
`normalize(L + V)` per pixel before, a tight hotspot that read far weaker than
the original on the weapons and the monsters alike.

Kept from before: `lit`'s gate on `N.L > 0` ramps over 0.25 of `N.L`
(`kSpecularGate`), because per pixel the step draws a hard line along the
contour where the original's per-vertex interpolation smeared it. The
normal-mapped weapons take the same half-vectors with their own power (10) and
the map's alpha in place of the colour.

## World specular

With **Dynamic Lights at 2** (`World+0x18fc`, Menu.md, "Video options") the
world mesh has a gloss: a Phong highlight off a gloss map, on the meshes a
level's `MapEntities/*.EMesh` describe. `EMesh:Apply` runs
`MESH.ResetSpecularLights`, one `MESH.AddSpecularLight` per name in
`Specular.Lights`, then `MESH.SetSpecular(e, Specular.Power)` (class default 8).

- `WorldMesh::AddSpecularLight` (`0x101d6d90`) fills the first free of two
  slots (`+0x7e8`, `+0x7ec`) and drops the rest. The names are the level's
  `IsFakeSpecular` lights (`aa_fake1` ...), which light nothing else.
- `WorldMesh::SetSpecular` (`0x101dc520`) stores the power (`+0x7f0`) and loads
  each material's gloss map: its texture name plus `"_s"` (`0x102ca418`) when
  that is on disk, else the diffuse itself. 990 `_s` maps ship. It also picks
  the mesh's light list (`+0x1c`): every light when the mesh has fake lights and
  the setting is 2, else the dynamic ones only (`RenderWorld`, `0x100b3320`).

What draws it (`WorldMesh::Draw`, `0x101daa70`):

- **A mesh with fake lights** draws its base pass as `tu2_fx_gloss`
  (`RenderTU2Specular`, `0x101d7a40`; `Lights.fxo` `FXTU2Gloss`):
  `lightmap x (albedo x overbright + gloss x sum)`, summing over the two lights
  `colour x intensity x 4 x pow(sat(R.L), power) x sat(N.L)`, where `R` is the
  eye ray reflected about the normal. No falloff: `aa_fake1` has Range 5000.
  Constants: `c1`/`c3` each light's colour x intensity / 255 with the power in
  `w`, `c10.x` 2 under Overbright, else 1.
- **The point lights on the list** go through `RenderUberLightPass`
  (`0x101d8030`, `FXUberPointPassTU2`, three to a draw), each with
  `GP_UberLight = (diffuse, lightmap bias, gloss gain, 1/range)`. A placed light
  is `(0, 0, 4)`: a glint only, x4, masked by the lightmap, which already holds
  its diffuse. A dynamic one is `(1, 1, 2)`: diffuse, and an unmasked x2 glint.
  Falloff `sat(1 - d/range)`, the mesh's power. So a placed light glints only on
  a mesh with fake lights; a dynamic one on any `.EMesh` mesh.
- The `tu2_fx_gloss_pointpass` material `SetupShaders` builds (`+0x760`) is
  never drawn.

Cathedral: 37 meshes, 34 with fake lights, 102 gloss maps (the log's
`specular:` line). 1327 `.EMesh` files across the levels name specular lights.

The port folds it into `fs_world` on lightmapped batches: up to five gloss
lights a chunk (`WorldRenderer::kGlossLights`), the two fake ones and then the
three point lights that score highest at the chunk (`kGlossPoints`), each with
its gain and whether the lightmap masks it. The mask is the lightmap after the
model shadows, so a character's shadow takes the glint with the light. Not
carried: the original's four-slot list also holds spots and the fake lights,
which crowd the uber pass; here only point lights compete. The world's dynamic
diffuse keeps the port's curve. `PAINFUL_GLOSSVIEW=1` draws the gloss alone.

## Screen-space ambient occlusion

`Pf.SSAO` (console `pfssao 1`) darkens the scene where its surfaces crowd each
other - corners, the floor under a monster, a barrel against a wall - from the
depth the frame already drew, so it takes the models as they stand. A deviation with nothing to recover behind it: the lightmaps hold
the world's own corners, and nothing held what the models add to them
(`Render/Ssao.cpp`).

It needs the scene in its target (`Render/SceneTargets.h`), which a frame with
SSAO keeps it in, and a depth it can sample: the target's depth is created
readable where the device allows, multisampled like the colour, and read at its
first sample, since depth does not resolve. At half size, each pixel's view-space
position comes back through the inverse projection and its normal from its
neighbours - each axis from the side whose depth runs on smoothly - and 12 taps
on a spiral of seven turns, turned and pushed out per pixel by a 4x4 Bayer tile, are
averaged within `SSAOScreenRadius` (40 thousandths of the screen's height): the
same share of the screen at any distance and any resolution, its radius in the
world growing with the depth. It was a world radius of one unit first: from a
distance the occlusion shrank to a few noisy pixels (the user's report), and at
arm's length a wall spread its taps wide and drew a dark band across itself. The
sky takes none.

Each tap occludes by three factors, multiplied: its elevation over the surface
past `Ssao::kAngleDegrees` (30 degrees, ramped to 1 at straight up), its height above the
surface's plane from a quarter of `Ssao::kHeight` to all of it (40 percent of the
radius, smoothstep), and its distance, falling linearly to 0 at the radius; the
average times `SSAOIntensity` (500 percent, the user's pick in play) comes off. The first estimator was McGuire's Alchemy
obscurance (scaled by 2; his 5 took Cathedral's corners to black), summing
(v.n)/(v.v) under a cubic falloff. That sum is 1/length and was never multiplied
back by the radius, so it grew as the radius shrank toward the camera, and with
next to no angle bias every facet bend of a low-poly model or rock drew a dark
line (the user's report, 2026-09-17). Measured in the lighting-only view,
PAINFUL_NOAI, frame 150, strength 100, intensity 300, against SSAO off: Catacombs' start went
from 20.6% of the frame darker by over 3% (7.7% by over 10%) to 2.3% (0.4%) -
the rock's creases clear, the building's recesses and the wall's foot kept;
Cemetery's from 30.1% (12.5%) to 20.0% (3.2%), most of what remains being the
clouds moving between runs.

Two blurs of nine taps follow, weighted 1 2 3 4 4 4 3 2 1 and each tap weighted
down by how far its depth is from the centre's. Every offset modulo 4 sums to the
same weight, so on a surface the two passes average the 16 turns of the tile out
exactly. The turn was interleaved gradient noise per pixel under a seven-tap
Gaussian first: that noise runs nearly constant along steep diagonals (a step of
one across and two down changes it by 0.06 of a turn), the Gaussian could not
cancel it, and it showed as diagonal streaks sliding with the camera (the user's
report, 2026-09-17). `PAINFUL_SSAOVIEW` writes the blurred occlusion in place of
the scene (with `PAINFUL_BLOOM=0`, or the bloom washes it flat).

The occlusion is multiplied over a scene the world shaders already fogged, so
it has to thin as they do: the occlusion pass takes the level fog (mode,
start, end, density) and the world shaders' own curve over the view distance,
`length` of the view-space position like `v_viewdist`, and scales the darkening
by it. It also fades out between `Ssao::kFadeStart` and `kFadeEnd` (30 and 60
units): far off, the taps span less than a pixel of geometry and the occlusion
flickered on buildings (the user's report, 2026-09-17). A pixel with nothing
left to show skips its taps. Then the result, whole (`Ssao::kStrength`), multiplies the
scene's colour before anything reads the frame (views 68-71, ahead of the haze's
copy and the bloom). The view model, the particles and the coronas draw after
it, in the late views (Particles.md, "The warp sprites"): drawn in the world view,
the dust of a shot at the ground showed the occlusion of the ground behind it
(the user's report), and the weapon's depth would have occluded the wall. Measured on 2026-09-15 (Release, hidden window,
1600x900, 4x MSAA, the depth readable), with the screen radius: by Cathedral's
first monks it changed 8.1% of the frame, the mean level 53.1 to 51.3; on City
on Water's start (its models under the since-removed traced shading), 124.2 to
121.5; on Catacombs'
start, 3.6% of the frame, 68.0 to 67.0.

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
