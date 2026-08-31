# Hitboxes — what the original engine does

Asked because shots at a monster land against one small sphere near its feet.
The answer is that PainEngine keeps **two entirely separate collision
representations per entity**, and we currently implement one of them and use it
for both jobs.

## Two representations, independently switchable

Every entity carries both:

| where | what | used for |
|---|---|---|
| `Entity + 0xac` | `PhysicsObject` | moving through the world |
| `Entity + 0x7b8` | `Ragdoll` | a body per bone |

Each has its own `EnableLineTraceCollision`, and the script layer switches them
separately:

- `ENTITY.AddToIntersectionSolver` (0x101349A0) enables **both**.
- `ENTITY.AddRagdollToIntersectionSolver` (0x10134830) enables **only the
  ragdoll**.

The scripts bracket their traces with these constantly - in the first
measurement of this port `AddRagdollToIntersectionSolver` and
`RemoveRagdollFromIntersectionSolver` were the **third and fourth busiest**
unimplemented natives in the whole game, at 7600 and 8000 calls. That traffic
is the shooting system telling the engine which limbs are shootable this
instant.

## The ragdoll is animated, not just a corpse

This is the part that answers the question. `Ragdoll` is not only what a body
becomes when it dies:

```
Ragdoll::Animate(const DynamicArray<Matrix>&)      // driven BY the bone matrices
Ragdoll::Activate(const DynamicArray<Matrix>&, int)
Ragdoll::Deactivate()
```

`Animate` takes the same skinning matrices the renderer poses with, so a LIVE
monster's per-bone bodies follow its animation frame by frame. Death is
`Activate`, which hands the same bodies to the simulation. So the shootable
shape of a monster **is its skeleton**, posed, at all times - not a proxy
volume and certainly not a single sphere.

And the game knows which bone you hit:

```
Ragdoll::GetJointFromHavokBody(void*) -> joint index
```

exposed to scripts as `MDL.GetJointFromHavokBody`, with 21 call sites. That is
how a headshot is distinguished from a leg shot.

## `.rde` says which bones are limbs

220 of them ship beside the models, and they are **plain INI**:

```
[k_szyja]
Mass = -1.0
LinearDamping = 0.050000
AngularDamping = 0.050000
Friction = 2.500000
Restitution = 0.400000
```

One section per limb, keyed by bone name. `evilmonkv2` lists **17 of its 63
bones**; the average across all 220 files is 9.4. So a ragdoll is a coarse
skeleton - spine, head, upper and lower limbs - not every finger.

**There is no shape data in the file**: only mass and material. Every `.rde` in
the game uses exactly five keys and no others. The limb shapes must therefore
be derived from the model, which `Ragdoll::Init(Model*, RagdollSkeleton*,
const char*, float, int)` is handed - almost certainly from the vertices
weighted to each bone, which is data we already parse for skinning.

## The movement shape is something else again

`PhysicsWorld::CreatePhysicsObject` (0x101999F0) routes by body type:

- mesh-derived types (4, 5, 6, 7, 8, 11, 12, 13) -> `CreatePhysicsObjectFromMesh`
- type 15 (`FromRagdoll`) -> `CreatePhysicsObjectFromRagdoll`
- type 100 (`Player`) -> its own path
- everything else, **including `Fatter` (2) which is what actors use** -> a
  shape sizer, `FUN_101B3E20(out, scale, bodyType, group)`

That sizer has an explicit `bodyType != 2` branch, so **`Fatter` is a distinct
shape rather than a sphere with a different radius**. What primitive it
actually builds is inside Havok-specific code that has not been cracked: the
constants are shape-header magic rather than anything named, and the only
`CAPSULE` string in Engine.dll is `HK_DISPLAY_CAPSULE`, a debug-draw enum. So a
capsule is the natural reading and the one the movement code wants, but it is
**not established** and should not be written down as if it were.

## Where this leaves us

We build one sphere - horizontal half-extent, on the soles - and use it for
movement AND for shooting. Movement is roughly right. Shooting is not: a shot
tests against a 0.29-unit ball at the feet of a model two and a half units
tall.

The two jobs want different things, and the second one is a real system:

1. **Per-bone hitboxes.** Read the `.rde` for which bones are limbs, build a
   shape per limb from the vertices weighted to that bone, and pose them with
   the skinning matrices we already compute every frame. Trace against those.
   `MDL.GetJointFromHavokBody` then becomes answerable, and with it headshots.
2. **The intersection solver becomes per-limb**, which is what the scripts have
   been asking for all along with those 15 600 calls a run.
3. **A capsule for movement**, if the shape sizer can be read - or an argued
   approximation, flagged as one.

(1) reuses the skeleton, the `.rde` and the skinning weights that already
exist. It belongs with the ragdoll work rather than with movement.


---

# What the movement shape actually is

The section above left the sizer unread. It is readable, and the answer is not
a primitive.

## A compound, and `Fatter` is the one type that keeps it

`FUN_101B3E20` switches on the body type and builds its result from two pieces:

- `FUN_10211640(this, childArray, count)` - a refcounted CONTAINER of child
  shapes. A vtable, a growing array, a refcount bump per child.
- `FUN_10211040(this, child)` - takes that container and derives a SINGLE
  convex shape from it, querying the child's vertices through its vtable.

The discriminator is one line: `if (param_4 != 2)`. Every body type except
`Fatter` collapses the compound into one convex shape; **`Fatter` keeps its
parts**. That is what the name has been saying.

## `Fatter` is three stacked spheres

Case 2 computes twelve floats from a working scalar `k = scale * 0.2`
(`_DAT_102b3b80`) and hands them to a THREE-element constructor. In address
order they group as three `(0, y, 0, r)` records:

| sphere | centre y | radius |
|---|---|---|
| 1 | -2.2k | 2.6k |
| 2 | +1.0k | 3.0k |
| 3 | +4.0k | 1.5k |

Wide low, wider through the middle, narrow at the head. It explains why the
compound has children at all, and why walking into one feels like a capsule
without being one.

The corroboration is a fourth constant in the same branch, `-4.8`: the bottom
of sphere 1 is `-2.2k - 2.6k = -4.8k`. Two independent computations of the same
extent agreeing is what makes this a reading rather than a guess.

## Which monsters use it

82 monster templates declare a body type, in `o.s_Physics`:

| BodyType | monsters |
|---|---|
| `Fatter` (2) | **66** |
| `Simple` / `Sphere` (1) | 16 |
| `FromRagdoll` (15) | 1 |

`CActor:PO_Create` reads it from `self.s_Physics.BodyType` - not from a field on
the object - and falls back to `Fatter` when the block is absent. Nothing in the
templates carries a radius or a height: `s_Physics` holds only `BodyType`,
`Mass` and `InertiaTensorMultiplier`.

Measured at the engine boundary, `zombie`, `nun`, `banshee`, `vamp_small`,
`vamp_v2` and `DevilMonkv2` all arrive as `bodyType=2` at scales of 0.13-0.18.

## How the engine sizes it, and where our version differs

`PhysicsWorld::CreatePhysicsObject` (0x101999F0) looks up the joint named
`ROOOT` and sizes the shape from it alone:

```c
param_5 = (local_68 - entity[0x58]) * 0.909090;      // (root.y - ?) * 10/11
FUN_101b3e20(&local_78, param_5, bodyType, group);
local_78 = -local_6c;  local_74 = -local_68;  local_70 = -local_64;
```

Per-joint records live at `model + 0x684`, stride `0x5c`, XYZ at
`0x30/0x34/0x38`. No mesh extents are consulted anywhere in that path.

**`Entity+0x58` is not identified**, and it matters. Scaling from the root's
height above the soles is right for a rig whose root sits at a humanoid hip -
around 0.53 of total height - and wrong wherever a rig disagrees. They do:
banshee's root is at 0.70 of its height, vamp_v2's at 0.245, and the resulting
bodies came out 1.30x and 0.46x of their models, tracking that ratio exactly.

So we anchor to the shape's own span instead. The three spheres run from `-4.8k`
to `+5.5k`, so `k = height / 10.3` makes the body match the model on every rig
by construction, and the offset puts the lowest sphere's bottom on the soles.
**The layout is the engine's; what sets its size is ours**, pending
`Entity+0x58`.

Measured across the bench - eleven rigs, every one:

    modelH == bodyH   (ratio 1.00)     footGap 0.00

## The rigs name the same joint two ways

Six of ten shipped rigs call it `ROOOT`. The rest call it `root`, at the same
kind of height - zombie 8.59, vamp_small 6.43, raven 2.37 beneath a `big_root`
at the origin. One joint, two spellings; matching only the first leaves those
rigs with no measure. `ROOOT` wins where both exist, then `root`.

Rigs also disagree about where the model ORIGIN sits - at the feet for banshee
and nun (`lo[1]` about -3), at mid-body for the evilmonks (about -13) - so
anything derived from the origin has to be measured relative to `lo[1]`, never
assumed.

## Two shapes, where the engine has one

The engine gives a monster ONE PhysicsObject: `PO_SetMonsterType` sets a flag
(bit 2 at `PhysicsObject+0x74`) and the engine then moves that same object from
the vector `PO_Move` stores. It is both what carries the monster through the
world and what everything else collides with.

Ours are separate. `TickMonsters` sweeps its own sphere to move a monster, and
the three-sphere body is only what others hit - so the body work does not affect
pathing, and the mover is still sized from mesh bounds. Unifying them on the
recovered shape is the remaining piece.

Being a monster is not contingent on the shape: the kinematic conversion has to
happen whatever the rig looks like, and the body pose has to be synced every
frame, including for a monster that is standing still.

## Per-limb hitboxes: derived, posed, drawn - not yet traced against

`.rde` parsing and `BuildLimbBounds` live in `Source/Assets/Rde.h/.cpp`. A
vertex counts towards the bone that influences it MOST; splitting it across
every influence would smear each box over its neighbours. Boxes are held in BONE
space, so the skinning matrices already computed for the draw pose them for
nothing.

Across all 220 shipped ragdolls: **220 parsed, 0 named bones absent from their
model, 0 limbs with no vertices weighted.** `painful hitboxes <model>` dumps any
of them, and **F2** draws them in orange over the collision they are meant to
replace.

Shooting still tests the movement shape. That trace is what turns this from a
picture into gameplay.
