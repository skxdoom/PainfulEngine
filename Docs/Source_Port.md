# Painkiller source port — architecture and roadmap

Grounded in the reverse engineering recorded in the findings in Part II below. Every
number here was measured against the shipped data in this repository, not estimated.

## The strategic finding

**Painkiller's gameplay logic is not compiled.** It lives in 431 Lua 5.0 files plus
thousands of serialised property tables (`.CActor`, `.CItem`, `.CWeapon`, ...).
Weapons, monsters, AI, HUD, menus and level scripting are all script-side.

That changes what a source port *is*. You are not reimplementing Painkiller's game
design — you are implementing the **native API those scripts call**, and letting the
original scripts drive it. The porting specification is therefore concrete and
finite rather than open-ended:

| Measure | Count |
|---|---|
| Native Lua functions recovered from `Engine.dll` | 941 |
| ...excluding Lua's own standard library | ~846 |
| ...actually called by the shipped scripts | **790** |
| ...covering **80%** of all 14,387 call sites | **113** |

A prioritised, call-frequency-ranked list is in
[`native_priority.tsv`](native_priority.tsv) (name, call count, module). Start at
the top of that file; it is the work queue.

The top of the queue is dominated by a few modules — ragdoll/joints
(`GetJointIndex`, `SetAnim`, `GetJointPos`, `TransformPointByJoint`), world
mesh/collision (`PO_Create`, `SetVelocity`, `GetPosition`, `EnableCollisions`),
console/logging (`Print`, `AddMessage`), and sound. Implement those and a large
fraction of the scripts start doing something.

## Layer map

## Implementation language

**C++17 is the engine language**, with the asset layer in
[`PainEngineKit/`](PainEngineKit/) — a CMake static library plus a `pekit` CLI,
built with MSVC 2022 (`/W4 /permissive-`, clean). This is the code the port builds
on.

`PainKit/` (C#) is retained as a **reference implementation and cross-check**. The
two are independently written from the same format notes, and they agree exactly on
every measured quantity — object, vertex, triangle, material, texture, bone, skin
and keyframe counts — which is far stronger evidence than either alone. The C#
side also holds the glTF exporter, which is useful for getting assets into Blender
but is not needed at engine runtime.

```
cmake -S RE/PainEngineKit -B RE/PainEngineKit/build -G "Visual Studio 17 2022" -A x64
cmake --build RE/PainEngineKit/build --config Release
pekit map   Data_Extracted/Maps/1x01_Chaos.mpk
pekit pose  Data_Extracted/Models/clown.pkmdl Data_Extracted/Models/clown.walk.ani 10
```

`pekit pose` runs the full chain — parse model, rebuild hierarchy, invert bind
matrices, sample the animation, skin the mesh — and prints the deformed bounds, so
the whole pipeline stays verifiable from the command line.

`.pak` reading is implemented natively in the engine
(`Source/Core/PakArchive.*` + `Source/Core/FileSystem.*`): the shipped `Data/`
folder mounts directly, with `Data_Extracted/` remaining a loose reference
tree rather than a requirement. Inflate comes from the miniz already compiled
inside bimg (see the CMake notes on link order).

| Layer | Status | Notes |
|---|---|---|
| Asset decoding | **Done** — `PainEngineKit/` (C++), `PainKit/` (C#) | mpk + materials, pkmdl, ani, skeleton, skin; pak native in the engine |
| Lua host | **Boots** — vendored Lua 5.0.2, Loader.lua + Game:Init + frame ticks clean | see `LuaHost.md` |
| Native API | Instrumented stubs + first real impls | 790 functions; 113 give 80% coverage |
| Physics | Decided: **Jolt** (MIT) | see FINDINGS §3c; `.rde` gives ragdoll tuning |
| Renderer | Not started | full reimplementation required (see below) |
| Audio | Not started | Miles → OpenAL-Soft / miniaudio |
| Netcode | Deliberately last | GameSpy is dead; would need new transport |

### Why the renderer is a full rewrite
`D3Dev.dll` exposes exactly **two** exported functions, `CreateDevice` and
`DestroyDevice`. Everything else is internal, so there is no usable seam to
reimplement against — the renderer must be written from scratch. The good news is
that what it must *draw* is fully understood: `.mpk` geometry with two UV sets
(diffuse + lightmap), and skinned `.pkmdl` meshes.

### Why physics is tractable
PCF already wrapped Havok behind `PhysicsWorld` / `PhysicsObject` / `Ragdoll`
using opaque handles and engine-native math types, and collision is **built at load
time from `WorldMesh`/`Model` geometry** rather than deserialised from Havok. The
17,643 `.mopp` files are a Havok-specific *cache* derived from that geometry — a
Jolt backend regenerates its own acceleration structure and ignores them entirely.

## Roadmap

Each milestone is chosen to end in something runnable, so progress stays verifiable.

### M0 — Asset pipeline ✅ done
`PainKit` reads every major format and exports verified glTF.

### M1 — Asset viewer
Load a `.mpk` level and `.pkmdl` characters, render statically, fly a camera around.
No Lua, no physics. Proves the asset pipeline inside a real renderer.
*Done when:* you can fly through `DM_Trainstation` and see a character standing in it.

### M2 — Lua host with instrumented stubs ← **highest leverage**
Embed Lua 5.0.2, point it at `Data/LScripts/`, and run `Loader.lua`. Stub **every**
native to log its name and arguments, then return a neutral value.

This is the highest-value step in the whole project, because it converts guesswork
into data: the log tells you exactly which natives the game calls, in what order,
with what argument types. Combine it with `native_priority.tsv` and the
implementation order stops being a judgement call.
*Done when:* `Loader.lua` completes and the class hierarchy registers without errors.

### M3 — Static world + collision
Renderer draws the level; Jolt loads the same geometry as a static mesh. Honour the
**name-substring semantics** (`noclip`, `portal`, `antyp`, `zone`, `death`,
`ladderzone`, `barrier`, `glass`, `phys`, `statdest`) — these are the level's entity
system, not decoration. `MapObject.IsCollidable` in PainKit already models the basic
filter.
*Done when:* a debug capsule can walk and collide through a real level.

### M4 — Player and weapons
Implement the player/camera/input natives and enough of the weapon API for the
scripts to fire. Weapon behaviour comes from the shipped `.CWeapon` tables.
*Done when:* you can move, shoot, and hit something.

### M5 — Monsters, animation and ragdolls
Skinned rendering, animation playback (`.ani` is fully decoded), the AI natives,
and Jolt ragdolls driven by `.rde` parameters.
*Done when:* a monster spawns from its `.CActor`, walks, takes damage, and ragdolls.

### M6 — HUD and menus
The Menu/GUI module is the single largest native group (146 functions), but it is
self-contained and can be deferred safely.

### M7 — Audio, particles, polish
Sound system (31 natives) + sound instances (14), particle effects (`.pfx`).

## Legal boundaries

Reverse engineering for interoperability and modding a game you own is the classic
legitimate case, but keep it clean:
- Ship **no original assets or binaries**. The port must require the user's own copy.
- Write your own code. Do not copy disassembly output into the implementation.
- Distribute mods as loose files or `.pkm` packages, never repackaged originals.

## Known unknowns

These are the honest gaps that will need work during the port:
- Material blocks are now solved for both formats, so exports can be textured.
  Remaining: 7 of 2,532 model meshes still fail the exact-landing material parse,
  and a few per-object bytes after the last `.mpk` material are unmapped. Neither
  blocks rendering.
- **Native signatures.** We have names and addresses but not argument lists. M2's
  instrumented stubs are the cheapest way to recover them — log the actual Lua
  arguments at runtime rather than reading disassembly.
- **`.pfx` particles** and the remaining small unidentified native tables.
- **Gameplay feel.** `PhysicsObject::FixHavokPositionBug` shows behaviour was tuned
  around Havok's quirks; ragdoll feel under Jolt will differ and need retuning.
- The `.pkmdl` geometry header preceding the index array varies between models, so
  PainKit uses a strict-then-loose heuristic. Fully mapping it would remove that.

## Using PainKit today

```bash
dotnet run --project RE/PainKit -- model info Data_Extracted/Models/clown.pkmdl
dotnet run --project RE/PainKit -- map   info Data_Extracted/Maps/DM_Trainstation.mpk
dotnet run --project RE/PainKit -- gltf  Data_Extracted/Models/clown.pkmdl out/clown.gltf Data_Extracted/Models/clown.walk.ani
dotnet run --project RE/PainKit -- verify Data_Extracted/Models/clown.pkmdl out/clown.gltf Data_Extracted/Models/clown.walk.ani
```

`verify` re-reads the written glTF using glTF's own conventions (column-major,
column-vector, `globalJointTransform * inverseBindMatrix`) and compares the skinned
result against the native pipeline. On the shipped characters the two agree to
~5e-6 on models tens of units across — floating-point noise. That is what makes the
exporter trustworthy rather than merely plausible.

---

  edit at line 1898: old_string NOT FOUND # Painkiller (2004) / PainEngine — Reverse Engineering Notes

Working notes for learning and modding. Everything here was derived from the
shipped binaries and data in this folder; no external source was used.

## 1. Architecture at a glance

| Component | File | Role |
|---|---|---|
| Launcher | `Bin/Painkiller.exe` (1.8 MB) | Thin shell over the engine DLL |
| Engine | `Bin/Engine.dll` (4.4 MB) | Core: world, entities, physics glue, Lua VM, netcode, HUD, sound/render frontends. **2867 named C++ exports.** |
| Renderer | `Bin/D3Dev.dll` | Direct3D device behind a 2-function boundary (`CreateDevice`/`DestroyDevice`) |
| Editor | `Bin/Editor/PainEditor.exe` + its own `Engine.dll` | Same engine, +editor hooks (DLL is ~280 KB larger) |
| Sound | `Bin/mss32.dll` | Miles Sound System |
| Video | `Bin/binkw32.dll` | Bink |
| Scripting | (in Engine.dll) | **Lua 5.0.2** |
| Physics | (in Engine.dll) | **Havok** (asserts + source paths left intact) |
| Net | (in Engine.dll) | GameSpy + custom `UdpMessage`/`NetworkDevice2` |

Build path baked into the binaries: `w:\Painkiller\Game\Bin\ObjectsRelease\...`.
The exe/editor were built as *release with a debug allocator* — `Bin/memleaks.log`
prints real source file + line (`DynArray.h(383)::DynamicArray<char>::ResizeBuffer`).

### Why this engine is reverse-engineering friendly
1. **Most gameplay is Lua, not compiled.** 431 `.lua` files plus thousands of
   serialized object files (`.CActor`, `.CItem`, `.CWeapon`, ...) that are plain
   Lua property tables. You can mod huge swathes without touching a disassembler.
2. **The C++ side self-documents** via 2867 mangled exports — class names, method
   names, and full parameter types recover cleanly. See `Engine_API.md`.
3. **Third-party code is identifiable** (Lua 5.0.2 public source; Havok asserts),
   so it can be marked off and skipped during static analysis.

## 2. The Lua <-> C++ bridge — SOLVED

**941 native Lua functions recovered from `Engine.dll`, with addresses**, without
a disassembler. See `Engine_LuaAPI.md` (game) and `EngineEditor_LuaAPI.md` (editor),
generated by `tools/LuaBridge.ps1`. A Ghidra script that applies the same recovery
directly to a project database is in `tools/ghidra_PainEngineLuaAPI.py`.

The engine uses **two** registration mechanisms:

1. **`luaL_reg` tables** (the bulk — 958 entries). Stock Lua 5.0 style: arrays of
   `{ const char *name; lua_CFunction fn; }` in `.rdata`/`.data`, one array per
   engine module. Found by scanning data sections for runs of
   (pointer-to-identifier-string, pointer-to-code) pairs.
2. **Direct `Script::RegisterFunction(name, fn)` calls** (49). MSVC emits these as
   two adjacent `push imm32` instructions then a `CALL`. **All 49 call sites resolve
   to a single target, `0x10146760` — that address *is* `Script::RegisterFunction`.**

### Validation (why these results are trustworthy)
- **Exact address match against an independent source.** The DLL exports four
  `PFNAPI_DATA_TYPE` structs (`CDAPFN0506_luaMenu_*`) whose function pointers are
  reachable without any of the above. All four match the scan exactly:
  `ActivateMap` 0x10074880, `SwitchToMap` 0x10074930, `SwitchToLevelSel` 0x10074EC0,
  `SetBorderScroller` 0x1007B4E0.
- **783 of 941 recovered names (83%) are demonstrably called by the shipped Lua
  scripts.** The remainder are mostly unused/leftover API and stdlib internals.
- 100% of direct call sites converging on one address is itself strong evidence
  that the identified registrar is correct.

### Module breakdown
Grouping by registration table recovers module boundaries. ~95 of the 941 are Lua's
own standard library, so roughly **846 are genuine engine natives**:

| Module | Count | Module | Count |
|---|---|---|---|
| Menu / GUI | 146 | Havok bodies / mesh groups | 24 |
| World mesh / collision | 140 | Filesystem (`FS`) | 18 |
| World / level | 73 | Transform / orientation | 18 |
| Ragdoll / joints | 72 | Materials / textures | 18 |
| (direct call sites) | 49 | Console / demo recording | 17 |
| Render / debug draw | 40 | Scoreboard | 15 |
| Network | 39 | Waypoints / AI paths | 15 |
| Sound system | 31 | HUD drawing / Player+bot / Sound inst. | 14 each |
| Input / timing | 28 | Mouse | 10 |

Plus ~11 small unidentified tables (1–8 functions each) still worth a look.

The **editor** build registers 9 extra functions via a *second* registrar
(`0x1025A730`) — all Havok debug visualisation: `Broadphase`, `CentreOfMass`,
`Constraint`, `ContactPoints`, `DebugDisplay`, `Islands`, `Shapes`, `Statistics`,
`Tensor`. Nothing is game-only, so the retail DLL exposes the full scripting API.

`Painkiller.exe` registers **nothing** itself — it has a `.bind` section (bound
imports) and delegates all scripting to `Engine.dll`.

### Script entry point
`LScripts/Loader.lua` loads from `"../Data/LScripts/"`, pulls in
`Main/{Utils,Cfg,Tweak,Definitions,Network,Profiler}.lua`, then the class hierarchy
(`CObject` -> `CItem` -> `CActor` -> `CPlayer` / `CWeapon` / ...). Original
developer comments are in Polish (People Can Fly).

### Note on `PFNAPI_DATA_TYPE` / `CDAPFN*`
These four exported structs share a magic `0xD4F70933` and hold a function pointer
but **no name field** — they are a patch/hotfix hook table (the `0506` looks like a
patch date), not part of the Lua registry. Useful as ground truth, as above.

## 3. PAK archive format — SOLVED

Custom archive, little-endian. **Fully reverse-engineered and validated** — see
`tools/PakTool.ps1` (list + extract). Round-trip verified against the reference
extraction: Scripts.pak 1187/1187, Sounds.pak 3361/3361, Levels.pak 30671/30671
files identical by MD5, 0 failures.

```
Header:
  u8    version          # 0x01 = zlib-compressed entries; 0x00 = stored (e.g. Sounds.pak)
  u32   dirOffset        # absolute offset of the directory (at end of file)

Directory @ dirOffset:
  u32   entryCount
  entry[entryCount]:
    u32    nameLen
    u8     name[nameLen]  # OBFUSCATED, see below
    u32    dataOffset     # absolute offset of this file's data
    u32    uncompressedSize
    u32    compressedSize  # 0 => directory marker (name ends in '/')

Data @ dataOffset:
  if compressedSize == uncompressedSize and not zlib-headed -> stored (raw copy)
  else -> zlib stream (2-byte header 0x78 .. + raw DEFLATE)
```

### Name obfuscation
Per-entry arithmetic keystream over the filename bytes:

```
decoded[j] = encoded[j] XOR ((k0 + 2*j) & 0xFF)
```

`k0` is a per-entry seed (it drifts roughly with entry index but the exact
generator is not yet pinned down — see Open Questions). It doesn't need to be:
names are ASCII paths, so each name is recovered by trying all 256 seeds and
scoring for path-likeness (`/`, known extensions, alnum ratio), with two
neighbour tiebreaks: directory coherence, and the fact that **the directory is
stored in lexicographic order** — a candidate that keeps `prev <= name <= next`
wins true ties (e.g. `ntu.vso` vs `fog.vso`: same length, same letter count,
both known extensions). Recovers **65,628/65,628 file names across all ten
shipped archives**, validated against a full reference extraction.

Two hard-won decoder rules (each silently corrupted names before it was fixed):
the known-extension list must be COMPLETE (a censused list of all 61 shipped
extensions lives in the decoder — `.rde` being absent let a junk decode win for
`beast.rde`, and the coherence pass then propagated its garbage directory onto
`beast.pkmdl`, silently dropping models); and only a directory made of ordinary
path characters may hand out the coherence bonus, so two junk neighbours cannot
vote each other past the correct names. Implemented twice, in agreement:
`tools/PakTool.ps1` and the engine's `Source/Core/PakArchive.cpp`.

Decoded names are relative paths with `/` separators, e.g. `Decals/blood.ini`,
`C1L1_Cathedral/C1L1_Cathedral.CLevel`, `actor/alastor/alastor-fly-fire.wav`.

### Numbered patch layering (observed in exe string table)
For each category the engine mounts, in priority order:
`<name>2.pak`, `<name>1.pak`, `<name>.pak`, then the loose `<name>/` directory.
The numbered variants are how the official patches (e.g. 1.64) override base assets
without rewriting the originals — `Textures2.pak` shadows `Textures.pak`, etc.

## 3b. Geometry & asset formats

### `.mpk` — world mesh — SOLVED
Produced from 3ds Max ASE by `Exporters/3DS MAX ase2mpk Exporter/ase2mpk.exe`
(or the Maya plug-ins). Parser: `tools/MpkTool.ps1` (list / OBJ export),
`tools/MpkPreview.ps1` (PNG render). **Validated across all 85 shipped maps:
85/85 parse cleanly — 28,816 objects, 9,159,934 triangles**, every file ending
exactly on its terminator with no out-of-range indices.

```
u32   magic = 0xDEAFBABE
objects[] until terminator:
  u32   nameLen                    (includes NUL)
  char  name[nameLen]              (substrings drive engine behaviour, below)
  f32   matrix[16]                 (4x4 node transform)
  u32   uvChannels                 (1 = dynamically lit, 2 = lightmapped)
  u32   vertexCount
  vertex[vertexCount]              ALWAYS 32 bytes; middle floats depend on uvChannels:
      uvChannels == 1:  pos[3], normal[3], uv[2]
      uvChannels == 2:  pos[3], pad[1]=0, uv[2], lightmapUv[2]
  u32   normalCount                (0 if normals were inline, else == vertexCount)
  f32   normals[normalCount][3]
  f32   bboxMin[3], bboxMax[3]
  u32   indexCount
  u16   indices[indexCount]        (triangle list)
  ...   material block (variable; length-prefixed texture names)
u32   terminator = 0xDEADBEEF
```

Everything is **byte-packed with no alignment padding** — an odd-length name
leaves every following field unaligned, so read sequentially, never by offset.
Lightmapped geometry moves normals into a separate array (lighting is baked, so
the inline slot is free), keeping the vertex record 32 bytes in both cases.

Zero-length normals appear in some shipped meshes (61 in one sky map). These are
degenerate vertices in the source art, not decode errors.

### `.mpk` material block — SOLVED
```
u32   materialCount
material[materialCount]:
  u16   firstIndex           into this object's index array
  u16   triangleCount
  slot[4]:                   ALWAYS four slots
    u32 nameLen              includes the NUL; 1 means an empty slot
    char name[nameLen]
    f32 offsetU, offsetV     texture transform
    f32 scaleU,  scaleV
```
Slot 0 is the diffuse map. On lightmapped geometry slot 1 is the **lightmap**,
named after the mesh plus the lightmap suffix (`_L_0000` by default — exactly as
documented in the `ase2mpk` readme). Slots 2 and 3 are almost always empty.

Materials tile the index array: each covers a contiguous run, and consecutive
materials continue where the previous ended. **Validated across all 85 maps —
69,794 materials parsed with zero triangle-coverage mismatches and zero
non-contiguous runs.** `1x01_Chaos` alone has 1,688 materials over 564 objects
using 81 distinct textures.

A few per-object bytes after the last material are still unmapped, but they do not
affect parsing — the next object is located by header scan.

**Object names carry semantics** (from `Docs/Editor Docs/Pain Engine - MPK
Substrings.pdf`): `portal`, `antyp` (antiportal), `trans`, `water`, `noclip`
(excluded from physics), `2sided`, `atest`, `decal`, `barrier`, `monster`,
`vollight`, `volfog`, `zone` (+`death`, `ladderzone`), `glass`, and the physics
set `statdest` / `physdest` / `phys` (+`autodelete`, `pinned`). A reimplementation
must reproduce this name-driven behaviour — it is the level's "entity" system.

### `.pkmdl` — models — SOLVED (geometry + skeleton)
Parser: `tools/PkmdlTool.ps1` (list / OBJ export), `tools/PkmdlPreview.ps1` (PNG),
`tools/PosePreview.ps1` (skinned + animated PNG).
**367 of 382 shipped models parse cleanly — 2,532 meshes, 1,044,010 triangles**,
with zero non-unit normals, zero out-of-range UVs, zero bad skin weight sums and
zero out-of-range bone indices. The 14 that yield no geometry are small bone-only
helper models (`splash`, `teleport`, `wind*`, `wybuch`) that legitimately contain
no mesh.

`.pkmdl` is a **serialised engine object graph** — the class name is stored inline
(`AnimatedMesh`) and resolved through the engine's `AbstractFactory<Object,String>`.
All strings are length-prefixed *including* the NUL.

```
u32       version                 (380 shipped models are v3, 2 are v4)
string    name
string    source path             (original authoring path; absent in some models)
string    class name              ("AnimatedMesh")
...       a few u32 fields, source path sometimes repeated
u32       boneCount
bone[boneCount]:
  string  name                    ("joint1", or Polish anatomy names, below)
  f32     matrix[16]              4x4 affine, row-major, translation in row 3;
                                  upper 3x3 verified orthonormal
  u8      flag
mesh[]    each preceded by its material header:
  string  meshName
  u32 x3                          "lead" - usually 12 bytes, all zero
  u32     materialCount
  material[materialCount]:
    string textureName
    u32, u32                      separator pair BETWEEN materials only
                                  (absent after the last one)
  then the geometry:
  u32     indexCount
  u16     indices[indexCount]
  u32     0
  u32     vertexCount
  vertex[vertexCount]             32 bytes: pos[3], normal[3], uv[2]
                                  (identical to .mpk's uvChannels==1 layout)
  u32     0, u32 0, u32 vertexCount        (skin block header)
  skin[vertexCount]:              VARIABLE length, 4 + 6*count bytes
    u32   influenceCount
    influence[influenceCount]:
      u16 boneIndex
      f32 weight                  weights sum to 1.0
```

The skin record is **variable length, not fixed**. It only looks like a fixed
10-byte record on rigidly bound models (`influenceCount == 1`), which is what
made the first reading wrong. Verified across the set: **0 bad weight sums and 0
out-of-range bone indices** in 337 fully skinned models; up to 8 influences on a
single vertex.

Four traps that cost time here, all worth remembering:
- **Mesh discovery must not depend on the skin block.** Skin records are variable
  length; advancing the scan past a mis-read skin block silently swallows the
  next mesh instead of failing loudly.
- **Do not jump the scan forward on string matches.** A spurious length-prefixed
  string can leap over an entire vertex block. Discover geometry with a pure
  byte scan first, then collect names from the gaps between blocks.
  These two bugs together were dropping ~209,000 triangles across the set,
  including whole character bodies (the clown's torso, arms and legs).
- The fields *preceding* the index array vary between models, so anchoring the
  scan there fails on ~14% of files. **Anchor on the vertex block instead**
  (`[u32 0][u32 vertexCount][32-byte vertices]`, validated by unit-length
  normals) and recover the index array backwards by solving for the `N` where
  `u32 at (blockStart - 2N - 4) == N`.
- Not every model has the source-path field, so header parsing must be tolerant
  and must never skip the geometry scan when a header field is missing.

### `.pkmdl` material header — SOLVED
**2,525 of 2,532 meshes (99.7%) across all 382 models parse exactly**, yielding
3,720 materials that reference 477 distinct textures. Unlike `.mpk` materials these
carry only a texture name — no triangle range and no UV transform — so a mesh's
materials apply in order rather than to explicit index ranges.

The header is found by a **self-validating search**: every candidate offset in the
gap before the geometry is tried, against a small set of layout variants (lead
12/8/16/4/0 bytes, separator 8/0/4/12/16), and only a parse that lands *exactly* on
the geometry header is accepted. A wrong guess is rejected rather than silently
believed. Most models use lead 12 with an 8-byte separator.

That search also recovers mesh names a forward string scan misses, since a spurious
length-prefixed string can skip past the real name.

Bone names are frequently Polish anatomy: `k_glowa` (head), `k_szyja` (neck),
`k_szczeka` (jaw), `k_ogo[n]` (tail), `k_zebra` (ribs), `k_ramiona` (shoulders).
**Cross-validation:** `LScripts/Templates/Monsters/Alastor/Alastor.CActor` sets
`rotateHeadBone = "k_szyja"`, and `k_szyja` is present among the 99 bones parsed
from `Alastor.pkmdl` — the Lua content layer and the binary model format agree.

### `.ani` — skeletal animation — SOLVED
Parser: `tools/AniTool.ps1` (list / CSV export), `tools/AniPreview.ps1` (renders
posed skeletons). **All 1,228 shipped animations parse exactly — 2,120,173
keyframes**, every file consumed to the byte with matching bone counts, no
non-orthonormal rotations and no time regressions.

```
char[4]  "skel"
f32      frameTime           seconds per frame (0.1 / 0.125 observed)
u32      boneCount
bone[boneCount]:
  u32    nameLen             NOTE: does NOT include a NUL, unlike .pkmdl
  char   name[nameLen]       matches the bone names in the matching .pkmdl
  u32    keyCount            per-bone, so tracks are variable length
  key[keyCount]:             68 bytes each
    f32  time                seconds; 0, 1/24, 2/24, ... at 24 fps
    f32  matrix[16]          4x4 affine pose, row-major, PARENT-RELATIVE
```

Track lengths vary per bone, which is why file sizes never factor into
`boneCount * frames * stride`.

Some rigs store **full Maya DAG paths** as bone names
(`joint1|joint2|...|joint17`), well over 100 characters — a name-length cap of
128 silently truncates parsing on those files. The same cap applies to `.pkmdl`.

**Cross-validation:** animation bone names match the model skeleton exactly —
Alastor 99/99, clown 39/39, Giant 90/90 (a subset of its 131 bones).

### Skeleton hierarchy — SOLVED
`.ani` matrices are parent-relative, so posing needs the hierarchy. It is stored
in the **`.pkmdl` bone table**: bones are written in **preorder (depth-first)
order**, and the trailing `u8` of each bone record is that bone's **child
count**. Walking the list with a stack recovers parent links exactly.

```
world = local * parentWorld      (row-vector convention, translation in row 3)
```

Verified anatomically on `clown` (39 bones, 1 root, max depth 10): `k_ramiona`
(shoulders) has 3 children, `k_szyja` (neck) has 5 (head plus four eyelid bones),
and jaw/eyelid bones are leaves with 0. Rendering the composed world transforms
produces a recognisable walking humanoid — see `exports/clown_walk_skeleton.png`.

### Other formats identified
| Ext | Count | What it is |
|---|---|---|
| `.mpk` | 85 | World mesh (above) |
| `.rde` | 220 | **Plain-text INI ragdoll definitions** — per joint: `Mass`, `LinearDamping`, `AngularDamping`, `Friction`, `Restitution`. Maps almost 1:1 onto Jolt's `BodyCreationSettings`. |
| `.wps` | 29 | AI waypoint set: `u32 count` then per-waypoint position + link data. Matches the `WaypointSet` class (78 methods). |
| `.mopp` | 17,643 | **Havok** MOPP collision acceleration data, in `Data/Maps/MOPPCode/<map>.mpk/`. Derived from mesh geometry — a replacement physics backend regenerates its own and ignores these entirely. |

## 3c. Physics: replacing Havok (assessment)

Havok 2.x (string `havok2`) is **statically linked into `Engine.dll`** — there is
no separate Havok DLL, so there is no seam to intercept. **The shipped binary
cannot practically be made to use a different physics engine.** The question only
becomes tractable in a reimplementation.

For a reimplementation the outlook is good, because PCF already wrote the
abstraction layer:
- Havok bodies cross the engine boundary as **opaque `void*`**
  (`PhysicsObject::GetHavokBody`, `PhysicsWorld::SetHavokBodyPosition(void*, Vector)`).
  No Havok type appears in PainEngine's own API.
- Everything is expressed in **engine types** (`Vector`, `Quaternion`, `WorldMesh`,
  `Model`), so `PhysicsWorld` (83 methods) + `PhysicsObject` (127) + `Ragdoll` (60)
  is a ready-made porting specification.
- Collision is **built at load time from engine geometry**
  (`CreatePhysicsObjectFromMesh(..., WorldMesh*, ...)`,
  `CreatePhysicsObjectFromRagdoll(..., Model*, ...)`), not deserialised from Havok.
  There are no Havok serialisation strings in the DLL.
- Ragdoll tuning is already human-readable in `.rde`.

**Chosen target: Jolt Physics (MIT).** Bullet (zlib) is the fallback if closer
2004-era Havok semantics matter more than modern tooling.

Caveat: `PhysicsObject::FixHavokPositionBug` shows gameplay behaviour was tuned
around Havok's specific quirks, so expect ragdoll *feel* to differ and need
retuning — this is not a mechanical swap.

## 4. Modding / override mechanism (track C)

From the exe string/mount table (`Painkiller.exe`):
- Every data category mounts BOTH a `.pak` and a loose `../data/<cat>/` directory
  (which is why retail ships loose `Data/Models`, `Data/Movies`, `Data/Music`).
- `../Data/*.pkm` is glob-mounted — `.pkm` is the community mod-package container.
- `FS.ExtractPack('../Data/Textures2.pak','../Data/Textures')` — a Lua-callable
  filesystem API can unpack a `.pak` to its loose dir.
- Command-line switches present in the exe: `-lscripts` (force loose scripts),
  `-script`, `-cfg`, `-dedicated`, `-game editorgame`, `-start`, `-inf`.

**Recommended mod loop (fastest, no repacking):**
1. Copy the files you want to change from `Data_Extracted/LScripts/...` into a loose
   `Data/LScripts/...` tree (mirror the paths).
2. Launch `Painkiller.exe -lscripts` so loose scripts are used.
3. Edit the loose `.lua` / `.CActor` / `.CWeapon` files and relaunch.

Good first edits to confirm the loop works: `LScripts/Templates/Monsters/Alastor/
Alastor.CActor` (`o.Health = 444`), or a weapon's damage under `LScripts/Templates/
Weapons/`. Config lives in `Bin/config.ini` (`Cfg.*`).

> NOT YET EMPIRICALLY CONFIRMED: the exact precedence between a loose dir and the
> paks (does loose always win, or only when the pak lacks the file?). The mount
> table proves loose dirs and `-lscripts` exist; confirm by testing one edited file.
> Content redistribution: ship mods as loose files / `.pkm` diffs, never repackaged
> original assets.

## 5. Files in this RE/ folder

- `Engine_exports.txt` — all 2867 demangled Engine.dll exports (sorted)
- `EngineEditor_exports.txt` — editor build's exports (diff vs above = editor hooks)
- `D3Dev_exports.txt` — renderer boundary (2 functions)
- `Engine_API.md` — exports grouped by class (112 classes) — the C++ API reference
- `Engine_LuaAPI.md` — **941 Lua natives + addresses, grouped by module**
- `EngineEditor_LuaAPI.md` — same for the editor build (950; +9 Havok debug)
- `tools/PakTool.ps1` — **list + extract any .pak** (validated)
- `tools/MpkTool.ps1` — **parse .mpk world meshes**, list objects, export OBJ
  (validated on all 85 maps)
- `tools/MpkPreview.ps1` — render a level to PNG straight from parsed geometry
- `tools/PkmdlTool.ps1` — **parse .pkmdl models**, list bones/meshes, export OBJ
  (353/382 models validated)
- `tools/PkmdlPreview.ps1` — render a model to PNG from parsed geometry
- `tools/AniTool.ps1` — **parse .ani animations**, list tracks, export keys to CSV
  (all 1,228 animations validated)
- `tools/AniPreview.ps1` — rebuild the skeleton hierarchy and render posed frames
- `exports/` — sample output: `DM_Trainstation.obj` (22 MB, opens in Blender),
  `skylow.obj`, `DM_Trainstation_plan.png`, `Alastor.obj`, `Alastor_front.png`
- `tools/LuaBridge.ps1` — **recovers the Lua API from a binary** (no disassembler)
- `tools/ghidra_PainEngineLuaAPI.py` — same recovery as a Ghidra script; renames
  every native to `Lua_<name>` and plate-comments how it was found
- `tools/DumpExports.ps1` — PE export dumper + MSVC demangler (dbghelp)
- `tools/GroupExports.ps1` — builds `Engine_API.md`
- `tools/PakDir.ps1`, `PakKey.ps1`, `PakKey2.ps1` — format-analysis scratch

### Using the Ghidra script
1. Install Ghidra (free, from NSA/ghidra-sre.org) and create a project.
2. Import `Bin/Engine.dll` and run auto-analysis (the export table alone names
   ~2867 functions).
3. Window -> Script Manager -> Manage Script Directories -> add `RE/tools`.
4. Run `ghidra_PainEngineLuaAPI.py`. It renames ~941 functions to `Lua_<name>`.
5. Optionally run Ghidra's `RecoverClassesFromRTTIScript` for the non-exported
   class layout.

Combined, steps 2 and 4 give named C++ methods *and* named Lua entry points, which
is most of what makes this binary readable.

## 5a. Textures

The shipped texture tree is **7,649 `.dds`**, 478 `.tga`, 135 `.bmp`. Crucially,
**texture references do not match the files on disk**: a model asks for
`Models/ALASTORtexture1.tga` while the shipped file is `Models/alastortexture1.dds`,
and `.mpk` materials store bare names with no extension at all (`sciana`, `paski`).

So resolution must be **extension-agnostic and case-insensitive**, keyed on the
path without its extension, with map textures additionally looked up under
`Textures/Levels/<mapName>/`. `TextureResolver` in PainKit does this and indexes
14,768 entries across the tree.

PainKit includes a small **DDS reader** (DXT1/BC1, DXT3/BC2, DXT5/BC3 and
uncompressed 24/32-bit) and a dependency-free **PNG writer**, so textures can be
decoded for export or preview without external libraries.

## 5b. PainKit — portable asset library

`PainKit/` is a .NET 9 library + CLI that consolidates every decoded format into
reusable code (the PowerShell tools remain as the exploration record). It reads
`.pak`, `.mpk`, `.pkmdl`, `.ani`, rebuilds skeleton hierarchies, and exports
**skinned, animated glTF 2.0**.

The exporter is verified rather than assumed: `painkit verify` re-reads the written
glTF using glTF's *own* conventions (column-major, column-vector,
`globalJointTransform * inverseBindMatrix`) and compares the skinned vertex
positions against the native pipeline. Across the shipped characters the two
independent maths paths agree to ~5e-6 on models tens of units across.

Note on convention: PainEngine stores matrices row-major/row-vector; glTF is
column-major/column-vector. Since one is the transpose of the other, **the 16 floats
transfer verbatim with no transposition** — a useful and non-obvious result.

glTF caps skin influences at 4 per vertex while PainEngine allows up to 8; the
exporter keeps the 4 heaviest and renormalises.

See [SOURCE_PORT.md](SOURCE_PORT.md) for the port architecture and roadmap, and
[native_priority.tsv](native_priority.tsv) for the call-frequency-ranked native API
work queue (**113 natives cover 80% of all 14,387 script call sites**).

## 6. Recommended next steps

- **Static analysis:** install Ghidra and run the two scripts above (see
  "Using the Ghidra script"). `RegisterFunction` is at `0x10146760` in the retail
  `Engine.dll` if you want to inspect the registrar itself.
- **Signature the Lua natives:** with names applied, the next win is recovering
  each native's *arguments* — every one is `int f(lua_State*)`, so the real
  signature is in its `lua_toXXX`/`luaL_checkXXX` calls. That turns the name list
  into a documented scripting API.
- **Dynamic:** x32dbg (breakpoints are trivial with named exports), Cheat Engine
  for live object layouts. Diff retail vs editor `Engine.dll` for editor-only hooks.
- **A glTF/FBX exporter is now unblocked.** Geometry, skeletons, hierarchy, skin
  weights and animation are all decoded and cross-validated, so every input a
  skinned-animated exporter needs is available. `tools/PosePreview.ps1` already
  performs the full skinning maths end to end.
- **Formats to crack next:** the `.mpk`/`.pkmdl` **material blocks** (texture
  assignment on export - geometry currently exports untextured) and `.pfx`
  particles.
- The `.pkmdl` geometry header preceding the index array is not laid out
  identically in every model, so `FindIndices` uses a strict match first and
  falls back to a looser one. Fully mapping that header would remove the
  heuristic entirely.
- **Tooling:** add repack to `PakTool.ps1` once the `k0` generator is pinned, for
  building `<name>2.pak` overrides the way the official patches do.

## Open questions
- The ~11 small unidentified `luaL_reg` tables (1–8 functions each) in
  `Engine_LuaAPI.md` — worth naming to complete the module map.
- 158 of the 941 recovered names are never called by the shipped scripts. Dead
  API, debug-only, or used by content not in this install? Some may be interesting.
- Exact `k0` seed generator (per-entry). Brute force sidesteps it for extraction,
  but repacking to a byte-identical archive needs the real formula. Leads: `k0`
  correlates loosely with entry index; `r = (k0 - 2*(nl+1)) & 0xFF` tracks the index
  with noise — likely a running counter or an FIdx-derived value.
- Loose-dir vs pak precedence (see section 4 caveat).
- `.pkm` internal format (same as `.pak`? a zip? — `GZipPack::GetFile` exists,
  suggesting the engine also supports real ZIP archives).
