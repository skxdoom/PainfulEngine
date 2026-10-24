# Frame compose - `WORLD.SetRenderTarget` and the hit blur

What the screen does when the player is hit in close combat: the last three
frames echo over each other while the camera jerks. Recovered from the native
(`0x10120580`), `View::Render` (`0x100B6720`), `View::RenderWorldToTexture`
(`0x100B5C80`), `View::RenderPostProcess` (`0x100B1DA0`),
`World::ComposeTextures` (`0x10059870`), and the shaders it names. The port's
pass is `Render/FrameCompose.{h,cpp}` with `Shaders/fs_compose.sc`.

## The native

`WORLD.SetRenderTarget(target, n, tex1, w1, tex2, w2, tex3, w3)` writes the
main view (`GEngine+0xf0` `+0x5d6bf8`):

- `target` outside 1..3: the view's target (`+0xa864`) and count (`+0xa86c`)
  go to 0, which is the ordinary path again.
- otherwise the target is `World+0x18a8 + target * 4`, the count is `n`, and
  each pair stores `World+0x18a8 + tex * 4` at `+0xa870 + i * 4` and the
  weight at `+0xa87c + i * 4`. There is room for three pairs (the weights
  start 12 bytes on) and the loop takes `n` unchecked.

The three textures are `World::Init`'s `+0x18ac`, `+0x18b0`, `+0x18b4`, 512x512:
the first two are Demon Morph's pair ([`DemonFx.md`](DemonFx.md)), the third
the ghost pass's. `+0x18b8` is where the composition lands and `+0x18c0` the
depth they share.

## The frame

In `View::Render`, on hardware above class 1, with neither Demon Morph
(`World+0x6dc`) nor the ghost pass (`+0x6bb`) on, a valid target and a valid
depth surface:

```
RenderWorldToTexture   the world into the target, its viewport resized to it
RenderPostProcess      ComposeTextures(+0x18b8, the view's textures, its weights, n)
                       +0x18b8 over the screen through the hud material
```

and bloom is not drawn that frame: its gate is computed on the ordinary
branch only ([`Bloom.md`](Bloom.md), "Where it sits"). The HUD follows as
always.

`ComposeTextures` caps the count at 4 and draws a quad through `compose2`
(two textures or fewer) or `compose4` (`Shaders/Scripts/custom.shader`), with
`vshader = blur`:

- `blur.vso` passes the position through and adds `c10 + i` to stage `i`'s uv.
  The loop sets every one of those to 0.
- the pixel constant `c[i]` is `(w, w, w, w)`, `w` the weight for `i < n`, 0
  past it; stages past `n` get the material system's default texture.
- `compose2.pso` is `mul r0, t0, c0; mad r0, t1, c1, r0`, and `compose4.pso`
  carries on through `t3`.

So the screen is `sum(weight_i * texture_i)`, every channel.

## `TPlayerHit`

`CPlayer:Client_OnDamage` starts it on an `AIClose` hit in the single-player game,
`Add(1, random(-5, 5), random(-3, 3))`. Every tick for that second, with
`alpha = 1 - t`:

| tick | target | stages |
|---|---|---|
| 1st | 3 | 3, 3, 3 |
| 2nd | 2 | 3, 2, 3 |
| 3rd | 1 | 1, 2, 3 |
| then 3, 2, 1, ... | | 1, 2, 3 |

with weights `alpha/2, alpha/2, 1 - alpha`, so a texture is only read once it
has been drawn. The camera's rotation displacement swings between the
`-1, 0, 1` phases of the same counter, fading with `(1 - t) cos(t pi / 2)`,
which is what the frames echo. The end, or `Delete`, sets the target to 0.

So each texture holds one phase of the shake: texture 1 the view turned by
`-d`, texture 2 by `+d`, texture 3 the centre. The screen is meant to be the
centre view with a ghost on either side, fading out over the second.

**What goes wrong.** Each texture is drawn on one tick in three and read on
all of them, so each of the three layers moves at a third of the tick rate.
While the view turns, the picture cross-fades between stale snapshots
instead of moving: the hit reads as a drop to 20 or 30 frames a second.

## The port

`FrameCompose` keeps three half-size textures, like Demon Morph's trail and
for the same reason ([`DemonFx.md`](DemonFx.md), "Deviations"). Demon Morph
takes precedence, and bloom is skipped on those frames, as in the original.

**By default it does not reproduce the stutter.** The rotation displacement
is left out of the camera while the blend is on (`TakeCameraPose`), and the
displacement in force on the frame each texture is the target is kept as that
texture's turn. Every stage then samples this frame's half-size image, moved
by its texture's turn: a turn of `a` moves the picture `tan(a) / tan(fov /
2)` half-screens, which for a few degrees is the rotated view less a
perspective term too small to see. That is `blur.vso`'s per-stage uv offset,
which the original sets to 0. The weights, the phases and the fade are the
script's. The view model is in the frame, so it echoes with the world.

Measured on Cathedral, the view turned steadily through a hit, consecutive
frames line up at a steady 37 to 40 half-size pixels in this mode; with the
original's textures no shift lines them up at all, and one step in three
barely changes.

`PAINFUL_COMPOSE`: 0 off, 1 the shifted frame (the default), 2 the
original's three textures - this frame copied into the target, the stages
read from the textures, a texture not drawn since the targets were built
standing in as the current frame.
