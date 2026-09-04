# The HUD

Painkiller draws its entire interface from Lua. There is no C++ that knows what
a health bar is: `HUD.lua` asks for quads and strings at pixel coordinates it
computed itself, and the engine's only job is to put them on the screen in the
order it was asked. That makes the 2D layer small - one shader, one vertex
buffer, one batcher - and makes the *contract* the whole of the work. Every
argument order, default and packing below was read out of the shipped
`Engine.dll` rather than inferred from the call sites.

## Where the natives live

The `HUD` and `MATERIAL` module tables sit next to each other in Engine.dll's
native registry at `0x102c2948`, as `{name, function}` pairs:

| native | thunk | engine method |
|---|---|---|
| `HUD.PrintXY` | `0x10126c60` | `HUD::Print` `0x1008d5c0` |
| `HUD.DrawRect` | `0x10126e30` | |
| `HUD.DrawQuad` | `0x10126f00` | `HUD::DrawQuad` |
| `HUD.DrawQuadRGBA` | `0x10127030` | `HUD::DrawQuad` |
| `HUD.DrawQuadRotated` | `0x101271b0` | `HUD::DrawQuadRotated` |
| `HUD.SetTransparency` | `0x101273d0` | |
| `HUD.GetTransparency` | `0x10127350` | |
| `HUD.DrawBossHealth` | `0x10127480` | |
| `HUD.DrawBorder` | `0x10127510` | `HUD::DrawBorder` `0x1008b510` |
| `HUD.GetTextWidth` | `0x101406a0` | `HUD::GetTextWidth` `0x1008b2b0` |
| `HUD.GetTextHeight` | `0x101275d0` | `HUD::GetTextHeight` `0x1008b360` |
| `HUD.SetFont` | `0x10140770` | `HUD::SetFont` `0x10090a20` |
| `HUD.PrepareString` | `0x10140840` | |
| `HUD.StripColorInfo` | `0x10140970` | |
| `HUD.ColorSubstr` | `0x10140a90` | |
| `MATERIAL.Create` | `0x10127690` | `MaterialSystem::CreateTexture` |
| `MATERIAL.Release` | `0x10127740` | |
| `MATERIAL.Replace` | `0x101277c0` | |
| `MATERIAL.Size` | `0x10127850` | |
| `MATERIAL.SetPriority` | `0x101278f0` | |

Every one of them opens with `if (GEngine->renderer != 0)` and returns having
done nothing when there is no renderer. Our natives do the same against
`hud_`, which is what keeps the headless `lua` command running the HUD scripts
without a window.

## Coordinates

Pixels, origin top-left, y increasing downward. `HUD::Print` builds its glyph
quads as `(2*p - 0.5) / screenDim - 1.0` with the y term negated, which is a
plain screen-space ortho.

The scripts do their own resolution scaling. They were authored against
**1024x768** and every layout line looks like

```lua
mw = mw * size * w / 1024
mh = mh * size * h / 768
```

with `w, h` from `R3D.ScreenSize()`. So `R3D.ScreenSize` must report the real
window: it is the only thing standing between the authored layout and the
actual screen.

Two coordinates are special. `HUD.PrintXY` centres the string horizontally when
`x < 0` and vertically when `y < 0`, both against the real screen size - that
is how every banner in the game is positioned.

## The rotated quad — `HUD.DrawQuadRotated`

The compass needle is the only caller (`Hud:QuadRot` / `QuadRotTrans`), and
three things about it are not what the signature suggests.

**It is centred on the PIVOT, not placed at x,y.** `HUD::DrawQuadRotated`
(`0x1008F3C0`) builds its four vertices at `+-0.5 * w` and `+-0.5 * h`
(`_DAT_102ae5b0` / `_DAT_102ae5e8`) — a quad about the origin — then derives
its screen position from the POINT: the left edge it computes is
`pivot.x - w/2`. Drawing from `x,y` as a top-left corner put the needle half
its own size off the hub (the art is 62x92) plus the `(+16, -4)` the script
offsets `x,y` by. The shadow still lands correctly because `Hud:QuadRot` gives
it its own pivot, 5 right and 4 down.

**What `x,y` is FOR is not recovered.** The compass always passes
`pivot = (x + 16, y - 4)` for both the needle and its shadow, so the call site
cannot separate them. Reading the rest of `0x1008F3C0`'s transform would
settle it.

**Rotate in 1024x768, then stretch.** The layout scales width by `w/1024` and
height by `h/768` — different factors off 4:3 — so rotating a quad built that
way shears it: upright it looks right, across it is stretched by the ratio
between them (1.79x at 3440x1440). Making the needle rigid in SCREEN space is
also wrong: the dial behind it takes the same two factors and is drawn as an
ellipse, so a rigid needle stops matching the face it sits on. Rotate in the
authored space, where the needle is rigid against its own dial, and stretch
the turned corners afterwards.

**The angle is negated.** Measured, not reasoned: put a compass target 10
units along `Player.RightVector`, and `RenderCompass` hands the native
`-1.5708` for a target on the RIGHT. The bearing turns the opposite way to a
screen whose y points down. Reasoning "a target on the right is +pi/2" gives
the wrong sign — the chain is `ENTITY.GetOrientation` -> `AngDist` ->
`_arrowRot`, and it is easier to plant a known target and read the number.

## Colours

Packed ARGB throughout, in a 32-bit number passed through Lua.

- `HUD.PrintXY` builds `((r | 0xffffff00) << 8 | g) << 8 | b`, i.e. `0xFFRRGGBB`.
- `HUD.DrawQuadRGBA` builds `((a << 8 | r) << 8 | g) << 8 | b`, i.e. `0xAARRGGBB`.
- `R3D.RGB` / `R3D.RGBA` agree, so one unpack serves all of them.

### Colour codes

`#` followed by one hex digit switches colour mid-string and is not itself
drawn. The sixteen colours come from a static table at `0x103e6220` - four
four-step ramps that are the whole of Painkiller's interface palette:

```
parchment  ffffba7a ffe6a161 ffcd8848 ff9b5616
grey       ffd1d1d1 ffb8b8b8 ff9f9f9f ff6d6d6d
blood      ffd60017 ffbd0000 ffa40000 ff720000
leather    ff6c483a ff532f21 ff3a1608 ff080000
```

`HUD::GetTextWidth` steps over both characters without measuring them, so a
marker costs no width. `HUD::Print` only honours a marker when the running
colour has any RGB at all, so text drawn explicitly black stays black through
one.

### Transparency is not applied by the engine

`HUD.SetTransparency(percent)` stores `round(percent * 2.55)` in a byte and
**nothing in the draw path reads it**. The scripts read it back themselves:

```lua
local trans = HUD.GetTransparency()
self:QuadTrans(self._matHUDTop, ..., trans)   -- passes it as the RGBA alpha
```

Folding it into the vertex colour as well squares the fade and leaves the whole
interface at a few percent alpha - which is exactly what happened here before
the binary was consulted. The argument is a *percentage* (it comes from the HUD
Transparency slider in the options menu); the stored value is 0-255.

## Signatures

```
MATERIAL.Create(name, flags)        -> light userdata (a Texture*), or nil
MATERIAL.Release(mat)
MATERIAL.Size(mat)                  -> w, h   (-1, -1 for a null material)

HUD.DrawQuad(mat, x, y, w, h, color=-1, u1=0, v1=0, u2=1, v2=1)
HUD.DrawQuadRGBA(mat, x, y, w, h, r=255, g=255, b=255, a=255,
                 u1=0.01, v1=0.01, u2=0.99, v2=0.99)
HUD.DrawQuadRotated(mat, x, y, w, h, angle, pivotX, pivotY,
                    r=255, g=255, b=255, a=255)
HUD.DrawRect(x, y, w, h, color)
HUD.DrawBorder(x=0, y=0, w=1024, h=768)

HUD.PrintXY(x, y, text, font=nil, r=0, g=255, b=0, size=0)
HUD.SetFont(name="", size=0)
HUD.GetTextWidth(text)              -> int
HUD.GetTextHeight(text)             -> int
HUD.SetTransparency(percent=100)
HUD.GetTransparency()               -> 0-255
```

Three of these defaults are load-bearing and none of them is guessable:

- **`DrawQuadRGBA`'s UVs default to 0.01/0.99**, not 0/1 - an inset that keeps
  the filter off the edge texels of an icon packed against its neighbours.
- **`PrintXY`'s colour defaults to (0, 255, 0)**, green.
- **`DrawQuadRotated`'s pivot is an absolute screen point**, not an offset and
  not the quad's centre. `Hud:QuadRot` draws the compass arrow at one place and
  turns it about the dial's hub a few pixels away.

A material is light userdata in the original, so the scripts cannot do
arithmetic on it. Scripts pass a literal `0` to mean "no texture", which
arrives as a number and is not userdata - hence "no texture" and "a real
handle" are distinguishable without a sentinel.

## Fonts

Real TrueType, from `Fonts/*.ttf` inside `Fonts.pak`. The engine bakes each
face into a glyph-cell texture at load and `HUD::Print` then emits a quad per
character out of it; we do the same with `stb_truetype`, baking on first use
and keeping the atlas.

`timesbd` is the default. It is what all but one of the shipped `HUD.SetFont`
calls ask for and the only face the HUD scripts print with; `tahomabd` appears
once.

**Font sizes scale with resolution, coordinates do not.** `HUD::SetFont(name,
size)` computes

```
pixels = round(size * (screenH/768 + screenW/1024) * 0.5)
```

from the constants `1/768` at `0x102af108` and `1/1024` at `0x102af160`. At
1024x768 that is 1:1. At 1280x720 a requested 26 becomes 28. This is the one
place the engine does the resolution scaling instead of the scripts, and it is
why text stays proportionate when the panels around it were scaled by Lua.

`HUD.SetFont` is also less powerful than it looks: **`PrintXY` overrides it on
every call**. With a font name it selects that face; with none it calls
`SetFont(0)` - font slot 0, the default - and *not* whatever `SetFont` last
chose. So `SetFont` only really governs `GetTextWidth` and `GetTextHeight`.

Metrics:

- `GetTextWidth(text)` sums per-glyph widths, skipping `#X` pairs, and returns
  the widest line of a multi-line string.
- `GetTextHeight()` with no argument is the font's line height;
  `GetTextHeight(text)` is `(newlines + 1) * lineHeight`.

## What is still missing

- **`HUD.DrawBorder` is not an outline.** In the original it builds a
  `MenuItemBorder` widget - the carved stone frame the menus sit inside - and
  draws that. That widget belongs to the menu stage; a plain outline marks out
  the same rectangle in the meantime, which is enough to lay a menu out against
  and visibly not the shipped art.
- `HUD.DrawBossHealth`, `HUD.PrepareString`, `MATERIAL.Replace` and
  `MATERIAL.SetPriority` are still stubs.
- `HUD::Print` splits a coloured string on the first space and prints the halves
  separately. We emit one run per colour instead, which draws the same pixels
  but would differ if the original's split turned out to affect kerning.
- The rotation direction of `DrawQuadRotated` follows screen-space convention
  (positive angle turns clockwise, y being down). The compass is the only
  caller and has not been checked against the original frame by frame.

## Sizing without a renderer

`MATERIAL.Create` returns nil when it cannot make a texture, and `Hud:Quad`
answers a `MATERIAL.Size` of -1 by printing `'... '..mat..' not found!'` - which
CONCATENATES the handle. A nil there raises inside the diagnostic itself, and
the error unwinds out of `Game:PostRender`, taking `Editor:Render` and
everything after it with it. One unresolved HUD texture kills the whole 2D
layer for the frame, and the traceback names `Hud:Quad`, not the texture.

That is why the headless `lua` report gets the texture INDEX (`TextureCache::
Init(root, createWhite=false)`) even though it has no renderer:
`TextureCache::Measure` parses the image header on the CPU, so `MATERIAL.Size`
answers real dimensions and the scripts' layout arithmetic is the one the
window runs. Drawing is what a headless run is missing, not sizing. Before
this the report threw on all 400 frames and `Hud:Render` never reached a single
`DrawQuad`; after it, 5592 quads over 200 frames.

## Verification

```bash
PAINFUL_SHOT_FRAME=400 PainfulEngine.exe game Data C1L1_Cathedral --shot out.tga --exec "Hud._showFPS = true"
```

`--exec` runs one chunk of Lua after the level loads, which is how a run is
pointed at whatever it is meant to show. The frame report prints
`hud: on, N quads in M draws, K fonts baked`, and the FPS counter is the
cheapest thing in the game that exercises the text path end to end -
`GetTextWidth` for the right-alignment, `SetFont` for the size, `PrintXY` twice
for the drop shadow.
