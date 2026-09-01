# Billboards and light coronas — decoded

Read out of `Engine.dll` with Ghidra and cross-checked against the shipped data.
Addresses are absolute at the `0x10000000` image base.

| What | Symbol | Address |
|---|---|---|
| the one entry point for every parameter | `BILLBOARD.SetupCorona` (Lua binding) | `0x10137b70` |
| defaults | `Billboard::Billboard` | `0x101cc2d0` |
| distance, occlusion trace, size ramp, quad | `Billboard::Draw` | `0x101ccae0` |
| fade in / fade out | `Billboard::FadeTick` | `0x101cc120` |
| corona flag | `Billboard::SetCorona` | `0x101cc0f0` |
| bounds | `Billboard::UpdateBBox` | `0x101cc410` |
| colour packing | `R3D.RGBA` (Lua binding) | `0x10122c10` |

## There is no separate lens-flare system

`CBillboard` is the whole story — 2790 placed across the shipped levels, 1465 of
them coronas. The "lens flare" look is a sprite texture (`sflare1b`, `flarka`)
hung on an ordinary corona; there is no multi-element flare chain, no
screen-space ghosting pass, and no corona data on `CLight` (checked: zero of the
2829 placed lights carry a `Corona.*` key). `SceneRender::MaxCoronas` caps the
corona list at 256; no shipped level comes close (the most is 112, in
`C4L1_Oriental_Castle`).

## Parameters

Everything reaches the engine through a single call in `CBillboard:Apply`:

```lua
BILLBOARD.SetupCorona(self._Entity, self.Alpha, c.FadeInTime, c.FadeOutTime,
    c.MinSize, c.MinDistance, self.Size, c.MaxDistance, c.OffDistance,
    c.TraceMargin, "Particles/"..self.Texture, self.Color:Compose(),
    self.BlendMode, not self.Corona.Enabled)
```

so the argument order in the binding *is* the field map. Defaults below are the
class defaults from `CBillboard.lua`, which always win — the script passes every
value on every apply, so the C++ constructor's own defaults never survive.

| Key | Default | Meaning |
|---|---|---|
| `Texture` | `"banka"` | resolved as `Particles/<name>` |
| `Size` | 5 | the size reached at `MaxDistance` and beyond |
| `Alpha` | 0.5 | the alpha a fully faded-in corona reaches |
| `Color` | 255,255,255,255 | RGB only; the alpha channel is unused |
| `BlendMode` | 1 | editor index, see below |
| `Corona.Enabled` | false | corona if true, plain sprite if false |
| `Corona.MinSize` | 0.8 | size at or below `MinDistance` |
| `Corona.MinDistance` | 5 | start of the size ramp |
| `Corona.MaxDistance` | 20 | end of the size ramp |
| `Corona.OffDistance` | 70 | past this the corona fades out entirely |
| `Corona.FadeInTime` | 0.5 | seconds |
| `Corona.FadeOutTime` | 0.5 | seconds |
| `Corona.TraceMargin` | 1 | the trace stops this far short of the light |

`CBillboard:Apply` also converts an older light-corona spelling before calling
the binding — `Corona.Texture`, `Corona.Billboard`, `Corona.BlendMode`,
`Corona.AlphaMax`, `Corona.MaxRadius`, `Corona.MinRadius` — commented in the
script as *tryb konwersji ze swiatel* ("conversion mode from lights"). A few
templates still use them. `Corona.MaxSize` and `Corona.DistScale` appear in a
handful of files but nothing reads them.

### BlendMode is an editor index, not the material enum

`SetupCorona` remaps it: `1 -> 1` (alpha), `2 -> 2` (add), `3 -> 4` (filter),
`4 -> 5` (translucent), anything else `-> 0` (none). Those targets are the
material system's blend enum, the same one particle emitters use; the D3D
states each value produces are tabulated in [`Particles.md`](Particles.md).
Note that mode 1, the class default, is `SRCALPHA / ONE` — additive.

### Colour

`Color:Compose()` is `R3D.RGBA(r,g,b,a)`, which packs `0xAARRGGBB` — alpha is
the **fourth** argument (`0x10122c10`). The corona vertex colour is that RGB
with the *faded* alpha substituted, which is why every shipped template writes
`Color:New(r,g,b,0)` — the fourth argument never mattered.

`R3D.RGB(r,g,b)` (`0x10122b70`) is **not** the same shape with alpha zero. It
computes `((r | 0xffffff00) << 8 | g) << 8 | b` = **`0xFFRRGGBB`**, i.e. fully
opaque. Worth stating because the obvious assumption is the other way round,
and a draw that special-cases "alpha 0 means opaque" to compensate would then
be wrong for a genuinely transparent `RGBA`.

Both push a **signed** int, so anything at or above `0x80000000` reaches the
script as a negative number; every native that reads a colour round-trips it
through `int64` on the way back.

## Behaviour

A **plain billboard** (`Corona.Enabled = false`) is a fixed-size, depth-tested,
always-full-alpha sprite. `Draw` runs none of the corona logic for it.

A **corona** does this every frame:

```
distance = |pos - cameraPos|
visible  = false
if distance < OffDistance:
    traceTimer -= dt
    if traceTimer <= 0:                       # at most 10 traces/second
        traceTimer = 0.1
        dir   = normalize(pos - cameraPos)
        from  = cameraPos + dir * 0.3         # start clear of the camera
        to    = pos - dir * TraceMargin       # stop short of the fixture
        blocked = <line trace against solid world geometry>
    visible = not blocked
FadeTick(visible, dt)                          # runs even past OffDistance
if curAlpha > 0:
    size = MinSize                                    if distance <= MinDistance
           ramp MinSize -> Size across the two        if MinDistance < distance < MaxDistance
           Size                                       if distance >= MaxDistance
```

The size growing with distance is what holds a corona at a roughly constant
apparent size on screen as you back away from it, up to `MaxDistance`.

`FadeTick` keeps a timer rather than lerping the alpha directly, and when the
visibility flips it *rebuilds the timer from the alpha already reached*
(`timer = curAlpha/Alpha * fadeTime`). A corona interrupted half way through a
fade therefore reverses from where it is instead of restarting — which is the
whole reason the timer exists.

**Coronas do not depth-test.** That is one bit of the render state handed to
D3Dev, taken straight from the corona flag: set for a plain billboard, cleared
for a corona. Occlusion is the line trace and nothing else, so once the trace
says the light is in view the sprite draws over whatever is in front of it.

### The script can hide one, and that has to fade too

`ENTITY.EnableDraw(e, false)` on a billboard entity must run the sprite down
through `FadeTick`, not cut it. This was missed for a long time because
`spriteSlot` appeared in **no visibility path in the engine at all** — the
entity's `visible` flag reached its model instance and its children and
stopped there. A hidden billboard therefore kept drawing at full alpha until
its entity was released, and then vanished between one frame and the next.

The symptom is a VFX that pops out of existence instead of dimming, and it is
general: anything a script hides rather than releases.

The fade machinery itself was already right — this was only a missing input.

## The line trace

The original asks Havok: `PhysicsWorld::LineTraceFirstHit`. The physics world
holds the collidable map objects — the ones `MapObject::isCollidable` accepts,
excluding portals, zones, volumetric-light helpers and anything named `noclip`.

This port has no physics engine yet, so `World/CollisionMesh` builds a small BVH
over exactly those triangles at level load, in rendered space (raw mesh
coordinates times the level `o.Scale`). It answers only "does this segment hit
anything", which is all a corona needs and much cheaper than finding the nearest
hit. It is deliberately not billboard-specific: line of sight, projectile hits
and AI visibility all want the same query.

Measured: 275k–341k triangles per level, BVH built in 100–170 ms at load, and
the whole billboard update costs 2–11 µs per frame (Cathedral 46 coronas /
338 traces per 300 frames; Oriental Castle 112 coronas / 999 traces).

## Immediate sprites, and the one with an axis

Two of the script-facing draws land in this renderer rather than in the
billboard set, because they are drawn for a single frame and forgotten:

`R3D.DrawSprite(x,y,z, size, rot, argb, texture)` — a camera-facing quad,
turned in the view plane by `rot`. A muzzle flash is the caller, from a
`CProcess:Render`, alive for about 0.14s; consecutive flashes are the same
texture at a different angle, which is what the rotation is for.

`R3D.DrawSprite1DOF(x1,y1,z1, x2,y2,z2, width, argb, texture, [flags])`
(`0x1013f170`) — **one degree of freedom**: the quad's long edge *is* the
segment between the two points, and it turns about that axis to face the eye.
The native builds a `Sprite1DOF` and hands it to
`ParticleSystem::RenderSprites` with the camera, so it is a particle-style
quad rather than anything to do with the corona set.

Two details it is easy to get wrong:

- The side vector is **`axis × lineOfSight`**, not a camera axis. Using the
  camera's right vector makes a beam aimed at the viewer flip inside out as it
  crosses the view direction. When the two are parallel — looking straight down
  the beam — any perpendicular will do, because the quad is edge-on anyway.
- **U runs along the beam, V across it**, so a trail texture stretches from one
  end to the other instead of tiling.

`PainKiller:Render` draws one of these every frame from the gun to its stuck
head — the energy beam the alt fire is named for — gated on the same test that
starts `electro_loop` and `shock_loop`.

## What this port does not do yet

- `Billboard::Draw` also validates the trace's end point against the world
  before tracing (`FUN_100af200`) and treats a failure as blocked. That check
  is not reproduced; the trace runs unconditionally.
- The billboard rotation field (`+0x6dc`) exists and `Draw` will spin the quad
  about the view axis, but nothing in the data ever sets it.
- The `MaxCoronas` 256 cap is not enforced, because no level reaches it.
- Billboards are not depth-sorted against each other. Neither is the original.
