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


## `SOUND.Play2D` never loops — its third argument is bullet-time

```lua
function PlaySound2D(sound, volume, sameSpeedInBulletTime, noRandomize)
    return SOUND.Play2D(sound, volume, sameSpeedInBulletTime, noRandomize)
```

`AudioEngine::Play2D` took that third argument as **loop**, so any caller
passing it true started a voice that never ended. Only one shipped call site
does — `Hud:RenderCompass`, the checkpoint heartbeat:

```lua
PlaySound2D("pickup_health_minisphere", vol, true, false)
```

which is a ONE-SHOT per pulse, gated by the glow cycle and a 1.5 s cooldown on
`_glowStart`. Every pulse opened another endless voice on top of the last, so
the heartbeat piled up until it was a wall of them. The other 11 call sites
that pass a `true` pass it as the FOURTH argument, `noRandomize`, and were
never affected — worth checking argument POSITION before assuming a shared
fault.

A looping 2D sound is not this call at all: it is `SOUND2D.Create(name, loop)`
plus `SOUND2D.Play`, which is how `_sndRotor` and `_sndElectro` are held.

`sameSpeedInBulletTime` is not modelled — nothing scales voice speed with the
game clock yet, so the flag is recorded and ignored. When bullet-time reaches
the mixer it says which voices keep their own rate.

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

## Pausing: the menu takes a token, it does not stop everything

`PainMenu::PauseSounds` (0x1004f730) is the whole rule:

```c
if ((*(int *)(this + 0x3a8) != -1) && (*(MilesEngine **)(GEngine + 0xd8) != 0))
    MilesEngine::ResumeSounds(*(MilesEngine **)(GEngine + 0xd8), *(int *)(this + 0x3a8));
iVar1 = (GEngine + 0xd8 == 0) ? -1 : MilesEngine::PauseCurrentlyPlayingSounds(...);
*(int *)(this + 0x3a8) = iVar1;      // the token lives on the menu
```

It resumes whatever set it is still holding, pauses everything **currently
playing**, and keeps the token naming that set (`PainMenu+0x3a8`, -1 when it
holds none). `PainMenu::ResumeSounds` is 0x1004f7c0;
`MilesEngine::PauseCurrentlyPlayingSounds` is 0x101f4df0 and
`MilesEngine::ResumeSounds` 0x101f5270. A separate pair exists for saving,
`SaveGame_PauseSounds` / `SaveGame_ResumeSounds` (0x101f54f0 / 0x101f5500),
which is why a token is kept rather than a single flag.

Two consequences, and both match what the game does:

- Sounds started *after* the pause are unaffected. The menu's own hover and
  click sounds are started after it, so they stay audible; a global mute would
  have silenced the menu too.
- Resume only touches the captured set. A sound a script had already paused
  itself stays paused, and a voice that ended while the menu was up is not
  resurrected.

The port mirrors this. `AudioEngine::PauseCurrentlyPlaying()` returns a token
and `ResumeSounds(token)` restores that set, both driven from
`ScriptEngine::SetGamePaused` - the one place the pause flag changes, so the
menu handler and `WORLD.SetGamePaused` cannot drift apart. Voices already
paused are left out of the set, for the reason above.

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

## Virtual voices: what the original actually mixes

Sources: `MilesEngine::Sound3D_Create/Play/Stop/Delete` (0x101f6260,
0x101f3a20, 0x101f33a0, 0x101f31c0), `TryToPlayRealSound` 0x101f43b0,
`Find3DSoundToStart` 0x101f0c90, `Find3DSoundToStop` 0x101f09a0,
`Start3DSample` 0x101f3ed0, `Tick` 0x101f49c0, `SetSoundProperties`
0x101f5f20 and its native 0x10140560, the ctor 0x101f7b40; `Definitions.lua`
`SoundsProperties`, `Game.lua:197`.

Every sound the scripts create is a LOGICAL sound (`Miles3DSound`, a 0xa0
record in a hash table). Only some hold a REAL Miles sample handle at any
moment, and that set is recomputed every tick:

- **Per file, two properties**: how many instances may be real at once
  (`MilesLoadedFile+0x48`) and the minimum gap between two starts
  (`+0x40`, ms). `SOUND.SetSoundProperties(name, max, ms)` sets them, with
  the name `"default"` setting the engine-wide fallback (`+0x2c0/+0x2c4`,
  100 and 0 from the constructor). `Game:Init` pushes Definitions.lua's
  `SoundsProperties` table through it: `{"default", 6, 0.1}` and 88 named
  files - raven wings 4 / 0.4 s, grenade explosion 3 / 0.4 s, the sado's
  shots 2 / 0.5 s. So no more than six instances of any one sample are ever
  audible, and none starts within 100 ms of the last.
- **A sound's score is `dist1 / distance`**, and a 3D sound further than
  `dist2` scores nothing. Priority is a byte on the sound (0x100 when the
  "privileged" flag is set) and ranks above score.
- **`TryToPlayRealSound`** gives a sound a handle when it is in range, its
  file's start interval has elapsed, and the file is under its cap - else it
  looks for the weakest real instance of the same file and takes that
  handle only when `weakest.score < score - 0.1`. Real handles come from
  Miles's own pool; the first allocation failure fixes the pool size
  (`+0x23c`, -1 until then).
- **`Tick`** first releases every real sound that has left its range, hands
  each freed handle to the best waiting sound, then keeps promoting the best
  waiting sound while the pool has room.
- A waiting sound is still "playing" to the scripts. A loop waits as long as
  it takes; a one-shot that waits past its own length just ends
  (0x101ee6e0). A demoted sound keeps its offset and resumes from there.
- **The policy clock is REAL time and runs while the game is paused.** Both
  rules above are clocked: a file's minimum start gap, and the length a waiting
  one-shot outlives. Freezing that clock with the simulation stops both, and a
  menu is exactly where it shows — the Options plates play a hover sound each,
  and with `now` stuck no waiting one-shot ever expires and the gap never
  elapses, so each hover is promoted only as the previous real voice ends. The
  result is the hovers queueing up and playing back in the order they were
  entered, one after another, instead of the later ones being dropped. Pause
  freezes the SIMULATION; `MilesEngine::Tick` is not part of it.

- **`Sound3D_Stop` does not free the record**; only `Sound3D_Delete` does.
  `FlameThrowerGas` creates one fire loop per burning patch, stops it on
  release and never deletes it - a leak the original has too, ten records a
  second while the flamethrower is held.

The port's `AudioEngine` is this model: a `Playing` is logical, `real` marks
the ones being mixed, `TryToPlayReal` / `Demote` / `Update` are the three
routines above, per-file properties live on the `Sample`, and the handle
table grows instead of refusing (a stopped held record costs nothing to mix).
The mixer's global budget is 64 real voices - the original's startup log
reports Miles at `DIG_MIXER_CHANNELS: 64`. Two approximations, both marked in
the code: 2D sounds always win (the original ranks them separately in
`TryToPlayRealSound2D`, not recovered), and the priority byte is not carried
(no shipped script passes one).

Why this matters in play: the flamethrower drops a burning patch every 0.1 s
and each starts its own `barrel-wood-fire-loop`; forty of them at random
phases is a wash, and the old mixer played every one. With the shipped
properties six play and the rest wait their turn.

### Testing it headlessly

`PAINFUL_AUDIO=1` makes the `lua` report open a real device. Without it
`audio_` is null, every SOUND native is a silent no-op, and the mixer cannot be
measured at all - which is why this went unnoticed: the report never exercised
the sound path.
