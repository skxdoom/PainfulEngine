# Vec3 — the vector type, and how it is being adopted

`Source/Core/Vec3.h`. A 3-vector with the layout of `float[3]`, introduced to
be adopted **one function at a time** rather than in a sweep.

## Why it has the shape it has

The engine passes positions and normals as raw `float[3]` because that is what
the file formats, the Lua stack, Jolt and bgfx all speak. There are 904 such
declarations and 58 hand-written `for (int i = 0; i < 3; ++i)` copy loops, plus
`Dot`, `Cross` and `Normalize` written out again in three different files.

A vector type that could not be mixed with `float[3]` would force all of that
to change at once. So Vec3 keeps the layout and meets the old code in both
directions:

| | |
|---|---|
| `operator const float*` | implicit — a Vec3 passes to any existing `const float[3]` parameter unchanged |
| `.p()` | explicit `float*` — an out-parameter, so a write through it is visible at the call site |
| `.Store(out)` | writes into a `float[3]` |
| `AsVec3(a)` | reads a `float[3]` **in place**, both const and mutable, no copy |

Four `static_assert`s hold the promise up: three packed floats, no
over-alignment, standard layout, trivially copyable. `p()` and `AsVec3` are the
only places that depend on it, and if any assert ever fails those conversions
have to go rather than be patched.

`operator[]` branches on the index instead of indexing off `&x`, so only the
two named conversions rely on the layout at all.

## The one behaviour to know

`Normalized()` returns the zero vector for zero length rather than a NaN. A
script divides a velocity by its own length the frame the thing is standing
still, and a NaN reaching the solver takes the process down.

That is *not* the same rule the old hand-written helpers used. `Lighting.cpp`
normalised only above a `1e-6` floor and left shorter vectors alone. That floor
is kept where it was rather than widened — see `Normalize` there, now three
lines over Vec3 instead of its own arithmetic. **When converting, check the
epsilon before assuming `Normalized()` is a drop-in.**

## Testing a conversion

`PainfulTools selftest` — 45 numeric checks of the type itself: the layout
promise, arithmetic, the geometric identities (cross is perpendicular and
anticommutes, dot commutes), the degenerate cases, and interop with `Mat4::
TransformPoint` and the `(w,x,y,z)` quaternion helpers. It is the report Vec3
has instead of game data, and it fails loudly with a non-zero exit.

The self-test proves the *type*. It does not prove a *conversion*, and the way
those were checked is worth repeating:

1. Find a report that exercises the converted maths and produces numbers.
2. Capture it.
3. `git show HEAD:<file> > <file>`, rebuild, capture again.
4. Diff. Restore.

That caught nothing, which is the point — it is what makes "no regression"
a measurement rather than a hope.

| Converted | Exercised by | Result |
|---|---|---|
| `World/Decals.cpp` — basis, projection, backface cull; local `Dot`/`Cross`/`Normalize` deleted | 420 decal spawns over real geometry, 123 non-empty cuts, `PAINFUL_DECAL_TRACE` | identical |
| `World/Lighting.cpp` — local `Normalize`, `Dist` | `lighting` at 7 probe points | identical |
| `Render/ParticleRenderer.cpp` — local `Cross`, the spark streak basis | `particles` report; 1148 and 2846 live particles in-game | identical |
| `Game/ScriptCollision.cpp` — `FixGrenadeFlight`'s mirror and 2 mm step-off | 75 grenades fired into geometry, resting places to 4 dp | identical |

## What is left

Most of the 904 remain. The order that makes sense, hardest last:

1. The rest of the ad-hoc maths — `PlayerPawn.cpp` (6 sites), `ScriptLimbs`,
   `ScriptExplosion`, `AudioEngine`, `Properties`.
2. Local variables in the dense files — `PhysicsWorld.cpp` (67),
   `ScriptEntity.cpp` (32), `EntityRenderer.cpp` (29), `ScriptAnim.cpp` (28).
3. **Storage** — `ScriptEngine::Entity::pos`, `PhysicsWorld`'s structs,
   `Hke.h`. Layout-compatible, so it is safe, but it touches save/load and the
   renderer's instance data, and wants its own before/after pass.

A `Quat` for the 288 `float[4]` rotations is the obvious companion and does not
exist yet.
