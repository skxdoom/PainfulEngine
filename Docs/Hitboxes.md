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
