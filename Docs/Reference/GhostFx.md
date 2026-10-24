# Ghosts - `WORLD.EnableGhostsFX`

C5L4's render path for its demonic models: they are not drawn into the world
at all, but in grey into a buffer of their own, trailed, and added over the
frame. Recovered from the native (`0x101206A0`), `View::Render`
(`0x100B6720`), `View::RenderGhostFXWorld` (`0x100B5960`, read from the
disassembly: its 150 KB stack frame defeats the decompiler),
`AnimatedMesh::Render` (`0x10002BB0`), `AnimatedMeshMatPal::RenderGhostFX`
(`0x10005620`) and `bw.pso`. The port's pass is `Render/GhostFx.{h,cpp}`.

## The demonic flag

`ENTITY.EnableDemonic(e, on, alsoChildren)` (`0x10136480`) writes byte
`Entity+0x28`, and on every child too when asked. `AnimatedMesh::Render` reads
it before anything else but the shadow flags:

| `+0x28` | render flags | draws |
|---|---|---|
| set | `0x4000000` (Demon Morph) | `RenderDemonFX` ([`DemonFx.md`](DemonFx.md)) |
| set | `0x8000000` (ghosts) | `RenderGhostFX` |
| set | neither | **nothing** |
| clear | `0x40000` | its own path |
| clear | anything else | `RenderDefault` |

So a demonic model is invisible on the ordinary path. Demon Morph sets the
flag on every actor for its duration and clears it after; C5L4's spawn points
set it on every monster they make (`CSpawnPoint._EnableDemonic`), and
`LuciferSword` on itself.

## The switch

`WORLD.EnableGhostsFX(on)` is `World+0x6bb`. `View::Render` takes, in order:
Demon Morph (`+0x6dc`), the ghosts (`+0x6bb`), the render target
([`FrameCompose.md`](FrameCompose.md)), the ordinary world. Bloom is gated on
the last one only, so a ghost frame has none. C5L4 turns it on in `OnPlay`
and on the way back from Lucifer's realm, off on the way in.

## The frame

```
bucket     every render object by its layer (Entity+0x24, 1 from the
           constructor), skipping those with flag 0x40 at +0x18
ghosts     target World+0x18b4, its own depth (+0x18c0), cleared to black
           and depth 1; layer 1's demonic objects, last first, with 0x8000000
trail      ComposeTextures(this frame's, [ghosts, last frame's], [0.2, 0.8])
           into the pair Demon Morph uses, World[(frame & 1) + 0x62b] and the other
copy       ComposeTextures(+0x18b8, [trail, ghosts], [1, 0])
world      RenderWorld, the ordinary pass, which draws no demonic model
out        +0x18b8 over the screen through hud_add
```

The ghosts' depth is their own, so walls do not hide them: the trail is added
over everything but the HUD. On hardware class 1 the function draws the world
and then layers 1 and 4 straight over it instead.

**The grey.** `RenderGhostFX` sets pixel constant `c0 = (0.3, 0.59, 0.11, 0)`
and draws every visible part with the mesh's own texture through
`ghostfx` (`skin.shader`: `palskinned`, alpha test off, `fshader bw`).
`bw.pso`:

```
tex t0
mul r0.rgb, t0, v0      ; the lit texture
+mov r0.a, t0.a
add r0.rgb, r0, v1      ; plus the specular
dp3 r0.rgb, r0, c0      ; its luminance
```

So a ghost is as bright as the monster would have been lit, in grey: C5L4's
dark-clad monsters are faint, as the original's are.

## The port

`EntityRenderer::SetGhostPass` routes demonic instances to their own view and
skips them on the ordinary one; with neither pass on they do not draw, as
`AnimatedMesh::Render` has it, though they are still posed for their
shadows. The ghost draw is the entity program with `u_stage1.w` set: the
luminance before the second stage and the fog, no alpha test, depth written.
`GhostFx` keeps the target and the trail pair at half the window, like Demon
Morph's trail, and weights the trail as `0.8^(dt * 60)` for last frame's and the
rest for this one: the original's 0.2 / 0.8 at 60 frames a second, the same
length on the clock at any other rate. The add goes on the backbuffer after
the scene is there, under the HUD. The frame report counts the ghost draws;
`PAINFUL_GHOSTFX=0` turns the pass off (and the demonic models with it).
