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

## Stage 2 landed: the pose reaches the screen

`ScriptEngine::TickAnimations` pushes what the clock is playing at the
renderer, which resolves the animation's tracks against the skeleton once,
poses every **visible** animated instance each frame, and draws it from a
per-instance dynamic vertex buffer. Measured on Cathedral: 4 instances CPU
skinned per frame (`EvilMonkV2` and the player's `KK2`) out of 81 entity
draws, at 120 fps — unchanged from before the work, because culling runs
first and an actor across the level costs nothing.

### The keys are matrices, so the pose had to be interpolated

`.ani` stores a whole parent-relative matrix per key, not a
translation/rotation/scale triple, at **25–30 keys a second**. Holding the
floor key visibly steps at any real frame rate, so `BlendPose` recovers the
rotation before blending: the rows of the 3x3 are basis vectors, their lengths
carry per-axis scale and lerp on their own, and what is left is a pure
rotation that goes through the quaternion. Blending the matrices entry by
entry instead would shrink a bone as it turns and shear it through a large
rotation.

### Verifying it without a window

`painful pose <model> <anim> [time]` reports bind-pose bounds against posed
bounds, and this is the check that the maths is right rather than merely
plausible:

- **The unanimated identity.** With no track bound to any bone, every skinning
  matrix is `inverseBind * bindWorld`, which must come out exactly identity.
  It does, to 1e-6 across every model tried. A reversed multiply order shows
  up **only** here — the animated numbers would still look reasonable.
- **Continuity across a key.** Sampling either side of a key boundary must
  agree (`evilmonkv2 walk` at 0.49999 and 0.5 give identical bounds). This is
  what catches a transposed quaternion round-trip.
- **The shape changes the right way.** `evilmonkv2` goes from a bind T-pose of
  28.72 x 22.91 x 5.87 - arms out along X, 22.91 tall, and thin front-to-back -
  to 11.81 x 19.52 x 23.23 walking: arms come down, the height survives, and
  the depth grows as the legs stride and the axe swings. A weapon keeps its
  size and moves by a fraction, which is what a recoil should do.

This command is also why CPU skinning came first: it is the reference a GPU
skinning shader gets diffed against, one bone at a time.

## Stage 3 landed: the skeleton answers questions

Four natives were returning the entity's own position for every joint, and one
was returning -1 for every lookup. Those are the dangerous kind of stub: they
return a plausible value, so nothing errors and a muzzle flash simply appears
at a monster's feet.

`SkeletonCache` now loads bones, bind-pose world matrices and their inverses
per model, and the pose moved **out of the renderer onto the entity**. That
was the one structural decision this stage forced, and it went the way it did
for two reasons: the joint natives have to answer with no window open, which
is how everything here gets verified; and a pose computed in two places is a
pose that can disagree with itself, which shows up as a flash drawn at one
pose and spawned at another. The renderer is now handed `SetScriptSkinning`
and does no posing of its own.

The cost of posing every animated entity rather than only the visible ones is
one pass over a skeleton - about forty actors at sixty bones in a level. The
expensive half, deforming vertices, stays behind the frustum test, so an actor
across the map still costs nothing. Measured: unchanged at 120 fps.

### What the bone names settled

The rig is Polish, and reading it answers questions that would otherwise be
guesses. `evilmonkv2`'s spine runs `root -> k_ogo -> k_zebra -> k_ramiona ->
k_szyja -> k_glowa` (tail, ribs, shoulders, neck, head), and the templates'
`weaponBindPos = "k_ogo"` carries the comment *skad wylatuja pociski* - "where
the projectiles fly out from". So a weapon bind joint is the muzzle, and
`ApplyJointRotation` arriving with joint 5 is the scripts turning a **neck**,
which is what a head-look should do.

Bones extend along their own local **X**, so X is the twist axis. That matters
for testing: rotating a joint about X barely moves its children, and a first
test that used X looked like a failure when it was measuring the wrong thing.

Model space itself is **Y up, Z forward, X lateral**: an idle puts the head at
Y 8.88 over a root at Y -0.14, and the walk cycle slides the root along +Z.
The bind pose is a standing T-pose, not a figure lying down.

### Verifying it without a window

`painful bones <model> [anim] [time] [joint:ax,ay,az]` reports every bone's
bind and posed model-space origin, and with the fourth argument, which bones a
joint rotation moves:

- **The chain must be monotonic.** Read it on an animation that does NOT move
  the root, or the root's own travel is mistaken for the shape of the spine -
  this was got wrong once, against `walk`, whose root slides 21.6 units a
  second and carries every bone with it. Playing `idle`, `evilmonkv2`'s chain
  climbs -0.14 -> 0.24 -> 2.63 -> 3.79 -> 6.30 -> 8.88 in **Y**: neck above
  shoulders above ribs above root, which is true of a spine in any pose. A
  wrong parent multiply order scatters it.
- **A rotation moves a bone's descendants and nothing else.** Bending joint 5
  moves 6 and 7 and leaves 5 itself where it was. That is the check that the
  turn is applied in the bone's OWN frame: post-multiplying instead would
  apply it in the parent's frame and move bone 5 too, swinging the head off
  the neck.

### Left open, deliberately

`ApplyJointRotation` SETS a bone's rotation rather than accumulating. Every
shipped caller recomputes an absolute angle each tick and passes it again - a
turret's `_barrelPitch`, an actor's head angle - so set-and-hold is
behaviourally identical to the original for all shipped content, and made
additive a turret would wind up and spin. What is **not** established is
whether the engine clears these overrides itself on some event; if a bone is
ever seen holding a rotation it should have dropped, that is the reason.

## Root motion: what Engine.dll actually does

`MDL.GetAnimMovement` was the last placeholder from these stages. Rather than
infer it, it was read out of the binary — 0x1012C210 into
`Model::GetAnimationMovement` (0x101DE890) into FUN_1001BB60, which samples one
curve twice and subtracts:

```
movement = curve(t + delta * speed) - curve(t)
```

`SetAnim` (0x1013BFC0) fills in the rest of the picture, and its argument
defaults are the engine's own:

| arg | meaning | engine default |
|---:|---|---|
| 3 | loop | **`true`** |
| 4 | speed | `1.0` |
| 5 | blend seconds | `0.2` |
| 6 | movement-curve mask | `0` (no curve) |
| 7 | movement-curve **bone** | **`"ROOOT"`** |
| 8 | moving-curve rotation | `false` |

Two things fell out of that. The curve is a **named bone**, defaulting to
`ROOOT`, which is the name of bone 0 in the shipped rigs — no guessing about
"which bone is the root" was needed. And **looping defaults to true**, which
this port had backwards: a bare `MDL.SetAnim(e, "idle")` is a looping idle, and
several shipped call sites omit the argument and rely on it.

The mask is `Definitions.lua`'s `MovingCurve`: `ETransX 1, ETransY 2,
ETransZ 4, ERot 8`. A turn animation asks for `ETransX + ETransZ + ERot` —
deliberately without the vertical, so an animation's bob cannot lift an actor
off the floor. `CActor` turns a template's `mcurve = true` into `ETransZ`.

Verified against the data: `evilmonkv2`'s `ROOOT` sits at exactly (0,0,0) for
every sample of `idle`, slides linearly to +Z 25.88 over the 1.25 s of `walk`
(21.6 units a second), and lunges non-monotonically 0 -> -2.14 -> +12.16 during
`atak`. Idle does not move, walking does, and an attack lurches - which is what
root motion is for. At runtime the Swamp's actors resolve `ROOOT` to bone 0
with mask 4 and sample it every tick.

A looping animation crossing its own end holds at the last key rather than
wrapping, so the step across the seam contributes nothing instead of reporting
the whole loop's travel as one backwards lurch.

## Order

1. ~~**The clock.**~~ **Done.** `SetAnim` returning a real per-entity index, `GetAnimTime`,
   `GetAnimLength`, `Get/SetAnimTimeScale`, `SetAnimTime`, `ResetFrame`,
   `LoadAnim`. An animation cache keyed by (model, anim). No rendering.
2. ~~**Skinned rendering.**~~ **Done.** Time-based sampling with interpolation,
   retained CPU mesh for skinned parts only, per-instance dynamic buffer,
   posing each frame for visible animated instances.
3. ~~**Joints.**~~ **Done.** `GetJointIndex`, `GetJointName`, `GetJointPos`,
   `GetJointRotation`, `TransformPointByJoint` and `ApplyJointRotation`, off a
   pose that now lives on the script side.
4. ~~**Ragdoll.**~~ **Done.** On Jolt, with the boxes handed to the solver on death — see [`Hitboxes.md`](Hitboxes.md).

## Unknowns to settle rather than guess

- **`SetAnim` returns an index the scripts keep** (`_CurAnimIndex`) and hand
  back to every other call. Per-entity, stable, and `-1` must stay the
  "no such track" answer the scripts already handle.
- ~~**Root motion.**~~ **Settled.** `GetAnimMovement` is real, read out of
  Engine.dll rather than inferred, and it is what carries an actor forward
  during an attack — independently of any AI. How it comes out of the pose is
  its own section below; `ENTITY.PO_Move` turned out to be a separate,
  pure setter.
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

## Root motion has to come OUT of the pose

Reported from play: monsters walked forward, snapped back to where they
started, and walked the same line again; and while attacking, their facing
looked erratic.

Both are one mistake. An animation with a movement curve carries its own
travel - `evilmonkv2`'s walk slides `ROOOT` 25.9 model units down +Z, and every
bone hangs off it. `GetAnimMovement` extracts that and the scripts spend it on
the ENTITY, so leaving it in the pose as well moves the actor **twice**: the
mesh strides ahead of where the monster actually is, and snaps back to it every
time the loop wraps.

The attack animations do it too - `EvilMonk`'s `atak1` and `atak2` both declare
`movingcurve = true`, and that curve swings -0.26 to +1.45 world units mid
swing. A monk lurching a metre and a half along its own facing, out of step
with where it really is, is what "the direction looks random" was.

So the pose subtracts the curve bone's travel, on the axes the curve declares
and no others - `ETransZ` takes the forward slide out and deliberately leaves
the vertical, so the actor still bobs as it walks. Measured with the curve
forced on: at t=0.07 the walk curve has already travelled +1.47 in Z and the
posed `ROOOT` comes out at 0.00.

Worth knowing for testing: **a headless run never exercises this.** Monsters
idle when they cannot see the player, `idle` declares no movement curve, and
`PosedBones` is only reached when a renderer is attached - so the correction
has to be forced to be seen outside a real game.

## Blending: the fifth argument, finally used

Reported from play: transitions between a monster's animations were abrupt.
`MDL.SetAnim`'s fifth argument is a blend time - the templates carry one per
animation in `Animations[anim][4]` and `CActor` falls back to **0.201 s** - and
it was being recorded and ignored, so an actor snapped from walking to
attacking inside a single frame.

`SetAnim` now freezes the outgoing animation and its time, and the pose
cross-fades from it. Two things worth keeping:

**The blend is on each bone's LOCAL transform, before the hierarchy composes.**
Blending world matrices instead lets a child drift off its parent - two
independently blended world transforms need not agree about where the joint
between them is, so the model comes apart at the seams exactly while it is most
visible.

**The fade only restarts when the animation actually changes.** `CActor` re-sets
the same animation constantly; restarting on every call would leave an actor
permanently half-way between a pose and itself.

Checked with `painful blend <model> <animA> <animB> [time]`, which reports a
bone at five weights. Fading `evilmonkv2` from `idle` into `atak`, the head
travels smoothly from (0.644, 8.116, 2.645) to (1.498, 7.664, 1.813) - and its
distance from its parent reads **2.5932 at every weight**. That constant is the
real test: a blend that lerped the matrices entry by entry would shorten the
bone through the middle of the fade, and going through the quaternion does not.

## A `.ani` is a slice of a longer take, and keeps that take's clock

Reported as "the Painkiller's blades spin up and then freeze on the last
frame". The script side was blameless: driving the real input path with fire
held, `CWeapon:InterpretAction` fires, `StartFireSFX` sets the one-shot
`rozkrecenie`, `OnFinishAnim` catches its end, and `PainKiller:OnFinishAnim`
hands off to the looping `obrot`, whose `GetAnimTime` then advances every
frame. Every observable the scripts have said the blades were turning.

The pose said otherwise. Sampling all 21 joints of the weapon at two instants
two seconds apart returned **bit-identical** positions. The animation clock ran;
the skeleton did not move.

The keys explain it. `PKW.obrot` has nine keys and they run from **2.84 to
3.16** — the animation does not start at zero. Played from `t=0` it spends
2.84 seconds clamped to its first key, reaches the real motion for the final
0.32, loops, and freezes again. That is the freeze, exactly.

This is not one odd file. Of the **1228** shipped animations, **103** begin at a
nonzero time, among them `PKW.idle`, `PainKiller.idle`, `PLcam.shake2`/`shake3`,
thirteen `skull` animations, ten `RTF` ones, and most of the doors, lifts, fans,
chains, catapults and cars. Every one was frozen or partly frozen.

Each `.ani` is a **slice cut from a longer authored take**, exported with the
take's own timestamps rather than rebased to zero.

Engine.dll's loader (the `Animation` vtable's `Load`, `0x10049310`) rebases it:

```c
fVar3 = **(float **)(iVar2 + 0x10);          // first key of the FIRST track
...  for every track, for every key (stride 0xa0):
       *pfVar5 = *pfVar1 - fVar3;
...
*puStack_d8 = *(float*)(keyCount*0xa0 - 0xa0 + track0keys);  // last rebased key
```

Two things follow, and we had both wrong:

- **Key times are origin-relative and must be rebased at load**, by the first
  key of track 0 — not per track, so tracks stay in sync with each other.
- **The length is the last rebased key** (`last - first`), which is what
  `Model::GetAnimationTotalTime` (`0x101de730`) returns from `anim + 0x10`. We
  had been returning the largest raw key time: 3.16 for `obrot` instead of 0.32.

The header float is **not** the length. It is the authored total including one
trailing frame step — 0.36 against `obrot`'s 0.32, 0.52 against
`rozkrecenie`'s 0.48. Reporting it would break every script that ends a
one-shot with `MDL.GetAnimTime(...) == self._CurAnimLength`, because the clock
clamps at the last key and would never reach it. Across a 40-animation sample
the relation `duration == header * (keys-1) / keys` holds exactly, which is
also the check that track 0 is representative of the file.

Verify with `painful pose <model> <anim> [time]`: `PKW.obrot` now reports a
length of 0.320 and sweeps a full blade rotation across it, where before it
reported 3.160 and returned the same pose at every time up to 2.84.
