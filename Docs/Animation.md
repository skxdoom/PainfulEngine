# Animation — scope

What "animation" turns out to be here, what already exists, and the order to
build it. Written before starting, because the `MDL` surface is four separate
systems wearing one name and they are worth very different amounts.

## It is four systems, not one

Counted over the shipped scripts:

| system | calls | what it is |
|---|---:|---|
| **Joints** | ~430 | `GetJointIndex` 217, `TransformPointByJoint` 126, `GetJointPos` 41, `GetJointName` 22 — where a weapon, effect or gib attaches to a bone |
| **Ragdoll** | ~300 | `EnableRagdoll` 101, `SetRagdollLinearDamping` 104, `SetRagdollAngularDamping` 100, and a dozen more — a separate physics system |
| **Mesh/material** | ~150 | `SetMeshVisibility` 98, `SetTexture` 33, `SetMaterial`, `SetBlendAlpha` — not animation at all |
| **The clock** | ~80 | `SetAnimTimeScale` 41, `SetAnim` 17, `GetAnimTimeScale` 11, `GetAnimTime` 6, `GetAnimLength` 4, `SetAnimTime`, `ResetFrame`, `GetAnimMovement` |

The clock is the smallest of the four by call count and by far the most
valuable, because it is the one the rest of the game is waiting on.

## Why the clock comes first, alone

`CActor:Tick` gates its entire animation-event loop on it:

```lua
local animSpeed = MDL.GetAnimTimeScale(self._Entity, self._CurAnimIndex)
if animSpeed > 0 then
    while self._AnimationEvents[i] do
        local ev = self._AnimationEvents[i]
        if ev[1] > curAnimTime then break end
        ...                                    -- ev[2] is a method name
```

and the events come straight out of the actor's own template:

```lua
Animations = {
    charge = { 0.8, false, {
        {0.0, 'PlayRandomSound2D', "charge"},
        {0.4, 'Charge'}, {0.8, 'Charge'}, ... }, 0.5 },
}
```

`{speed, loop, events, blendTime}`, with each event `{timeInSeconds, method,
arg}`. That loop is how melee damage lands, how footsteps and attack sounds
fire, and how an actor sequences its state against `_CurAnimTime` and
`_CurAnimLength`. We return 0 from `GetAnimTimeScale`, so **none of it has
ever run**.

The clock needs **no rendering whatsoever**. It is a per-entity timer, a
duration read from the `.ani`, and an index. That makes it independently
landable, independently testable headlessly, and the single biggest
behavioural change still available. Skinning makes it visible; it does not
make it work.

## What already exists

More than expected. `Assets/Ani` parses `.ani` in full — `frameTime`, per-bone
variable-length tracks of parent-relative matrices, `duration()`, with an
exactness check. `Assets/Skeleton` has `BuildHierarchy`, `ComputeBindWorld`,
`ComputeSkinningMatrices` and `SkinMesh`, and the glTF export cross-checked
the maths against the native path years of work ago.

Animations resolve by filename: **`<Model>.<anim>.ani`** — `evilmonk.idle.ani`
for `evilmonk.pkmdl` with anim `"idle"`. 1228 animation files across 284
models.

## What is missing

**In the asset layer**, two gaps:

- `ComputeSkinningMatrices` samples by key **index**, not by time. Playback
  needs time-based sampling. Take the nearest key first — `frameTime` says the
  keys are fixed-rate, so that is faithful and simple; interpolate only if it
  visibly steps.
- No blending. `SetAnim`'s `blend` argument and the template's `blendTime`
  cross-fade between animations. Defer.

**In the renderer**, everything: there is no skinned path and no `vs_model`
shader. `GpuModel` keeps only GPU handles and drops the CPU mesh after upload,
and instances *share* a `GpuModel` — so a posed instance needs both the source
mesh retained and a buffer of its own.

## The rendering decision: CPU skinning first

Two options, and the reason to pick the slower one first is a correctness
argument rather than an effort one.

**CPU skinning** reuses `SkinMesh`, which is already written and already
checked against a known-good reference. It costs a retained CPU mesh per model
and a dynamic vertex buffer per animated instance, re-uploaded each frame.
With Cathedral's ~40 actors that is nothing; on a busy level it is the thing
to watch.

**GPU skinning** is the end state — one shared buffer, bone matrices as
uniforms, no per-instance upload. But it needs a new vertex layout carrying
bone indices and weights, a new shader, and a skinned variant of the material
path. Every one of those is a fresh convention: bone index order, weight
normalisation, matrix layout and row-versus-column in the uniform array.

Conventions are where this port has repeatedly bled — the rotation work took
four passes and two regressions. Doing CPU first buys a **correctness oracle**:
a working, visually verifiable reference to diff the GPU version against, one
bone at a time. Going straight to GPU means debugging a new shader and a new
convention simultaneously, with nothing to compare against.

So: CPU, then GPU as an optimisation once there is something to check it with.

## Stage 1 landed: the clock runs

`SetAnim` hands out real per-entity indices, `GetAnimLength` reads the `.ani`,
and the time advances, loops and holds. Measured on Cathedral: `SetAnim(idle)`
returns index 0 with a length of 1.250 s (the file's own duration), an unknown
track still returns -1, the time advances at exactly the declared speed and
wraps precisely at the length. **All 40 actors in the level are animating.**

The decisive test passes: an `EvilMonkV2` set to `atak1` fires its declared
events — `damage` at 0.75 s and the sound events around it. That loop had
never executed once before.

### Three things the clock taught

**The event loop is in `CActor:Update`, not `CActor:Tick`.** `Update` is
driven by `GObjects:Update()`, which `Game:Tick` calls `Game.Loops` times a
frame - and `Loops` comes from `delta * 30`, so the actor logic runs at a
fixed **30 Hz** while rendering runs free. Looking in `Tick` for it wastes an
hour.

**Turning the clock on aborted the entire tick.** With animations finally
playing, `CActor` reached `MDL.GetAnimMovement` for the first time; the stub
returned nothing, the script multiplied a nil, and the error unwound
`Game_Tick` **every frame** - taking the whole actor update with it. The same
thing happened again a step later, when weapons started animating and reached
`MDL.TransformPointByJoint`.

This is the "a stub that returns nothing errors loudly, which is the signal to
implement it" design working - but the blast radius is worth knowing: one
missing return value in one actor kills the update for every object in the
level. Both now return real value counts with honest placeholder contents,
documented at the call.

**The error handler was blind, and had been all along.** It looked up
`debug.traceback` as a global when an error happened - but the shipped scripts
alias the library to `debugl` and then use `debug`-prefixed names as their own
flags, so by the time anything failed the global was no longer the library and
every error arrived as a bare one-line message. It is now captured into the
registry at startup, before any script runs. (Lua 5.0's `debug.traceback` also
takes the message ALONE; the level argument only arrived in 5.1, and passing
one appends it to the text.)

## Order

1. ~~**The clock.**~~ **Done.** `SetAnim` returning a real per-entity index, `GetAnimTime`,
   `GetAnimLength`, `Get/SetAnimTimeScale`, `SetAnimTime`, `ResetFrame`,
   `LoadAnim`. An animation cache keyed by (model, anim). No rendering.
2. **Skinned rendering.** Time-based sampling, retained CPU mesh, per-instance
   dynamic buffer, `SkinMesh` each frame for visible animated instances.
3. **Joints.** `GetJointIndex`, `GetJointPos`, `TransformPointByJoint` off the
   posed skeleton from (2) — this is what puts a weapon in a hand.
4. **Ragdoll.** Its own system, on Jolt, and its own scope document.

## Unknowns to settle rather than guess

- **`SetAnim` returns an index the scripts keep** (`_CurAnimIndex`) and hand
  back to every other call. Per-entity, stable, and `-1` must stay the
  "no such track" answer the scripts already handle.
- **Root motion.** `SetAnim`'s `mcurve` / `hasMovingCurveRot` arguments and
  `GetAnimMovement` are how an actor translates during an attack. This likely
  interacts with `ENTITY.PO_Move`, the single busiest unimplemented native.
  Do not guess the relationship; measure it once the clock runs.
- **Whether `GetAnimTimeScale` is the speed or a play/pause flag.** The guard
  is `> 0`, and `CActor` pauses an animation by setting it to 0 and restoring
  it later, which reads as speed. Confirm against a template's declared speed.

## How to verify, headlessly

The clock is fully testable without a window, which is the point of doing it
first:

- `GetAnimLength` matches the `.ani`'s own `duration()`.
- `GetAnimTime` advances at `speed x dt`, wraps for a looping animation and
  stops at the end for a one-shot.
- **The decisive one:** an actor's animation events fire. Hook a method named
  in a template's event list and assert it is called at the declared time —
  then a monk's attack event should land damage on the player, which is the
  whole reason this stage matters.
