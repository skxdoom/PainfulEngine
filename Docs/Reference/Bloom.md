# Bloom

The original's one post-process, recovered from `View::Render` (`0x100B6720`),
the pass functions it calls and the shaders inside `Data/Shaders/effects/Bloom.fxo`.
The port's pass is `Render/Bloom.{h,cpp}` with `Shaders/vs_post.sc`,
`fs_bloom_bright.sc`, `fs_bloom_blur.sc` and `fs_bloom_composite.sc`. The
threshold, the kernel and the gains below are reproduced; the resolution
is not, see "Deviations".

## Where it sits

| what | where |
|---|---|
| the gate, the screen copy, the composite call | `View::Render` `0x100B6720` |
| the ps_2_0 chain (bright pass, two blurs) | `FUN_100a9dc0` |
| the ps_1_1 chain (nine taps, accumulated additively) | `FUN_100a94f0`, hardware class < 5 |
| Gaussian weights from Multiplier | `FUN_100a8f60` |
| tap offsets, horizontal / vertical | `FUN_100a8860` / `FUN_100a8ad0` |
| one pass: target, half-texel quad, draw | `FUN_100a9060` |
| the additive composite quad | `FUN_100b2890` |
| the targets | `World::Init` `0x1005C800` |
| `WORLD.BloomFXParams` into `World+0x6cc..0x6d8` | `Game/ScriptWorld.cpp` |
| the screen copy, `GetBackBuffer` + `StretchRect(D3DTEXF_NONE)` | `D3Dev` vtable `+0x5c` = `0x10006610` |
| the texture stage op of the composite | `D3Dev` vtable `+0xa8` = `0x10004890` |

The gate, in `View::Render`: render flag 8 (`Cfg.Bloom`, set by
`R3D.EnableBloom` / `R3D.ApplyVideoSettings`) and `BloomFX.Multiplier > 0`,
on the ordinary world path (not the render-to-texture, ghost or demon paths),
on hardware class 2 or better. Class 5 and up takes the ps_2_0 chain; that is
every card since 2003 and the one described here.

## The chain

```
copy    backbuffer -> 512x512 texture (World+0x18cc), StretchRect, POINT filter
bright  copy -> fb1 (W/2 x H/2, World+0x18d0):
            lum = dot(rgb, (0.3, 0.6, 0.11))
            rgb * 2 * sat(lum - LuminanceThreshold),  written to 8-bit (clamps at 1)
blur H  fb1 -> fb2:  sum over i = -6..6 of  w[|i|] * fb1(uv + i * 1.25 texels along x)
blur V  fb2 -> fb1:  the same along y
final   backbuffer += fb1 * OverlayColor / 255       (blend add, ONE/ONE)
```

**Bright pass.** `bright_pass` in `Bloom.fxo` is ps_1_1:

```
dp3 r0, t0, c0            ; c0.xyz = (0.3, 0.6, 0.11), c0.w = LuminanceThreshold
mad_sat r0.w, c0.w, c1.w, r0.w   ; c1.w = -1  ->  sat(lum - T)
add r0.w, r0.w, r0.w      ; x2
mul r0, t0, r0.w
```

`FUN_100a9dc0` sets c0 to `(0x3e99999a, 0x3f19999a, 0x3de147ae, World+0x6cc)`.
The doubled weight can reach 2; on the ps_1_1-only cards it would have been
clamped to 1 by the register range, on everything since it is not, and the
8-bit target clamps the product instead. The port does the latter.

**The blur** is `gauss_fb1` / `gauss_fb2`, ps_2_0, 13 taps: the centre, six
on each side. The vertex shader offsets them by `±c20..c22` (taps 1..3) and
`±c40..c42` (taps 4..6); the pixel shader weights them with `c0..c6`.
`FUN_100a8860` writes offsets of `i * 1.25 / width` (the constant at
`0x102b46d0` is 1.25, the multipliers 3, 4, 5, 6 at `0x102aeef0`, `0x102aea7c`,
`0x102b0e70`, `0x102b05e8`), `FUN_100a8ad0` the same over the height.
`FUN_100a8f60(Multiplier, 4.0)` computes, for i = 0..6:

```
w[i] = Multiplier * exp(-i^2 / (2 * 4^2)) / sqrt(4^2 * 2 * pi)
```

i.e. a Gaussian with sigma 4 in tap units (5 texels of the half-size buffer,
10 screen pixels) cut off at 1.5 sigma, where it is still 32% of the peak. The
seven weights sum to 0.897 over the 13 taps, and `FUN_100a9000` uploads the
same seven before BOTH passes, so the blur as a whole scales the bright pass by
`0.805 * Multiplier^2`. The levels set Multiplier 0.2 to 1.5; a 1.5 is 2.25 on
the result, a 0.5 is 0.25.

**Composite.** `FUN_100b2890(2, fb1, 0, 0, ...)` draws a screen quad through the
HUD's vertex path with blend mode 2 (`add`, ONE/ONE - the table in
`Particles.md`), texture stage `(5, 2)` which D3Dev's stage setter turns into
`COLOROP MODULATE, ARG1 TEXTURE, ARG2 DIFFUSE`, and the quad's diffuse set to
`World+0x6d4` = `BloomFX.OverlayColor`. So the overlay is a per-level gain on
the bloom, 0x808080 = one half by default; three levels raise it to 192, one
lowers it to 117, one sets it black (bloom off without touching Multiplier).

In `View::Render` the composite comes after the scripts' render callback, so
the original adds its bloom over the HUD too. The port composites before the
2D layer.

**The sprites are dimmed first.** With bloom on, particles and coronas are
packed at `BloomFX.DimScale` before any of this runs - `Particles.md`, "Bloom
dims the sprites". That is why a level with bloom looks no brighter overall:
the bloom gives back what the dim took, spread out.

## The parameters

`CLevel.BloomFX` reaches the engine through `WORLD.BloomFXParams(LuminanceThreshold,
Multiplier, OverlayColor, DimScale)`; the `World` constructor defaults are 0.25,
1, (128,128,128), 0.8. Across the 57 shipped levels: Multiplier 0 (7 levels),
0.2, 0.3, 0.5, 0.8, 1, 1.1, 1.2, 1.4, 1.5; LuminanceThreshold 0, 0.05, 0.15,
0.2, 0.21, 0.25, 0.35, 0.6; OverlayColor 192, 159, 117 and 0.

## Deviations

**No 512x512 point-sampled copy.** The original's bright pass reads a copy of
the screen made by `StretchRect` with `D3DTEXF_NONE`: at 1280x1024 every
third pixel, at 1920x1080 every fourth, no filtering. Small highlights flicker
as they cross the skipped columns, and the result is a 512-wide picture
stretched back over the screen, which is the blockiness around lights. Here
the scene is rendered to a full-size target and the bright pass samples it at
1/`BloomScale` (default 2, the original's fb1 size) with the bilinear fetch on
the corner of four pixels, an exact 2x2 average. Nothing is skipped and the
buffer is the screen's own aspect.

**The kernel runs out to three sigma.** The original's cut at 1.5 sigma leaves
a step of a third of the peak at the edge of every glow. `BloomKernel 0`
(default) takes the same Gaussian, the same 1.25-texel tap spacing, out to 3
sigma (25 taps), and scales it so the weights sum to what the original's 13
did - the intensity of a lit area is unchanged, only the tail is longer and
the edge gone. `BloomKernel 1` is the original's 13 taps, for an A/B. Both
carry Multiplier in both passes as the original does.

**Composite under the HUD**, see above.

Settings: `painful_config.ini` `BloomScale` (1 is full size, 2 the original),
`BloomKernel`; `PAINFUL_BLOOM=0` disables the pass for a comparison. The
on/off switch itself stays the original's `Cfg.Bloom`. The `--shot` log line
`bloom: on, WxH buffers, N taps, threshold T, multiplier M` is the numeric
probe.
