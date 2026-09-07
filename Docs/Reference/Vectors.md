# The maths types, and how the engine was moved onto them

`Source/Core/Vectors.h` holds `Vec3` and `Quat`; `Source/Core/Matrix.h` holds
`Mat4` and the 3x3 conversions. Each has the layout of the float array it
replaced.

## Why they have the shape they have

The engine passed positions as raw `float[3]` and rotations as `float[4]`
because that is what the file formats, the Lua stack, Jolt and bgfx all speak.
Both types keep that layout and meet array code in both directions, which is
what made an incremental conversion possible at all:

| | |
|---|---|
| `operator const float*` | implicit — the value passes to any `const float[N]` parameter unchanged |
| `.p()` | explicit `float*` — an out-parameter, so a write through it is visible |
| `.Store(out)` | writes into the array |
| `AsVec3(a)` / `AsQuat(a)` | reads the array **in place**, const and mutable, no copy |

Four `static_assert`s per type hold the promise up: packed floats, no
over-alignment, standard layout, trivially copyable. `p()`, `AsVec3` and
`AsQuat` are the only things that depend on it. `operator[]` branches on the
index rather than indexing off the first member, so it does not.

## The behaviours to know

`Vec3::Normalized()` returns the zero vector for zero length rather than a NaN;
`Quat::Normalized()` returns the identity. A script divides a velocity by its
own length the frame the thing is standing still, and a NaN reaching the solver
takes the process down.

That is **not** the rule the old hand-written helpers used. `Lighting.cpp`
normalised only above a `1e-6` floor; `ScriptLimbs` had its own `1e-12` on the
squared length, with a fallback whose value is recovered behaviour (it matches
what `RayCast` returns for a degenerate contact). Both floors are kept where
they were. **When converting, check the epsilon before assuming `Normalized()`
is a drop-in.**

## The rotation convention

`Quat` is engine order **(w, x, y, z)**, and it is the single authority for the
convention — the `float[4]` free functions that used to hold it are gone.

- `Quat::Rotate(v)` is `conj(q) * v * q`, which is the row-vector reading of a
  rotation: `v' = v R(q)`.
- **`a * b` therefore applies `a` FIRST**, matching `EngineRot9Mul` on the
  matrix side. The old comment on `EngineQuatMul` said the opposite and had said
  so since it was written; the self-test caught it the day the type landed. The
  code was always right, only the comment was wrong, so nothing moved.
- `Quat::FromEuler(x, y, z)` is `qz * qy * qx` — X applied first —
  read out of the native behind `0x1011C390`, whose maths is `FUN_1011bea0`.
- `EngineQuatToRot9` / `EngineRot9ToQuat` (`Core/Matrix.h`) cross between the
  quaternion and the row-vector 3x3 the renderers use. The 3x3 is the engine's
  own textbook matrix, **not** transposed.
- `Nlerp(a, b, u)` blends two rotations the short way round (`q` and `-q` are
  the same rotation, and without the flip a bone spins most of a turn between
  two keys). It is the animation blend, lifted out of `Skeleton.cpp`.

The **script-facing** quaternion is a separate type: `QuatD` in
`Source/Script/Natives.cpp`, deliberately `double`, because the Lua stack is
doubles and those natives compose in that precision.

## What is converted

`float[3]` went from **904 → 7**, `float[4]` quaternions from **~37 → 0**.

The `float[3]` left are deliberate: four in `Vectors.h` itself (the
constructor, `Store`, and the two `AsVec3` overloads — the bridge has to speak
arrays), two in the self-test that exist to prove that bridge works, and
`MenuSystem`'s `cols[3]`, which is three column widths and not a vector.

Storage, parameters and return types are converted across all nine layers.
`Properties::Vector3`, `MapObject::position`/`normal`, `Camera::Forward`/`Right`,
`ScriptEngine::Entity::rot`, `Entity::parentRot` and `ScriptBodyPose::rot`
speak the types now, so their callers got it for free.

Two renames came with the quaternion pass, because the type now states what the
name used to: `rotWXYZ` → `rot`, `quatWXYZ` → `rot`, `parentRotWXYZ` →
`parentRot`. Where a `float[9]` rotation matrix sat in the same scope it became
`rot9`, so `rot` means a quaternion everywhere.

## What is NOT converted

The remaining `float[4]` are **not** rotations and must not be swept: bgfx
uniform vec4s (`fog_`, `fogColor_`, `params`, `ambientValue`), UV transforms
(`uvAnim`, `tile`, `detail`, `waterParams`), the clip-space point in
`ProjectToScreen`, and plain four-element tables (`kListCols`, `radii`,
`centres`, `textRect`). Converting a uniform breaks bgfx **silently**.

**The loop bodies.** Hand-written three-element loops stand at ~295: the
conversion changed declarations and signatures, not the code inside functions.
Most of what remains is a plain assignment waiting to happen —

```cpp
for (int c = 0; c < 3; ++c) part.pos[c] = pos[c];        // part.pos = pos;
for (int c = 0; c < 3; ++c) dir[c] /= len;               // dir /= len;
```

— concentrated in `ScriptEntity`, `ScriptAnim`, `ScriptDeath` and `ScriptTrace`.
`float[9]` (row-vector 3x3) and `float[16]` (matrices) are untouched.

## Testing a conversion

`PainfulTools selftest` — 69 numeric checks: the layout promise, arithmetic,
the geometric identities, the degenerate cases, `Mat4` interop, and for `Quat`
the component order, the Euler composition, the rotation identities, the
composition order (including a check that the two orders actually differ, so
the test discriminates), agreement with `EngineRot9Mul`, and `Nlerp`.

The self-test proves the *types*. A *conversion* is proved by capturing a report
that exercises the maths, rebuilding the pre-change version (`git show
HEAD:<file> > <file>`, or a `git stash` round-trip for a change that spans
interfaces), capturing again, and diffing.

| Probe | Covers |
|---|---|
| 15 battery reports | level, zones, particles, billboards, lighting, entities, map, shaders, two `lua` runs |
| 680 animation probes | `pose`/`bones`/`blend` over 6 rigs × their animations × 15 times, with and without a joint override — the `Nlerp` path |
| 75 grenades fired into geometry | `FixGrenadeFlight`'s mirror and 2 mm step-off, to 4 dp |
| 420 decal projections | basis, projection, backface cull, whole-object reject |
| 6 ragdoll rigs dropped and settled | the `.hke` constraint graph, `csToRef`/`csToAtt` |
| Lighting at 7 points | environment fade, the four slots, the half-vector |
| Physics probe | static world build, prop shapes, camera push |

All identical across the whole conversion.

## What a blanket sweep costs

The early passes were per-subsystem and verified one at a time. One `float[3]`
pass was a single regex over the whole tree, and it is worth recording what that
bought and what it broke, because the trade is not obvious.

It worked in the sense that converting the entire `PhysicsWorld` API — 34
parameters plus definitions — produced **three** compile errors, none of them
from a caller: the storage work had already moved every call site onto Vec3.

It also produced five classes of damage:

| | Caught by |
|---|---|
| `Vec3& x;` local | compiler — a reference needs an initialiser |
| `Vec3& x = {…}` | compiler — non-const ref to a temporary |
| `Vec3& R[3]` | compiler — arrays of references are illegal |
| Half-converted declarator lists (`Vec3 lo = {…}, hi[3] = {…}` — `hi` became `Vec3[3]`) | compiler, but only because of how those were used |
| **Non-vector arrays converted** (`cols[3]` column widths, `angles[2]`) | reading the sites the compiler pointed at |

And one that matters more than the rest: **it converted the self-test's
deliberate `float raw[3]` and `float out[3]` to `Vec3`**, which would have
turned "AsVec3 reads a float[3] in place" into a Vec3-to-Vec3 tautology. It
surfaced only because `AsVec3(Vec3)` picked the const overload and failed on an
assignment. Had it compiled, the test would have reported 45/45 while testing
nothing, and that number was being quoted as evidence for the whole conversion.

The later `rot9` rename repeated the lesson in miniature: a line-ranged sed for
the *local* `float rot[9]` also hit `e.rot`, the entity's quaternion, in five
places. That one did compile — `EngineQuatToRot9(e.rot9, rot9)` is well-formed
if the member exists — and was caught by reading the diff, not by the build.

The rule that falls out: a regex cannot tell "three floats that are a vector"
from "three floats that happen to be three floats", it cannot tell a local from
a member of the same name, and it cannot tell code from the test that guards it.
Sweep the mechanical part if you like, but read every site the compiler stops
on, read the diff for the ones it does not, and never let a sweep touch the
tests.
