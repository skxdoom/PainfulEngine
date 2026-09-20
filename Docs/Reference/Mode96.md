# Mode 96 — `Pf.Mode96`

A port-only option: a 1996 look, deliberately unlike the original. Off by
default.

```
pfmode96 1          # the console
Pf.Mode96 = true    # painful_config.ini
PAINFUL_MODE96=1    # overrides the setting, for a --shot
```

| key | default | |
|---|---|---|
| `Pf.Mode96MipLevel` | 2 | the mip level every surface samples |
| `Pf.Mode96Colors` | 16 | levels per channel for the albedo, blue at half |

## What it forces

Each one is an override at the point of use, not a rewrite of the setting, so
turning the switch off restores what the player had with nothing reloaded.

| | |
|---|---|
| Textures | point on min, mag and mip for every scene bind (`SetForcePoint`); particles, sprites and beams through `Mode96Sampler`, which leaves their binds alone while the switch is off |
| Resolution | one mip level for every surface at every distance (`texture2DLod`), so nothing is chosen per pixel from the derivatives. Distant surfaces alias instead of blurring |
| Detail layer | off - `u_detail.z` cleared and the detail texture unbound |
| Sky | the two layer textures take the same mip level; the mask and lightmap keep hardware selection |
| Colour | the albedo quantised per material (`Mode96Quantize`), blue at half the levels |
| Shadows | the flashlight, character, placed-light and view-model maps init at size 0 |
| Post | bloom, SSAO and the heat-haze warp pass off |
| Model specular | `u_specColor` zeroed, which covers the directional highlight and the dynamic lights (`shared_entity.sh`: `specular *= specMask`) |
| Normal maps | the bump program is skipped; that path masks specular with the map's alpha, not a uniform |
| World specular | the gloss light count forced to 0 |

Untouched: the lightmap, ambient, the environment directional, the dynamic
lights' diffuse, fog, the fog and light volumes, and Demon Morph. Water has its
own shader and is not quantised.

## Coronas are exempt

A corona is one soft radial gradient; quantising it draws concentric rings.
`BillboardRenderer` passes zeros for a sprite with `corona` set, and the
texture's own sampler flags. Plain sprites, the `Spr_` immediates and the beams
take the mode.

## Colour is quantised per material

`fs_world`, `shared_entity` and `fs_particle` each quantise their own albedo.
A full-screen quantiser was tried and removed: rounding the finished frame to
8 levels puts the first step at 1/7, and most of Painkiller sits between 0.05
and 0.2, so rooms flattened to one grey. Per material there is also no scene
target and no extra pass, and the HUD is left alone.

The light term is not quantised. Banding the lightmap shifts a level's colours
too far.

## Force-point is not a fourth `TextureFilter` value

`R3D.SetTexFiltering` re-reads `Cfg.TextureFiltering` whenever the menu applies
video settings, so a value stored there is overwritten on the next apply. The
override is a separate flag `FilteredSampler` checks first; it takes a
`lightmap` argument for the two binds that pass one, which keeps their
filtering.

## Check

`PAINFUL_MODE96=1 ... --shot` on Cathedral: the bloom line reads `off` with
`0x0 buffers` and the shadow line reads `flashlight OFF, characters OFF (0 of N
cast), 0 lights ... view model off`. The wall ornament is blocky where a normal
capture has filtered detail.

With the switch off the frame matches the commit before this work within
run-to-run noise: the cross-build difference came out below the same binary's
own variation between two runs. Compare at one resolution - a capture takes the
configured window size, and a frame from another size is not comparable. The
`lua` report is unchanged either way.

The uniform carries 0 for the albedo levels while the switch is off. Leaving
the configured value there quantised the albedo with the mode disabled, which
reads as a small everywhere difference rather than an obvious fault.
