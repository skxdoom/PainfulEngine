# Sound

The easiest system in the game to bring up, and it was worth doing early: the
scripts have been calling into it constantly and hearing nothing, and a player
who cannot hear a footfall cannot tell you whether the animation event that
fires it worked.

## What the data is

**3222 samples, every one plain uncompressed PCM in a `.wav`** - mostly mono at
22 kHz, some stereo, some 44.1 kHz, one at 8 kHz. No codec to reverse, no
container to decode. `SDL_LoadWAV` reads all of them and `SDL_ConvertAudioSamples`
normalises them to the device format once, at load.

Mono matters: a mono sample can be panned, a stereo one cannot meaningfully be,
and the overwhelming majority of the game's positional audio is mono.

The music is a separate matter and **does not ship in the extracted data** -
`C1L1_Cathedral_Music_01` and friends are named by every level's `.CLevel` and
exist nowhere. `SOUND.Stream*` therefore has nothing to load. Left alone.

## The script surface, and how it splits

Four families, divided by how the scripts HOLD them rather than by how they
sound:

| family | shape | example |
|---|---|---|
| `SOUND.Play2D` / `Play3D` | fire and forget | a gunshot, a footfall |
| `SOUND2D.*` | a handle, driven | the bullet-time loop |
| `SOUND3D.*` | a handle, at a position | a flamethrower, an elevator |
| `SND.*` | an ENTITY, which carries the voice | a flying PainHead's rotor loop |

The fourth is addressed differently from the other three and that is the whole
distinction: `SOUND2D`/`SOUND3D` take a **voice**, `SND` takes an **entity**.
A script makes a Sound entity, hangs it off its owner with
`ENTITY.RegisterChild`, describes it with `SND.Setup3D` and starts it with
`SND.Play`; the sound then follows the thing it belongs to for as long as it
lives. `BindSoundToEntity` in `Utils.lua` is that sequence, and 23 call sites
use it. `SND.GetSound3DPtr` bridges the two: it hands back the voice, so a
script can drive an entity's sound with the voice API afterwards.

`SND.Setup3D(entity, name, dist1=10, dist2=20, interval=-1, ?, dontAutoDelete)`
— argument order and every default is from `0x10139620`, which reads them
backwards (`GetBool(7)`, `GetFloat(6,10)`, `GetFloat(5,-1)`, `GetFloat(4,20)`,
`GetFloat(3,10)`, `GetString(2)`) before calling `Sound::Setup3D`. `interval`
is the gap between repeats, and `BindSoundToEntity` passes **0 to loop** and
**−1 for a one-shot** — the same polarity as the loop count below.

Only `Setup3D`, `GetSound3DPtr` and `SetVelocityScaleFactor` were recovered
from that registration table (`0x102C28C8`), whose module name was never
identified. The scripts also call `Play`, `Stop` and `IsPlaying` on the same
entity handles, so those are implemented to the contract the scripts use
rather than to a recovered address.

`CObject:GetSndInfo` supplies the parameters from a template's `.soundsDef`:
the sample name, **volume as 0..100**, and `dist1`/`dist2` defaulting to 15 and
40 - where attenuation begins and where it reaches silence. Those are per
sound, which is why falloff cannot be one global curve.

## Loop counts are Miles' counts, and 0 means forever

`SetLoopCount(voice, n)`: **0 loops forever, 1 plays once, n plays n times.**
That is Miles Sound System's convention (`AIL_set_sample_loop_count`), and the
native does nothing but forward it — `0x10125ce0` reads the argument with a
default of 0 and hands it straight to `MilesEngine::Sound3D_SetLoopCount`.

The scripts confirm it from both directions. Of the 24 call sites passing 0,
every one is a sustained loop: `_sndRotor`, `_loopSnd`, `_rain`, `_sndElectro`.
And 12 pass **1** to a sound that is already looping — the idiom for "let this
pass finish, then stop", which only makes sense if 1 means once.

`AudioEngine` counts down internally, so forever is −1 there and 0 would mean
"play once". The native translates at the boundary. Getting this backwards
silences exactly the sounds the scripts loop most.


## The lifetime bug worth remembering

The first version gave every sound a held slot. Ninety-six one-shots later
nothing could play at all, and the symptom was a diagnostic reading `31 voices
playing` that was really thirty-one FINISHED sounds nobody had freed.

Two corrections came out of it:

**A handle and a mixing slot are not the same resource.** The scripts create a
sound long before playing it and hold that handle for the life of the entity -
Cathedral sits at ~135 held handles, none of them audible. Those cost nothing
to mix, so the slot table is generous (512) while what actually costs
something, sounds being mixed this instant, is capped separately at 64.

**`Play2D` / `Play3D` are fire and forget.** Almost every caller drops the
handle without telling us, so those voices must not be held; they free
themselves when they finish. The few callers that do keep the handle still
work - it simply stops resolving once the sound ends, which is exactly what
they are asking `IsPlaying` about.

Handles carry a **generation** in their high bits, so a kept handle whose slot
has since been reused answers "not playing" rather than answering about
whatever took the slot.

**The instantaneous voice count is a bad diagnostic** - a hundred one-shots can
start and finish between two samples of it. Started and reaped, cumulative, are
what show whether the system is alive. Cathedral over 1800 frames:

```
frame  300 | 7 playing, 162 started,  20 reaped, 0 missing
frame  900 | 7 playing, 210 started,  68 reaped, 0 missing
frame 1800 | 3 playing, 282 started, 143 reaped, 0 missing
```

Started climbs, reaped tracks it, held stays flat. **0 missing** across every
sample the game has named so far.

## Design

One device stream at 44.1 kHz stereo float, mixed here rather than by SDL - SDL
will open a stream per sound quite happily, but then nothing caps how many play
at once and a Painkiller fight is a lot of sounds at once.

The audio callback runs on SDL's thread and does nothing but add samples: all
of the loading, the distance maths and the reaping happen on the game thread in
`Update()`, once a frame. Panning is constant-power off the listener's right
vector, so a sound crossing in front does not dip in the middle.

`SOUND.SetPlayerPos` / `SetPlayerOrientation` give the listener. The scripts
hand over a forward vector only, so the right vector is derived from it.

## Not done

- **Music.** No files ship; `SOUND.Stream*` is inert.
- **`SOUND.SetRoomType`** - reverb zones. The scripts set them per level and
  per area, and it is currently ignored.
- **`SOUND.Set3DSoundFalloff`**, a global falloff scale.
- **Doppler**, if the original has it at all - not investigated.
- **`SND.SetVelocityScaleFactor`** (6 call sites), the per-entity doppler scale.
- **`Setup3D`'s `dontAutoDelete`** is stored and not acted on: nothing auto-
  deletes a finished bound sound, which leaks an entity rather than removing
  one early.

## The bug that made it silent, and why testing hid it

Everything above measured perfectly and the game played nothing. The cause:

**`SDL_LoadWAV` opens a FILESYSTEM path, and the shipped game reads its data
out of `.pak` archives.** Every sound went missing, silently, because a missing
sample is a normal answer here and is only counted.

What hid it is worth more than the bug. The engine takes a data root, and there
are two of them:

| root | what it is | sound |
|---|---|---|
| `Data_Extracted` | loose files, used for every diagnostic | worked |
| `Data` | the `.pak` archives, what `Bin/PainfulEngine.exe` resolves to | silent |

Every test above ran against the loose root, so the loader looked flawless -
device open, full-scale samples reaching SDL, `0 missing`. The deployed copy
next to the game runs against the packed root and could not read a single
sample. **A diagnostic that only ever runs against loose files cannot see this
class of bug at all.**

`Load` now reads through `painful::ReadFile`, the VFS that knows about the
archives, and hands the bytes to `SDL_LoadWAV_IO`. Measured against `Data`
afterwards: `5 playing, 184 started, 44 reaped, 0 missing`.

A second, smaller trap sat behind it: commands mount the archives from a
per-command table in `main.cpp`, and a new command that is not in that table
silently gets no VFS. `sound` had to be added to it.

**Run diagnostics against `Data` as well as `Data_Extracted` when the thing
being tested loads assets.**

## The second bug that made it silent: a handle read as an index

The first bug silenced everything. This one silenced only the sounds a script
**holds** — which is worse, because it looked selective and therefore looked
like a loop problem.

`AudioEngine::Open` returns a **packed handle**: the voice index in the low 16
bits, a generation counter in the high 16, so a stale handle cannot address a
slot that has since been reused. `Resolve` decodes it. The setters did not —
their guard treated the handle as a bare 1-based index:

```cpp
if (size_t(v) > voices_.size()) return;
Playing& p = voices_[size_t(v) - 1];
```

Voice 2 of generation 2 is `0x20002` = 131074, which fails a bounds check
against 512 voices and returns. So `Start`, `Stop`, `Pause`, `SetVolume`,
`SetPosition`, `SetHearingDistance`, `SetLoopCount`, `SetSpeed` and `Release`
were all silent no-ops for **anything `Create` handed out** — 32 `Create` sites
and 217 setter calls across the shipped scripts: rain, the flamethrower, the
electro loops, the minigun rotor, bullet-time.

The tell was which sounds survived. The Painkiller's rotor *start* and *stop*
played and its *loop* did not, because start and stop go through `SndEnt` into
fire-and-forget `Play2D`, which never touches a setter. Anything that reached
for a handle got nothing.

Two lessons, both cheap to reuse:

- **A packed handle and a raw index must not be interchangeable at a call
  site.** Both are `int` here, so the compiler had nothing to say.
- **When a subsystem works for some callers and not others, split the callers
  by the API they use rather than by what they sound like.** "Loops are broken"
  was the wrong hypothesis; "held voices are broken" was the right one, and the
  fire-and-forget/held split was already written down at the top of this page.

## Turning a stub on can expose bugs behind it

`SND.Play` was unimplemented, so nothing the scripts bound to an entity ever
made a sound. Implementing it immediately produced a stake that screamed from
the wall for the rest of the level — and that was not a new bug but two old
ones that had never been reachable:

- `ENTITY.UnregisterAllChildren(parent, type)` ignored its `ETypes` filter and
  dropped **every** child. `Stake:Tick` unregisters its Trail and then kills its
  flight loop by name on the very next line, so the list was already empty.
- `ENTITY.KillAllChildrenByName` then erased a child that `ReleaseEntity` had
  already unlinked — `erase(end())` on the last index, an access violation. It
  had simply never run with a non-empty list before.
- It also returned nothing, so `if KillAllChildrenByName(se,"stakeflame")` read
  false every time and a burning stake never smoked.

**Expect a stub's first real implementation to fail, and to fail in the code
around it rather than in itself.**
