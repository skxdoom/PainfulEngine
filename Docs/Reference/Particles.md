# Particles — decoded formats and simulation

Everything here was read out of `Engine.dll` / `D3Dev.dll` with Ghidra and
cross-checked against the shipped data. Function addresses are absolute at the
`0x10000000` image base. Nothing in this document is inferred from how "a
particle system usually works".

Source functions used:

| What | Symbol | Address |
|---|---|---|
| `.ini` field mapping | `ParticleSystem::LoadEmitter` | `0x100a4b40` |
| defaults | `ParticleEmitter::ParticleEmitter` | `0x100a34d0` |
| per-particle spawn state | `ParticleEmitter::InitParticle` | `0x100a1cc0` |
| spawn + update | `ParticleEmitter::Tick` | `0x100a1fc0` |
| `.pfx` entry -> emitter transform | `ParticleEffect::EmitterDef::SetupTransform` | `0x101e4a60` |
| per-emitter scaling | `ParticleEmitter::SetScale` | `0x100a19a0` |
| quad builder | (inlined, called from `ParticleEffect::Draw`) | `0x101e6040` |
| blend name -> enum | material script parser | `0x100973a0` |
| blend enum -> D3D states | D3Dev device state setter | `D3Dev 0x10002050` |

## The data chain

A placed particle effect resolves through four files:

```
Levels/<lvl>/CParticleFX/Flame_001.CParticleFX     o.BaseObj, o.Pos, o.Ang/o.Rot, o.Scale
  -> LScripts/Templates/ParticleFX/Flame.CParticleFX      Flame["Effect"] = "Pochodnia"
     -> Scripts/Effects/pochodnia.pfx                     list of emitters + offsets
        -> Scripts/Emitters/swiecznik.ini                 the actual parameters
```

`CParticleFX.lua` drives it: `LoadData` calls `ENTITY.Create(ETypes.ParticleFX, ...)`
then `LoadParticleFX(entity, self.Effect)`, and `Apply` pushes `Scale`, `Pos` and
`Rot` onto the entity.

Note the template uses the **bracket** assignment form,
`Flame["Effect"] = "Pochodnia"`, not `Flame.Effect = ...`. 117 shipped templates
do this — including ones declaring `Pack`, `Mesh` and `Scale` — so the property
parser has to accept it.

43 levels also ship their own `Levels/<name>/Templates` directory whose files
**shadow** the global ones for that level only. `C2L4_Asylum` and `C3L2_Factory`
each define a different `swieczka.CParticleFX` there.

### `.pfx`

A Lua table literal. Only five keys ever appear across all 318 files:

```
ParticleFX =
{
    Emitters =
    {
        { File = "Flame_factory1.ini", Scale = 1.00,
          Position = {0.00,0.00,0.00}, Rotation = {0.00,0.00,0.00} },
    },
    FixedTransform = false
}
```

In the shipped data every `Rotation` is zero, `Position` is non-zero in three
entries, and `FixedTransform` appears in 58 files (`true` in exactly one).

## `.ini` field map

Sections and keys are as `LoadEmitter` reads them. Defaults are what the
constructor installs — which is also, field for field, what `Emitters/Default.ini`
contains.

| Section | Key | Default | Meaning |
|---|---|---|---|
| General | Texture | — | sprite texture |
| | Material | — | material script override |
| | WarpTex | — | distortion texture; `particle_warp` material implies `warp` |
| | TexAnimFPS | 10 | frames/second, read with `atol` |
| | Type | 1 | 1 = camera-facing sprite, 2 = spark streak |
| | BlendMode | 512 | see below |
| | UseColorRange | — | **parsed and discarded** |
| | UseRandomNormal | 0 | flag bit 1 |
| | DepthTest | 1 | flag bit 5 |
| | Evolve | 1 | flag bit 3 — keep spawning past MaxParticles |
| | Warp | 0 | flag bit 7 — wrap position inside the PosRange box |
| | MaxParticles | 128 | live cap |
| | SpawnRate | 100 | stored as **1/rate**, i.e. seconds between particles |
| | Scale | — | **parsed and discarded** |
| | KillDist | 0 | stored **squared** |
| EditorPosition | Pos.X/Y/Z | 0 | authoring aid, overwritten at runtime |
| PosRange | Min/Max | ∓1 | spawn box, rotated by the emitter orientation |
| Velocity | Min/Max | (-80,-6,0)/(-64,6,0) | initial velocity range |
| | Acceleration.X/Y/Z | (0,-0.1,0) | writes **both** ends of the accel range |
| | AccelMax.X/Y/Z | (0,-0.1,0) | raises only the maximum |
| VelocityEnd | Min/Max | = Velocity | velocity blended towards |
| Color | Min/Max | (.5,.1,0)/(1,.5,0) | colour ramp over life |
| | AlphaMin/Max | 0.8 / 0 | |
| | AlphaMid | = AlphaMax | |
| | FadeTimeMin/Max | 100 / 100 | **percent of lifetime** |
| | VelBlendMin/Max | 100 / 100 | **percent of lifetime** |
| | RotationMin/Max | 0 / 0 | spin, rad/s (ctor's ±1 is zeroed before reading) |
| SizeLife | StartSizeMin/Max | 2 / 4 | |
| | EndSizeMin/Max | 8 / 12 | |
| | LifeTimeMin/Max | 0.5 / 0.8 | seconds |
| | Immortal | 0 | particle restarts instead of dying, pinned to the owner |
| SparkEmitter | ThicknessMin/Max | 0.3 / 0.5 | Type 2 quad width |
| | LengthMin/Max | 0.3 / 0.5 | Type 2 multiplier on the velocity vector |

### Two keys that do nothing

`UseColorRange` and `Scale` are both read with `atol`/`atof` and the result is
dropped on the floor.

`UseColorRange` looks meaningful because 824 of 825 files set it to 1 — but the
constructor sets flag bit 4 unconditionally (`flags = 0x39`) and `LoadEmitter`
never clears it. The colour ramp is therefore **always** on for anything loaded
from an `.ini`, and the per-particle random-colour branch in `InitParticle` is
unreachable dead code for that path.

### `Evolve` does not mean "animate"

`Evolve = 0` makes the emitter a **one-shot burst**: `Tick` stops spawning once
the total ever spawned reaches `MaxParticles`. That is what explosions use.
Level-placed effects never see it — `CParticleFX:LoadData` calls
`PARTICLE.SetEvolve(entity, true)` right after loading and `Apply` calls it
again, with the comment `-- only evolve in level`.

### `Warp` is position wrapping, not a distortion effect

Flag bit 7 makes `Tick` wrap each particle back inside the
`[emitterPos+PosMin, emitterPos+PosMax]` box, one axis at a time, by subtracting
or adding the box extent. That is how rain and volumetric mist keep a region
filled. The unrelated `WarpTex` key is the screen-distortion texture.

## Blend modes

`BlendMode` is written by the editor as either the material system's enum value
or, in older files, a single bit. `LoadEmitter` folds the bits first:
`512 -> 1`, `256 -> 5`, `128 -> 2`, `64 -> 4`; values 1, 2, 4, 5 pass through.

The enum comes from the material script parser at `0x100973a0`, and `D3Dev.dll`
turns each value into these D3D render states:

| # | name | SRCBLEND | DESTBLEND | BLENDOP | ZWRITE |
|---|---|---|---|---|---|
| 0 | none | — | — | — | material's |
| 1 | alpha | SRCALPHA | ONE | add | off |
| 2 | add | ONE | ONE | add | off |
| 3 | modulate | DESTCOLOR | ZERO | add | on |
| 4 | filter | ZERO | SRCCOLOR | add | on |
| 5 | translucent | SRCALPHA | INVSRCALPHA | add | material's |
| 6 | invmodulate | ZERO | INVSRCCOLOR | add | off |
| 7 | subtract | ONE | ONE | subtract | off |
| 8 | revsubtract | ONE | ONE | revsubtract | off |
| 9 | desttranslucent | DESTALPHA | INVDESTALPHA | add | material's |
| 10 | destalpha | DESTALPHA | ONE | add | off |
| 11 | modulate2x | DESTCOLOR | SRCCOLOR | add | on |

**`alpha` is not ordinary alpha blending.** It is additive weighted by the
source alpha; `translucent` is the ordinary one. Across the emitters: 527 use
`alpha`, 237 `translucent`, 57 `add`, 1 `filter`.

## Bloom dims the sprites

Particles and coronas are NOT drawn at their authored colour when bloom is on.
The packer behind every particle sprite (FUN_101e4080, called per particle by
the sprite builder FUN_101e6040) and `Billboard::Draw` (0x101CCAE0) both do

```
scale = 255
if (renderFlags & 8) && World.BloomFX.Multiplier > 0:   # render flag 8 = bloom
    scale = 255 * World.BloomFX.DimScale
R,G,B = round(colour * scale);  A = round(alpha * 255)    # alpha is NOT scaled
```

Render flag 8 is `Cfg.Bloom`: `R3D.EnableBloom` (0x101237C0) sets exactly that
bit and `R3D.ApplyVideoSettings` (0x1013F610) reads it from the Cfg table;
`Cfg.lua` defaults it to true. The World fields are the `CLevel.BloomFX` block
pushed by `WORLD.BloomFXParams(LuminanceThreshold, Multiplier, OverlayColor,
DimScale)` - class defaults 0.25, 1, (128,128,128), 0.8 (`World` constructor
0x10061340 initialises +0x6cc..+0x6d8 to the same). 30 levels override some of
it: Catacombs sets `DimScale 0.65` and `LuminanceThreshold 0.05`, seven levels
set `Multiplier 0`, which switches the dimming off.

So in the shipped game a Catacombs torch flame or corona reaches the screen at
65% of its authored colour, and the bloom pass adds a glow on top. The port had
no dimming at all, which is "particles and billboards are quite a bit brighter
than the original" (2026-09-05). `ParticleRenderer::SetColorScale` and
`BillboardRenderer::SetColorScale` now take the factor, from `WorldState`
(`bloom`, `bloomMultiplier`, `bloomDimScale`) in the game and from `LevelInfo`
in the viewer. The bloom post-process itself is not implemented, so a dimmed
sprite is slightly darker here than the original's dimmed-then-bloomed one.

## Fog reaches the sprites

The second half of "brighter than the original" was fog. Every sprite draw -
`ParticleSystem::RenderSprites`, `ParticleEffect::Draw`, `Billboard::Draw` -
first hands the world fog block (`World+0x1608`, what `WORLD.SetupFog` fills)
to the device (D3Dev vtable slot 0xb0, which sets `D3DRS_FOGVERTEXMODE` from
the mode byte and FOGSTART/FOGEND/FOGCOLOR from the rest), then selects the
`simple` vertex shader (slot 0x98 with type 1) and no pixel shader. `simple.vso`
ends with `mad oFog, r0.z, c9.y, c9.x`: it WRITES the fog output, so D3D fogs
the sprite colour toward the fog colour before the blend, alpha untouched.
Under an additive blend that is a dimming with distance whenever the fog colour
is dark - Cemetery's is (18,19,24), Catacombs' (98,98,70) - and it is what makes
a far corona in the original softer and less blocky than ours was. Confirmed
in play on Cemetery from the spawn (2026-09-05). `fs_particle.sc` now applies
the same fog term as the world and entity shaders, fed through
`ParticleRenderer::SetFog` / `BillboardRenderer::SetFog`. The fog COLOUR itself
is not touched by the bloom dim: `SetupFog` (0x10136EA0) round-trips it through
byte-to-float-and-back under the bloom gate with no multiply.

**The fog colour depends on the blend mode.** Fogging an additive sprite toward
the level's fog colour paints the whole quad - `sflare1.dds`, the corona texture
77 of Cemetery's 78 billboards use, is DXT1 with no alpha, so every texel adds -
and the first port of this drew every corona as a grey rectangle. The device
avoids it: D3Dev's state apply (FUN_10002050, vtable slot 0xa0) sets
`D3DRS_FOGCOLOR` from the material's blend mode before each draw:

| blend modes | fog colour |
|---|---|
| 0 none, 5 translucent, 9 desttranslucent | the level's (`SetFog`'s, cached at device +0x79c) |
| 1 alpha, 2 add, 6 invmodulate, 7 subtract, 8 revsubtract, 10 destalpha | black |
| 3 modulate, 4 filter, 11 modulate2x | white |

`FogColorForBlend` in `MaterialState.cpp` is that table; both sprite renderers
pick the colour per draw from the emitter's or corona's blend mode.

## Emitter transform

From `SetupTransform`:

```
emitter.orientation = entity.orientation * entry.rotation      (quaternion product)
emitter.position    = entity.position + entity.scale * entry.position   (NOT rotated)
emitter.scale       = entity.scale * entry.scale
```

`SetScale` then multiplies exactly this set of parameters, and nothing else:
`PosRange`, `Velocity`, `VelocityEnd`, the acceleration range, start/end sizes,
and the spark thickness/length. Lifetimes, colours, alpha, fade timings and spin
are scale-invariant — a shrunken flame burns for just as long.

### Bound effects: where `entity.orientation` comes from

A ParticleFX entity is NAMED after its effect - `AddPFX` passes it as the third
argument of `ENTITY.Create` - which is what `ENTITY.KillAllChildrenByName(se,
"stakeflame")` finds a bound flame by (Physics.md, "The stake").

`ParticleEffect::Tick` (0x101e59a0) places an effect that `RegisterChild` +
`PARTICLE.SetParentOffset` bound to a parent, every frame:

| bound to | position | rotation |
|---|---|---|
| a joint | offset through the joint's world transform | joint rotation × bound Euler, **or the parent entity's rotation when no Euler was given** |
| the entity | parent position + offset rotated by the parent | parent rotation × bound Euler, or left alone |

The Euler is `SetParentOffset`'s 9th..11th arguments, taken only when the
11th exists (`0x10139e30`), converted by the same qz·qy·qx routine as
`EulerToQuat` and kept at `ParticleEffect+0xc8c` behind the flag at `+0xc88`.
Arguments 6..8 are a per-axis pull of the position toward the camera; no
shipped script passes them non-zero.

This is the whole of "which way does the flame go". Emitter velocities are in
the effect's frame — `FTHR1.ini` fires along +X at 57..144 — and
`RifleFlameThrower:EnableFX` binds `RFT_flame` to `joint17` with
`(0, 1.57, 0)`, which is the quarter turn that puts +X down the barrel.
Without the Euler the flame took the weapon's own rotation and pointed
sideways; without any rotation it pointed along world X. Measured after the
port: the flame's local +X comes out as (1.00, 0.08, -0.06) against a forward
of (1.00, 0.00, 0.02).

### `PARTICLE.Die`

0x10139a30: unregister the effect from its parent, then for every emitter zero
the spawn budget (+0xd4), clear the evolve flag and set flag 0x40. The effect
keeps ticking; `ParticleEffect::Tick` deletes the entity once `IsActive` —
any emitter still flagged active (bit 0) — answers false. It is how a bound
continuous effect ends: the flamethrower calls it on the flame the moment the
trigger is released. The port stops the emitters spawning and lets the spent
one-shot reaping in `TickLifetimes` collect the entity when the last particle
has gone.

## Simulation

Per emitter, per frame (`ParticleEmitter::Tick`):

**Spawn.** `accum += dt`; if `accum >= interval`, `n = (int)(accum/interval + 0.5)`,
clamped so `alive + n <= MaxParticles` (and against a global 40960-particle
pool), then `accum -= n * interval`. For `i = 1..n`:

- position = `lerp(prevPos, pos, i/n)` plus a random point in `PosRange` rotated
  by the emitter orientation — so a moving emitter leaves a trail
- `spawnDelta = (i-1) * dt/n`

Note the off-by-one: the particle placed furthest back along the path gets a
**zero** sub-frame step and the newest gets nearly a whole frame, which is the
reverse of what the interpolation implies. It is reproduced as-is.

**Per particle** (`InitParticle`): both velocity endpoints are drawn from their
ranges and rotated into world space; acceleration is drawn but **not** rotated;
lifetime, start size, end size and spin are drawn from their ranges; Type 2 also
draws a thickness and a length.

**Update.** `step = spawnDelta` on the birth frame, `dt` thereafter (the field is
reset to `-2` and compared against `-1`).

```
age += step; if age > life: die (or restart, if Immortal)
t = age/life;  pct = 100*t

vel = velStart                       if pct <  VelBlendMin
      velEnd                         if pct >= VelBlendMax
      lerp between them              otherwise
accelVel += accel*step;  vel += accelVel

alpha = lerp(AlphaMin, AlphaMid, age/(FadeTimeMin*life/100))   if pct < FadeTimeMin
        AlphaMid                                               if pct < FadeTimeMax
        lerp(AlphaMid, AlphaMax, ...) over the remainder       otherwise

pos  += vel*step
pos   = owner position               if Immortal
wrap into the PosRange box           if Warp
size  = lerp(startSize, endSize, t)
rot  += rotSpeed*step
color = lerp(ColorMin, ColorMax, t)
```

With the defaults (`FadeTime*` and `VelBlend*` both 100, `AlphaMid = AlphaMax`)
this collapses to a plain `AlphaMin -> AlphaMax` ramp and a constant velocity.

## Geometry

Four vertices per particle — position, one packed colour (RGB from the ramp,
alpha from the fade curve), and UVs `(0,1) (1,1) (1,0) (0,0)` in that corner
order.

**Type 1**: `centre ± right ± up`, with `right`/`up` taken from the camera basis
scaled by the particle size, rotated about the view axis when the particle spins.

**Type 2**: a streak. One edge sits on the particle, the other at
`pos + velocity * length`; the width runs along
`normalize(cross(velocity, pos - cameraPos)) * thickness`, so the streak always
turns its width towards the camera. The velocity is used **unnormalised**, so
faster sparks are longer.

## What this port does not do yet

- **Texture animation.** `TexAnimFPS` is parsed and stored; the original steps a
  frame index into a multi-frame texture object and rewrites the UVs. Frame 0 is
  always used here.
- **`Warp` / `WarpTex` screen distortion.** The wrap-in-box half of `Warp` is
  implemented; the refraction pass is not.
- **`UseRandomNormal`** is parsed but unused — its effect was not traced.
- **`KillDist`** is parsed (and squared) but nothing culls on it yet.
- **Mesh emitters.** One emitter file carries `Mesh`/`Skin_*`/`FPS` keys and
  renders as a model rather than quads. Not handled.
- **Sorting.** The original does not depth-sort particles either.
- **Fog.** Not applied on the particle pass, matching the original's draw path.
