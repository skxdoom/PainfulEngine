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

Three families, divided by how the scripts HOLD them rather than by how they
sound:

| family | shape | example |
|---|---|---|
| `SOUND.Play2D` / `Play3D` | fire and forget | a gunshot, a footfall |
| `SOUND2D.*` | a handle, driven | the bullet-time loop |
| `SOUND3D.*` | a handle, at a position | a flamethrower, an elevator |

`CObject:GetSndInfo` supplies the parameters from a template's `.soundsDef`:
the sample name, **volume as 0..100**, and `dist1`/`dist2` defaulting to 15 and
40 - where attenuation begins and where it reaches silence. Those are per
sound, which is why falloff cannot be one global curve.

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
