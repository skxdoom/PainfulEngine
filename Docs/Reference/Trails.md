# Trails — the ribbons behind projectiles and blades

Sources: `TrailEffect` (constructor `0x100b0180`, `Load` `0x100b0290`),
`TrailSystem::CreateTrail` (`0x100b0850`), and `Trail`: the constructor
(`0x101ea5c0`), `AttachToBones` (`0x101ea930`), `Init` (`0x101ea2f0`),
`UpdateTransform` (`0x101e99e0`), `Tick` (`0x101ea800`), `AddNode`
(`0x101e9cd0`), `CutTail` (`0x101ea080`), `Draw` (`0x101e9430`).

## The definition

`Data/Scripts/Trails/<name>.ini`, `[General]`, with the constructor's
defaults where a key is absent:

| key | field | default | |
|---|---|---|---|
| `Texture` | `+0x0c` | | loaded as `Particles/<Texture>` (`SetTexture` swaps in `Trails/<name>`) |
| `BlendMode` | `+0x18` | 5, translucent | the material system's names (`FUN_100973a0`) |
| `Width` | `+0x1c` | 0.1 | |
| `LifeTime` | `+0x20` | 1.0 | seconds, or length with `ConstLen` |
| `FPS` | `+0x24` | 0 | picks a frame of an animated texture; no shipped trail sets it |
| `ConstLen` | `+0x28` | off | a flag: `trail_ghost`'s `5` only means "on" |

18 ship, used by every dart, nail, shuriken, stake and grenade and by the
monsters' blades.

## Making one

The scripts always do the same three calls:

```lua
local e = ENTITY.Create(ETypes.Trail, "trail_kolek", "trailName")
ENTITY.SetPosition(e, 0, 0, -1.2)      -- optional: the offset on the owner
ENTITY.AttachTrailToBones(owner, e [, joint names...])
```

`AttachToBones` registers the trail as the owner's child (following, joint
-1) and sizes its ring with `Init(2000, n)`: `n` is the number of joint names
for a model owner, floored at 2, and the trail keeps the count itself (0 for
an owner that is not a model, which is every projectile).

## Sampling

`UpdateTransform` replaces the entity's own and never touches the trail's
scale, so a trail stays at the scale it was created with (1) whatever its
owner's - a stake's 0.07 does not shrink it. It samples, each time the owner
moves:

| joints | points |
|---|---|
| 0 | the owner's position plus its matrix applied to the trail's offset |
| 1 | that joint's position plus the same offset |
| 2+ | each joint's position |

## The ring

2000 nodes; each is the sampled points and the clock when they were taken.
`Tick` advances the clock by the frame delta - unless `ConstLen`, where
`AddNode` advances it by the distance moved instead - then runs `AddNode` and
`CutTail`.

**`AddNode`, two or more joints.** Once the first point has moved
`Width * scale * 0.25` (`0x102b49a4`) from the last committed position a new
node begins; until then the newest node is overwritten with the current
points, so the ribbon's head always reaches the owner.

**`AddNode`, fewer.** Nothing at all until the point has moved
`Width * scale * 0.5` (`0x102ae5b0`); then a node is committed with the
position and the normalised direction from the previous node (zero for the
first; the previous node's when the step is under `1e-7`).

**`CutTail`.** The tail advances past nodes older than `LifeTime`, but stops
on the newest expired one, so the ribbon runs to the end of its texture before
it shortens; a span over twice the `LifeTime` drops that one too. A trail with
no nodes left and no owner deletes itself - which is how a trail outlives the
projectile that made it and fades out after it hits.

## Drawing

One triangle strip per ribbon, two vertices per node, white:

- **one point a node**: the node position plus and minus
  `normalise((position - camera) x direction) * scale * Width * 0.5`, so the
  ribbon always faces the camera; v 0 and 1 across.
- **joints**: one strip per neighbouring pair, the two joints' points, v their
  index over `joints - 1`.

u is the node's age over `LifeTime`, 0 at the head. The texture carries the
fade: `trailkolek` is grey along its middle, black at the edges, with alpha
running from 169 at u 0 to 0 at u 1.

## The port

`World/Trails` holds the definitions (`TrailLibrary`) and the ring
(`TrailRibbon`, the `AddNode` / `CutTail` above), `Game/ScriptTrail` the native
and `ScriptEngine::TickTrails`, which samples after `UpdateAttached` has placed
the trail on its owner. `ParticleRenderer` draws the strips with the particle
program after the emitters.

Two things are chosen rather than read:

- **The blend.** `Draw` sets a fixed state block (`World+0x1608`) and never
  reads `+0x18`; which code applies the definition's `BlendMode` was not found.
  The port blends each trail by its `BlendMode` through `BlendModeState`, the
  table recovered for particles.
- **The sampler.** `SetState`'s stage bytes were not decoded. The port clamps
  both axes: the one expired node kept at the tail has u past 1, and wrapping
  it would draw the texture's bright end again at the tip.

The offset is applied the way `RegisterChild` places any following child,
through the owner's scale. Whether `UpdateTransform`'s owner matrix carries
the scale is unrecovered; it decides whether a stake's `-1.2` puts the trail
at its tail or at its middle.
