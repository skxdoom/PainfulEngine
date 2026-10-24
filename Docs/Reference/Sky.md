# Sky - the dome's lightning flash

Sources: `Sky::FlashTexture` (`0x100AF950`), `Sky::Tick` (`0x100AF6E0`),
`Sky::Load` (`0x100AFCC0`), the layer setup `FUN_100AFA90`, and the native
`WORLD.FlashSkyTexture` (`0x101202F0`). How the layers themselves composite is
in [`Status.md`](../Status.md), "Sky".

## The dome's layers

`Sky::Load` collects the dome's objects named `layer00` to `layer99`, in that
order, as layer entries of `0x60` bytes (`Sky+0x14`, count `+0x18`); entry 0 is
the lowest-numbered layer. `WORLD.SetupSkyLayer(i, ...)` fills entry `i` and
`FUN_100AFA90` writes the layer's `Tex1` into every texture slot of its mesh
(`WorldMesh+0x764`, `0x70` bytes a slot, the slot's texture at `+0xc`) and
`Tex2` beside it (`+0x4c`), the mask and the lightmap into the mesh's own
fields, then reloads from `Skies/`.

A `Tex1` named `..._00` is therefore a frame sequence
([`TextureTransforms.md`](TextureTransforms.md), "Bonus: texture animation"):
`ruinzsky_00`..`_07` on C3L5, `c6l4_citysky_00`..`_07` on C6L4. Nothing
advances it on its own, so the sky shows frame 0.

## The flash

`WORLD.FlashSkyTexture(seconds, frame)` reaches `Sky::FlashTexture` on the
first layer's slot-0 texture:

- `frame` below the sequence's frame count: that frame becomes the texture,
  and the flash ends at the sky's clock (`+0x10`) plus `seconds` (`+0xc`).
- otherwise only the end is cleared (-1); the texture stays as it is.
- a sky with no layers, or a slot with no texture, does nothing.

`Sky::Tick(dt)` runs from `World::Tick` with the world's delta: it advances
the clock, and once the end has passed puts frame 0 back and clears it.
`Sky::Load` and `Unload` clear the end as well.

C3L5's and C6L4's level scripts drive it from their `Flash` timer: while a
flash is on they step through frames 0-3 or 4-7 at 24 a second, each call
asking for 10 seconds, then end it with `FlashSkyTexture(0, 0)`, which lapses
on the next tick.

## The port

`SkyRenderer` loads the first layer's sequence (`LoadFrames`), keeps the
frame, the clock and the end, and binds the current frame as that layer's
`Tex1`. `GameApp` ticks it with the sim delta beside the trails. Headless
reports have no sky, so there the native is a no-op.
