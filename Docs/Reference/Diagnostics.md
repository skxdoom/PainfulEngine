# Diagnostics — checks, logging, truncated files, switches, layering

How the engine reports its own failures: which guards are checked, how a log
line is written, what a parser does with a file that ends early, where the
PAINFUL_* switches live, and how the layering is kept honest.

## Checks

`Source/Core/Check.h`. The engine's failure style is the defensive early
return — `if (!e) return;` — which is right for a script layer that asks
boneless props for joints all day, and wrong for a genuine invariant: the
frame keeps running and the symptom surfaces three subsystems later.

`PAINFUL_CHECK(cond, fmt, ...)` is that early return, made audible. It is an
expression returning the condition, so an existing guard converts in place:

```cpp
if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
                   "EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
    return;
```

A pass costs one branch. A failure logs **once per call site**, counts every
occurrence, and carries on. `ReportChecks()` prints the tally at the end of a
run; `LuaHost::PrintCallReport` and the game loop both call it, so
`PainfulTools lua` and a windowed session each end with either

```
checks: none failed
```

or one line per failed site with its count. That is what makes a silent
failure visible without a debugger attached.

The message is required. A check that cannot say what broke is not worth the
branch, and requiring it also keeps the macro off `__VA_OPT__`, which MSVC
offers only under `/Zc:preprocessor`.

`PAINFUL_ASSERT` is the same plus a debugger trap in Debug builds, for an
invariant whose violation means the *recovery* is wrong rather than the data
being odd. `PAINFUL_CHECK_BREAK=1` traps on the first failure of either, in
any configuration — the tally says a check fired, this says which frame did
it.

### What is a check and what is an ordinary answer

The distinction is the whole discipline, and getting it wrong floods the log
until nobody reads it.

| | |
|---|---|
| **Check it** | A slot index outside its container. A stale positive slot. A userdata of the wrong kind. A music slot outside the engine's fixed 0..15. |
| **Leave it silent** | A negative slot meaning "not attached" — headless runs carry `-1` everywhere. An absent entry in a lazily-grown sparse array: `AudioEngine::streams_` grows on `StreamLoad`, so slot 3 is legitimately out of range until slot 3 is loaded. A nil or missing script argument. A model with no skeleton, a prop with no ragdoll. |

`BillboardRenderer::SetupScriptCorona` shows both in one guard: `slot < 0` is
its allocate path and stays silent, while a stale positive slot is checked
rather than quietly re-allocated.

## Logging

`Source/Core/Log.h`. One file, `painful.log`, beside the executable; the tools
print to the console only. A crash still writes `painful_crash.log` of its own -
that is the file worth handing to someone else, and it is written by a handler
in a dying process.

There were three files: `painful.log` plus filtered copies of the script and
shader lines. Measured on a full Cathedral load plus 40 frames, the main log is
190 lines and **both copies held nothing but their timestamp header**. The
categories are line prefixes now and `grep` replaces the files.

| Emitter | Tag | Level |
|---|---|---|
| `LogWarn` | `warning: ` | warn |
| `LogScript` | `script: ` | warn — a script ERROR |
| `LogShader` | `shader: ` | warn |
| `LogInfo` | none | info |
| `LogTrace` | none | trace |

The scripts' own print output — `EDITOR.OutputText`, where `Game:Print` goes,
and the `Log()` native — is `LogInfo` with a `lua: ` tag, so a script error and
a script `print` do not read alike. `EDITOR.OutputText` used to hand-write
`script: ` itself, which would have collided with the error tag exactly the way
`PAINFUL_HIDDEN` once meant two things.

`PAINFUL_LOG=warn|info|trace` sets the level; the default is `trace`. Measured
on the same run: 190 lines at trace, 29 at info, 3 at warn. The 161 lines the
first step drops are the `[stub]` instrumentation, which is why they are
`LogTrace` and why trace is the default — `Docs/Plan.md` measures the remaining
native work by counting them, so quiet-by-default would have thrown away the
progress bar.

`Log` reads `PAINFUL_LOG` with a raw `getenv` rather than through
`Core/Debug`, for the same reason `PAINFUL_CHECK_BREAK` does: `Debug` reports a
bad switch through `Check`, and `Check` logs. Both are listed in the switch
table so `PainfulTools traces` shows them.

The emitters are C variadic rather than variadic templates, so the format string
carries `__attribute__((format(printf, ...)))` on GCC and Clang and
`_Printf_format_string_` on MSVC. A template hides the format from both, which
is how `"%s"` with an int compiles silently. One line is capped at 2048 bytes
and truncated rather than wrapped; anything longer is a dump, and a dump belongs
in a report.
## Truncated files

`Reader` (`Source/Core/Reader.h`) bounds-checks every read. Past the end it
yields zero and latches `overran()`.

The latch, rather than a check per field, is what suits these formats: a
count read out of a truncated file can say half a million, and the loop that
trusts it walks off the buffer. `MapMesh::Load` validated its header through
the vertex block and then read `ncount * 3` floats, six bbox floats and the
index count with no guard at all — on a cut file that is megabytes past the
end. With the latch, a parser checks once where it already decides success:

```cpp
if (r.overran()) { out.error = "truncated record: " + o.name; break; }
```

`Mpk`, `Ani`, `Dat` and `Waypoints` all do. Note that `pos()` still advances
on a failed read, so an existing bounds test on `pos()` often fires first —
in `MapMesh::Load` the index-overflow guard usually names the failure before
the `overran()` net does. Either way no read leaves the buffer.

Measured: 110 adversarial parses of `1x01_Chaos.mpk` (19.7 MB) — 80 random
truncations and 30 rounds of 128 corrupted bytes — plus a 352-point dense
truncation sweep of `TestFloor.mpk`, with no fault. Every one reported an
error and stopped.


## The PAINFUL_* switches

`Source/Core/Debug.h`, and the table itself is `Debug.cpp`. `PainfulTools
traces` prints every switch, its kind, its help and what it is set to now; a
run that has any of them on logs one `switches:` line at startup, so a log
explains its own odd behaviour.

They were 40 scattered `getenv` calls. Three things were wrong with that: the
set was undiscoverable, the caching was inconsistent (`ScriptMonster` read one
per tick), and a typo read as "off" with no complaint. The accessors are
`DebugFlag` / `DebugInt` / `DebugFloat` / `DebugText`; a name absent from the
table, or read at the wrong kind, is a check failure rather than a silent
default. The environment is read once on first access, which is what every
call site already assumed by caching in a `static const`.

Two disagreements the table forced into the open:

- **`PAINFUL_HIDDEN` meant two different things.** `GameApp` treated it as
  presence, `Window` as `set and not "0"`, so `PAINFUL_HIDDEN=0` silenced the
  audio but still showed the window. It is presence everywhere now.
- **`PAINFUL_WINDOWED` genuinely is value-carrying** — `=0` means *off*, so it
  stays `text` rather than becoming a flag, which would have inverted it.

`PAINFUL_CHECK_BREAK` is in the table for listing but is read directly in
`Check.cpp`: the check facility cannot call the switch table while the switch
table reports its own errors through the check facility.

## Layering

`CMake/Layering.cmake` checks, at configure time, that no layer includes one
above it.

The CMake targets enforce the layering at **link** time only, and that is
weaker than it looks: an upward `#include "../Render/Foo.h"` resolves relative
to the including file and never consults the target's include directories, so
a header-only or inline use compiles and links clean. `Zones.cpp` reached into
`Render/Frustum.h` that way for as long as `Frustum` lived there — which is
why `Frustum` is now in `Core`, where a pure-maths type that `World` needs
belongs.

The check runs on reconfigure. Adding a source file requires a `CMakeLists.txt`
edit in this project, so a new file always triggers it; adding an include to
an existing file is caught at the next reconfigure rather than immediately.
