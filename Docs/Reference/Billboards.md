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

`Color:Compose()` is `R3D.RGBA(r,g,b,a)`, which packs `0xAARRGGBB`. The corona
vertex colour is that RGB with the *faded* alpha substituted, which is why every
shipped template writes `Color:New(r,g,b,0)` — the fourth argument never
mattered.

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

## What this port does not do yet

- `Billboard::Draw` also validates the trace's end point against the world
  before tracing (`FUN_100af200`) and treats a failure as blocked. That check
  is not reproduced; the trace runs unconditionally.
- The billboard rotation field (`+0x6dc`) exists and `Draw` will spin the quad
  about the view axis, but nothing in the data ever sets it.
- The `MaxCoronas` 256 cap is not enforced, because no level reaches it.
- Billboards are not depth-sorted against each other. Neither is the original.
