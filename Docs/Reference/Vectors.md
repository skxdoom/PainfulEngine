# Vec3 — the vector type, and how the engine was moved onto it

`Source/Core/Vec3.h`. A 3-vector with the layout of `float[3]`.

## Why it has the shape it has

The engine passed positions and normals as raw `float[3]` because that is what
the file formats, the Lua stack, Jolt and bgfx all speak. Vec3 keeps that
layout and meets array code in both directions, which is what made an
incremental conversion possible at all:

| | |
|---|---|
| `operator const float*` | implicit — a Vec3 passes to any `const float[3]` parameter unchanged |
| `.p()` | explicit `float*` — an out-parameter, so a write through it is visible |
| `.Store(out)` | writes into a `float[3]` |
| `AsVec3(a)` | reads a `float[3]` **in place**, const and mutable, no copy |

Four `static_assert`s hold the promise up: three packed floats, no
over-alignment, standard layout, trivially copyable. `p()` and `AsVec3` are the
only things that depend on it. `operator[]` branches on the index rather than
indexing off `&x`, so it does not.

`Mat4::TransformPoint` and `EngineQuatRotate` each gained a value-returning
`Vec3` form beside the out-parameter one.

## The one behaviour to know

`Normalized()` returns the zero vector for zero length rather than a NaN — a
script divides a velocity by its own length the frame the thing is standing
still, and a NaN reaching the solver takes the process down.

That is **not** the rule the old hand-written helpers used. `Lighting.cpp`
normalised only above a `1e-6` floor; `ScriptLimbs` had its own `1e-12` on the
squared length, with a fallback whose value is recovered behaviour (it matches
what `RayCast` returns for a degenerate contact). Both floors are kept where
they were. **When converting, check the epsilon before assuming `Normalized()`
is a drop-in.**

## What is converted

`float[3]` went from **904 → 7**. The seven left are all deliberate: four in
`Vec3.h` itself (the constructor, `Store`, and the two `AsVec3` overloads —
the bridge has to speak arrays), two in the self-test that exist to prove that
bridge works, and `MenuSystem`'s `cols[3]`, which is three column widths on the
key-bindings screen and not a vector at all.

Storage, parameters and return types are converted across all nine layers,
including the `PhysicsWorld`, `ScriptEngine`, `PlayerPawn` and `EntityRenderer`
interfaces. `Properties::Vector3`, `MapObject::position`/`normal` and
`Camera::Forward`/`Right` speak Vec3 now, so their callers got it for free.

## What is NOT converted

**The loop bodies.** Hand-written three-element loops went 299 → 297: the
conversion changed declarations and signatures, not the code inside functions.
Most of what remains is now a plain assignment waiting to happen —

```cpp
for (int c = 0; c < 3; ++c) part.pos[c] = pos[c];        // part.pos = pos;
for (int c = 0; c < 3; ++c) dir[c] /= len;               // dir /= len;
for (int c = 0; c < 3; ++c)
    part.velocity[c] = inherited[c] + dir[c] * speed;    // inherited + dir * speed
```

— concentrated in `ScriptEntity` (24), `ScriptAnim` (20), `ScriptDeath` (17),
`ScriptTrace` (16). That is the next pass, and it is the one that actually
collects the readability the type was introduced for.

`float[4]` (121 quaternions), `float[9]` (48 row-vector 3x3) and `float[16]`
(14 matrices) are untouched. A `Quat` is the obvious companion and does not
exist.

## Testing a conversion

`PainfulTools selftest` — 46 numeric checks of the type: the layout promise,
arithmetic, the geometric identities, the degenerate cases, and interop with
`Mat4` and the `(w,x,y,z)` quaternion helpers, including that each value form
agrees with its out-parameter form.

The self-test proves the *type*. A *conversion* is proved by capturing a report
that exercises the maths, rebuilding the pre-change version (`git show
HEAD:<file> > <file>`), capturing again, and diffing.

| Probe | Covers |
|---|---|
| 13 battery reports | level, zones, particles, billboards, lighting, entities, map, shaders, two `lua` runs |
| 75 grenades fired into geometry | `FixGrenadeFlight`'s mirror and 2 mm step-off, to 4 dp |
| 420 decal projections | basis, projection, backface cull, whole-object reject |
| 6 ragdoll rigs dropped and settled | the `.hke` constraint graph, `csToRef`/`csToAtt` |
| Lighting at 7 points | environment fade, the four slots, the half-vector |
| Physics probe | static world build, prop shapes, camera push |

All identical across the whole conversion.

## What a blanket sweep costs

The early passes were per-subsystem and verified one at a time. The last one
was a single regex over the whole tree, and it is worth recording what that
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

The rule that falls out: a regex cannot tell "three floats that are a vector"
from "three floats that happen to be three floats", and it cannot tell code
from the test that guards it. Sweep the mechanical part if you like, but read
every site the compiler stops on, and never let a sweep touch the tests.
