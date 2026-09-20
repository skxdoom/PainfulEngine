# Animation

The clock, the pose, the joints, root motion, blending and the `.ani` file's
own timing. Ragdolls are in [`Hitboxes.md`](Hitboxes.md) and
[`Physics.md`](Physics.md).

## `MDL` is four systems, not one

Counted over the shipped scripts:

| system | calls | what it is |
|---|---:|---|
| **Joints** | ~430 | `GetJointIndex` 217, `TransformPointByJoint` 126, `GetJointPos` 41, `GetJointName` 22 — where a weapon, effect or gib attaches to a bone |
| **Ragdoll** | ~300 | `EnableRagdoll` 101, `SetRagdollLinearDamping` 104, `SetRagdollAngularDamping` 100, and a dozen more — a separate physics system |
| **Mesh/material** | ~150 | `SetMeshVisibility` 98, `SetTexture` 33, `SetMaterial`, `SetBlendAlpha` — not animation at all |
| **The clock** | ~80 | `SetAnimTimeScale` 41, `SetAnim` 17, `GetAnimTimeScale` 11, `GetAnimTime` 6, `GetAnimLength` 4, `SetAnimTime`, `ResetFrame`, `GetAnimMovement` |

The clock is the smallest of the four by call count and the one the rest of
the game waits on.

## The clock gates the actor's event loop

`CActor` gates its entire animation-event loop on it:

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
`_CurAnimLength`. `GetAnimTimeScale` is the speed, not a play/pause flag.

**The loop is in `CActor:Update`, not `CActor:Tick`.** `Update` is driven by
`GObjects:Update()`, which `Game:Tick` calls `Game.Loops` times a frame - and
`Loops` comes from `delta * 30`, so the actor logic runs at a fixed **30 Hz**
while rendering runs free.

The clock needs no rendering: a per-entity timer, a duration read from the
`.ani`, and an index. Check: `SetAnim(idle)` on Cathedral returns index 0 with
the file's own length, an unknown track returns -1, and an `EvilMonkV2` set to
`atak1` fires `damage` at 0.75 s.

## Where the code is

`Assets/Ani` parses `.ani` in full (per-bone variable-length tracks of
parent-relative matrices, keys rebased - "A `.ani` is a slice of a longer take"
below).
`Assets/AnimationCache` and `Assets/SkeletonCache` load per model.
`Assets/Skeleton` has the hierarchy, `ResolveAnimTracks`, the time-sampled and
blended poses (`ComputeBoneWorldAtTime`, `ComputeBoneWorldBlended`,
`ComputeBoneLocalBlended`), `BoneWorldToSkinning` and `SkinMeshVertices`.

Animations resolve by filename: **`<Model>.<anim>.ani`** — `evilmonk.idle.ani`
for `evilmonk.pkmdl` with anim `"idle"`. 1228 animation files across 284
models.

## The pose lives on the entity; skinning is on the CPU

`ScriptEngine::TickAnimations` poses each animated entity (`PosedBones`) and
hands the skinning matrices to `EntityRenderer::SetScriptSkinning`; the
renderer does no posing of its own. Two reasons: the joint natives have to
answer with no window open, and a pose computed in two places can disagree with
itself - a flash drawn at one pose and spawned at another.

The renderer keeps the CPU mesh of a skinned model and deforms it into a
per-instance dynamic vertex buffer, for **visible** instances only, so an actor
across the map costs one pass over its skeleton and nothing else. GPU skinning
is not done; the `pose` report below is the oracle it would be diffed against,
one bone at a time.

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

`PainfulTools pose <file.pkmdl> <anim> [time]` reports bind-pose bounds against
posed bounds, and this is the check that the maths is right rather than merely
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

## The skeleton answers questions

`SkeletonCache` loads bones, bind-pose world matrices and their inverses per
model, and the joint natives answer from the entity's pose. A joint native that
returns a plausible wrong value (the entity's own position, -1) is the
dangerous kind of stub: nothing errors and a muzzle flash simply appears at a
monster's feet.

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

### Verifying the joints without a window

`PainfulTools bones <file.pkmdl> [anim] [time] [joint:ax,ay,az]` reports every bone's
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
| 5 | blend seconds | `0.2` (the port defaults to `0.201`, `CActor`'s own fallback) |
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


## OPEN: two entity teleports that are NOT the pose

Both found by logging per-frame entity steps with the AI running
(`PAINFUL_PLAYER_AT="-291.9,-2.4,-14.0"` on Cathedral, 900 frames). Baselines
from the same run: `walk` averages 0.0193 units a frame, `idle` and `atak1` are
effectively zero.

**A dying monk snaps to exactly (0,0,0).** `EvilMonkV2_ThrowAndDie_001` moves
275.98 units to the world origin the frame it dies, on `atakthrow_deth`, with
`_died = true` and `MDL.IsRagdoll` false. Bracketing the frame shows the write
lands in the ENGINE phase, not the script phase. Ruled out by measurement:
`ENTITY.SetPosition` and `ENTITY.PO_Move` (hooked from Lua, never called with a
large delta), `PlaceAttached` (the monk is a child's PARENT, not a child), and
guarding `TickRagdolls`' root adoption on `PhysicsWorld::RagdollActive` - which
did not change the result. Whatever writes it is one of the remaining engine
ticks, and finding it wants a C++ probe over `e.pos` rather than more Lua hooks.

**`atakthrow` moves the actor when nothing should be moving it.** Sustained
0.111-0.142 units a frame for seven frames - about 8 units a second, ~0.9 units
of travel - with `_moveWithAnimation` false, so `MoveWithAnimation` is not the
source. Mean step over the animation is 0.0366 against 0.0014 for `atak1`.

## The `SetAnim` index contract

`SetAnim` returns an index the scripts keep (`_CurAnimIndex`) and hand back to
every other call. Per-entity, stable, and `-1` must stay the "no such track"
answer the scripts already handle.

## How to verify, headlessly

- `GetAnimLength` matches the `.ani`'s own `duration()`.
- `GetAnimTime` advances at `speed x dt`, wraps for a looping animation and
  stops at the end for a one-shot.
- **The decisive one:** an actor's animation events fire. Hook a method named
  in a template's event list and assert it is called at the declared time.

## The mover drives, the animation plays in place

Asked because a walk cycle that loops on the spot looks like a bug, and because
getting this backwards would be invisible until every monster in the game
walked at the wrong speed.

**It is not root motion.** PainEngine makes the same division a modern engine
does: `UpdateWalking` moves the actor at the template's `WalkSpeed`/`RunSpeed`
through `ENTITY.PO_Move`, and the animation plays in place with its baked
travel removed from the pose. Root motion is an explicit per-animation opt-in
for special moves, not how locomotion works.

The animations DO carry travel - `evilmonkv2.walk` slides `ROOOT` 26.96 model
units down +Z, `run` slides 50.68 - so the question is only ever whether that
travel is what moves the monster. Three measurements say no:

| | WalkSpeed | RunSpeed | anim walk | anim run |
|---|---:|---:|---:|---:|
| `Bagbaby` | 1.0 | 1.0 | 1.0 | **2.5** |
| `Corn` | 1.0 | 1.0 | 1.0 | **2.0** |
| `Boy` | 1.0 | 1.0 | 0.7 | **1.5** |
| `Deto` | 1.6 | **2.6** | 1.0 | **0.9** |

1. Most monsters declare **`WalkSpeed == RunSpeed`** while their walk and run
   animations play at very different rates. Under root motion Bagbaby would run
   2.5x faster than it walks; the template says both are 1.0.
2. **`Deto` inverts it.** It runs 1.6x faster than it walks (1.6 -> 2.6) while
   its run animation plays SLOWER than its walk animation (1.0 -> 0.9). No
   root-motion scheme can produce that.
3. `evilmonkv2`'s run bakes 1.88x walk's travel *and* plays 2.13x faster
   (0.9 -> 1.92), which under root motion is about 4x the ground speed. Both
   are declared 1.2.

So the per-animation speed multiplier is hand-tuned FOOT SYNC - make the stride
look right at the speed the mover is already going - and it is not even
consistent with the declared speed, which is the tell.

### The two flags are different questions

`s_SubClass.Animations[name]` is `{speed, movingcurve, events, blend, moveWith}`:

| element | meaning |
|---|---|
| `[2]` `movingcurve` | this animation HAS baked travel: report it through `GetAnimMovement`, and take it out of the pose |
| `[5]` | SPEND that travel on the actor - real root motion, for lunges and boss moves |

`CActor:SetAnim` resets `_moveWithAnimation` to false (`CActor.lua:732`) before
re-deciding it from `[5]`, so it is scoped to one animation and does not leak
into the next.

**The pose must therefore strip the travel whenever the curve declares it**,
whether or not anything spends it. Leaving it in makes the mesh stride away
from the monster and snap back every time the loop wraps - measured at 3.215
world units of drift on `walk` and a 3.186-unit snap at the wrap. Gating the
subtraction on "did someone take it" was tried and is wrong for exactly that
reason.

### Harness notes, both of which produced false readings first

- **`animTime` is per ENTITY, not per slot.** `MDL.GetAnimTime(e, idx)` reports
  the clock of whatever is actually playing, so a probe that sets one animation
  and reads another's index silently measures the wrong thing.
- **`CActor` re-sets the animation every tick and takes the clock back.** Set
  `_enabledRD = true` and `CActor:SetAnim` bails at its first guard, which lets
  a test own the animation. Without it, `MDL.SetAnim` from a probe is undone
  before the next pose - and since `SetAnim` also resets `animTime` to 0, the
  clock appears frozen.

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
headless the pose is computed only when a joint native asks - so the correction
has to be forced to be seen outside a real game.


### Both sides of a cross-fade, or the mesh jumps when it stops

A blended pose contains the OUTGOING animation's bones as well as the incoming
one's, and the outgoing bones still carry their root travel. Subtracting only
the incoming animation's curve leaves the previous one's accumulated stride in
the blend.

Fading from `walk` to `idle` is the common case - every monster that reaches its
destination does it - and idle declares no curve at all, so nothing was removed.
Measured on Cathedral, posed `ROOOT` distance from the entity across the switch:

| frame | before | after |
|---|---:|---:|
| f-1 (walking) | 0.000 | 0.000 |
| f (switch) | **2.922** | 0.000 |
| f+1 | 2.679 | 0.000 |
| ... | -0.244 a frame | 0.000 |
| f+12 (fade done) | 0.000 | 0.000 |

So the mesh snapped nearly three units the instant the monk stopped and slid
back over the 0.2s fade. The offset is now computed for each side from ITS OWN
slot's mask and bone, at its own time, and blended with the same weight the
pose is - which is zero for a curve-less animation and makes the arithmetic
work without a special case.

Checked in all four directions (`walk`->`idle`, `idle`->`walk`, `walk`->`atak`,
`atak`->`walk`): biggest one-frame step 0.000 in each.

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

Checked with `PainfulTools blend <file.pkmdl> <animA> <animB> [time]`, which reports a
bone at five weights. Fading `evilmonkv2` from `idle` into `atak`, the head
travels smoothly from (0.644, 8.116, 2.645) to (1.498, 7.664, 1.813) - and its
distance from its parent reads **2.5932 at every weight**. That constant is the
real test: a blend that lerped the matrices entry by entry would shorten the
bone through the middle of the fade, and going through the quaternion does not.

### An interrupted fade continues from the pose on screen

Reported from play: the held weapon's walk animation "reset" with no fade when
walking was toggled forward/backward quickly. The chain is all in the shipped
scripts: `CPlayer:Steps` decides `_Walking` from `PLAYER.FloorCheck`, the
movement keys and `ENTITY.GetVelocity > 2`, and at a reversal the velocity
passes through zero for a frame, so `_Walking` drops for that frame;
`CWeapon:Tick` then sets `idle`, and `walk` again the frame after. The
original does the same. What differed was the fade's SOURCE: the port froze
the *outgoing animation* and its time, so the second `SetAnim` — arriving one
frame into the walk-to-idle fade — started fading from the idle pose, while
the pose on screen was still almost entirely the walk. The mesh jumped to
idle and faded back.

`SetAnim` now notices a fade still in flight and freezes the **blended local
poses** themselves (`Entity::blendFromLocal`, with the root-motion offset the
blend carried in `blendFromOffset`); the new fade runs from that snapshot
(`ComputeBoneWorldFromLocals`), and a snapshot-based fade interrupted again is
re-frozen the same way (`ComputeBoneLocalFromLocals`). The snapshot is taken
without script joint rotations, which go on after the blend as before.

Measured on the Bridge spawn through the real path (`Game.CameraFromPlayer`
on, `INP.Action` fed, forward/backward every 10 frames): the weapon's
`Bip01 R Hand` moves at most 0.0064 per frame, and that is the walk cycle
itself; the frames around each idle/walk switch step 0.002–0.004. Before, the
switch frame jumped by the whole walk-to-idle distance, about 0.05.

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

Verify with `PainfulTools pose <file.pkmdl> <anim> [time]`: `PKW.obrot` now reports a
length of 0.320 and sweeps a full blade rotation across it, where before it
reported 3.160 and returned the same pose at every time up to 2.84.
