# Fog and light volumes

Local fog (Cemetery's and Catacombs' ground mist) and light shafts (Cathedral,
Prison, Pentagon) are map objects drawn as volumes: how much of the volume lies
between the eye and the scene decides how much of its colour a pixel takes.

| What | Symbol / file | Address |
|---|---|---|
| object name -> `Volume(mesh, type)` | `World::LoadMeshPakFile` | `0x1005dd40` |
| the class: colour, End, type, materials | `Volume::Volume` / `RenderInitialize` | `0x101d4060` / `0x101d3d70` |
| per-volume constants, passes, contents | `SceneRender::RenderWorld` | `0x100b3dbc..0x100b3fd0` |
| back / front pass | `Volume::DrawBackPass` / `DrawFrontPass` | `0x101d3af0` / `0x101d3ba0` |
| `FOGVOL.Setup(e, color, end)` | Lua native | `0x1013b4b0` |
| `FOGVOL.GetProperties(e) -> r, g, b, end` | Lua native | `0x1013b560` |
| the passes | `Shaders/Scripts/volumetric.shader` | — |
| the ramp lookup / the colour | `fog.vso` / `fog_color.vso` | — |

## Which objects, which settings

`LoadMeshPakFile` tests the object name for `"vollight"` first, then `"volfog"`,
and makes a `Volume` of type 0 (light) or 1 (fog) instead of a `WorldMesh`; it
is non-colliding and never drawn as a mesh. Type 0 takes `vollightfront` for its
front pass, type 1 `volfogfront`; both take `volback`.

A map's `MapEntities/<object>.EVolumetric` holds the settings, and
`EVolumetric:Apply` hands them over with `FOGVOL.Setup(self._Entity,
self.Color:Compose(), self.End)`: the composed colour into `Volume+0x864`
(`0xAARRGGBB`), End into `+0x86c`. The class defaults are white and 20, the
constructor's too. Cemetery's `volfogshape` is `Color(163,163,163,0)`, End 40.

## The passes

Per visible volume, farthest-listed first, after the opaque world and the model
shadows and before the translucent list (render flag `0x20000`):

1. `c9 = (_, _, 0, 1 / E)` with `E = min(End, World+0x6b4 - 1)` (the far clip),
   and `c10 = colour / 255`, its RGB times `World+0x6d8` (BloomFX's fourth value,
   0.8) while `World+0x6d0` (the bloom multiplier) is above 0.
2. `volback` on the back faces, depth-tested: alpha = `fogatten(clipZ / E)`.
   The front faces count stencil on depth failure (+1 back, -1 front).
3. Every mesh and model touching the volume and seen this frame, again with
   `volobj` / `volpalskin`: where stencil is non-zero (a surface inside the
   volume) alpha = `fogatten` at the surface's depth.
4. `volfogfront` / `vollightfront` on the front faces: reverse-subtract alpha,
   leaving `ramp(back or surface) - ramp(front)`. Then the colour over the back
   faces, depth test off: `desttranslucent` for fog (`c10 * a + scene * (1 - a)`),
   `destalpha` for light (`scene + c10 * a`); then the alpha is reset.

`fog.vso` is `oT0.xy = clip.z * c9.w - c9.z`, and `special/fogatten` is a linear
alpha ramp (0, 129, 255 across u) sampled clamped. So the fog a pixel takes is
the depth it spends inside the volume over End, **with both depths clamped to
End first**: fog further than End from the eye adds nothing, which is why End is
the density and the fade together. A camera inside the volume has no front face
drawn, and the ramp starts at 0.

## The port

`Render/VolumeRenderer`. The same arithmetic, read from the scene's own depth
instead of stencil, destination alpha and redrawn contents, so a frame with a
volume in the frustum keeps the scene in its target (as SSAO does). Per visible
volume (zone and frustum culled as the chunks are, farthest first, two views each
out of `Renderer::kVolumeViewCount`):

- `fs_volume_faces`: the mesh into a full-size RGBA16F target, no depth test:
  r the nearest front face and g the nearest back face as `min(depth / E, 1)`,
  b 0 wherever a back face lies (all min-blended), a 1 wherever a front face
  lies (max-blended).
- `fs_volume` / `_ms`: the back faces over the scene colour, with
  `s = min(sceneDepth / E, 1)`: `a = min(g, s) - front`, where `front` is
  `min(r, s)` under a front face, 0 when the eye is inside the volume, else `s`
  (no fog); and `a = 0` with no back face at the pixel. The scene standing in for
  a hidden back face is the `volobj` pass, and a hidden front face gives 0 as the
  stencil did. Blended alpha for fog, added for light.

"Inside" is a ray-parity test of the eye against the volume's triangles on the
CPU, once a volume a frame. Reading it per pixel from "no front face" drew the
outline of every volume from outside (the user's report, 2026-09-17): a pixel
centre on a silhouette edge takes the back triangle and not the front one (the
top-left rule gives a shared edge to one side), and a multisampled edge sample
can have no face at its pixel centre at all.

Depth is the distance along the view axis, which is what clip z is. The volumes
are not drawn into the water's reflections. `FogVolumes` (true) switches them
off for an A/B; `FOGVOL.Setup` values are cleared by `WORLD.LoadMap` and applied
after the map uploads, like the `.EMesh` overrides.
