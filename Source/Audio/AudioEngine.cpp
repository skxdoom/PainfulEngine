#include "AudioEngine.h"

#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace painful {

namespace {

// Two different limits, because a handle and a mixing slot are not the same
// thing. The scripts CREATE a sound long before they play it - a flamethrower
// loop, an elevator, a torch - and hold that handle for the life of the
// entity. Cathedral alone holds ninety-odd at once, none of them audible.
// Those cost nothing to mix, so the slot table starts generous and grows...
constexpr size_t kMaxVoices = 512;
// ...while what actually costs something, the REAL voices being mixed this
// instant, is capped at what the original's Miles mixer reports
// (DIG_MIXER_CHANNELS: 64). Which logical sounds hold a real voice is
// TryToPlayReal's decision - Docs/Reference/Sound.md, "Virtual voices".
constexpr size_t kMaxPlaying = 64;
constexpr int kChannels = 2;

} // namespace

AudioEngine::~AudioEngine() { Shutdown(); }

void AudioEngine::SDLCALLBACK(void* userdata, SDL_AudioStream* stream, int more, int) {
    AudioEngine* self = static_cast<AudioEngine*>(userdata);
    if (more <= 0) return;

    const int frames = more / int(sizeof(float) * kChannels);
    if (frames <= 0) return;

    // The callback runs on SDL's audio thread. It takes the lock, mixes, and
    // gets out; everything expensive - loading, distance maths, reaping - is
    // done on the game thread in Update().
    std::lock_guard<std::mutex> guard(self->lock_);
    if (self->scratch_.size() < size_t(frames) * kChannels)
        self->scratch_.assign(size_t(frames) * kChannels, 0.f);
    else
        std::fill(self->scratch_.begin(),
                  self->scratch_.begin() + ptrdiff_t(size_t(frames) * kChannels), 0.f);

    self->Mix(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, self->scratch_.data(),
                           frames * int(sizeof(float)) * kChannels);
}

bool AudioEngine::Init(const std::string& soundsRoot) {
    root_ = soundsRoot;
    voices_.assign(kMaxVoices, Playing{});

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LogWarn("audio: SDL_InitSubSystem failed: %s", SDL_GetError());
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = kChannels;
    spec.freq = rate_;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &AudioEngine::SDLCALLBACK, this);
    if (!stream_) {
        // A machine with no audio device is not a broken game.
        LogWarn("audio: no output device (%s); the game runs silent", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);

    // What SDL actually opened, which is not necessarily what was asked for.
    // Worth printing: a device that came up at a surprising rate, or a driver
    // that is not the one the machine plays everything else through, is the
    // difference between silence and sound and is invisible otherwise.
    const SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(stream_);
    SDL_AudioSpec got{};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(dev, &got, &frames)) {
        LogInfo("audio: driver '%s', device %u at %d Hz x%d, %d-frame buffer",
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
                unsigned(dev), got.freq, got.channels, frames);
    }
    LogInfo("audio: mixing %d Hz stereo, up to %zu voices", rate_, kMaxVoices);
    return true;
}

void AudioEngine::Shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    {
        std::lock_guard<std::mutex> guard(lock_);
        voices_.clear();
    }
    cache_.clear();
}

AudioEngine::Sample* AudioEngine::Load(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second.ok ? &it->second : nullptr;

    Sample& s = cache_[name];
    auto props = pendingProps_.find(name);
    if (props != pendingProps_.end()) {
        s.maxInstances = props->second.first;
        s.minIntervalMs = props->second.second;
    }
    const std::string path = root_ + "/" + name + ".wav";

    // Through the engine's VFS, NOT SDL_LoadWAV's own file opening. The
    // shipped game reads its data out of .pak archives, and a loader that
    // takes a filesystem path finds nothing there: every sound in the game
    // goes missing and the whole thing is silent, while a loose-file data root
    // works perfectly and hides it.
    std::vector<uint8_t> file;
    if (!ReadFile(path, file) || file.empty()) {
        // A named sample the game does not ship is an ordinary answer - the
        // scripts name plenty that only exist in other builds - so it is
        // counted rather than shouted about, and cached as a miss so a looping
        // caller does not probe for it every frame.
        ++missing_;
        s.ok = false;
        return nullptr;
    }

    SDL_AudioSpec have{};
    Uint8* raw = nullptr;
    Uint32 rawLen = 0;
    SDL_IOStream* io = SDL_IOFromConstMem(file.data(), file.size());
    if (!io || !SDL_LoadWAV_IO(io, true, &have, &raw, &rawLen)) {
        if (io) SDL_CloseIO(io);
        // Present but unreadable is a different thing from absent, and worth
        // saying out loud.
        LogWarn("audio: %s is not a WAV this build can read: %s", name.c_str(),
                SDL_GetError());
        ++missing_;
        s.ok = false;
        return nullptr;
    }

    // Convert once, at load, into the device's own format and rate. Mixing
    // then costs an add per sample instead of a resample per sample.
    SDL_AudioSpec want{};
    want.format = SDL_AUDIO_F32;
    want.channels = have.channels > 1 ? 2 : 1;   // mono stays mono, so it pans
    want.freq = rate_;

    Uint8* converted = nullptr;
    int convertedLen = 0;
    if (!SDL_ConvertAudioSamples(&have, raw, int(rawLen), &want, &converted, &convertedLen)) {
        LogWarn("audio: cannot convert %s: %s", name.c_str(), SDL_GetError());
        SDL_free(raw);
        ++missing_;
        s.ok = false;
        return nullptr;
    }
    SDL_free(raw);

    s.channels = want.channels;
    s.pcm.resize(size_t(convertedLen) / sizeof(float));
    std::memcpy(s.pcm.data(), converted, size_t(convertedLen));
    SDL_free(converted);
    s.ok = !s.pcm.empty();
    return s.ok ? &s : nullptr;
}

void AudioEngine::ComputeGains(Playing& p) const {
    if (!p.positional) {
        p.gain[0] = p.gain[1] = p.volume;
        return;
    }

    float to[3];
    for (int c = 0; c < 3; ++c) to[c] = p.pos[c] - listener_[c];
    const float dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);

    // dist1 is where it starts to fade, dist2 where it is gone. The soundsDef
    // files give both per sound, which is why this is not one global falloff.
    float attenuation = 1.f;
    if (p.dist2 > p.dist1 && dist > p.dist1)
        attenuation = std::max(0.f, 1.f - (dist - p.dist1) / (p.dist2 - p.dist1));
    else if (p.dist2 <= p.dist1 && dist > p.dist2 && p.dist2 > 0.f)
        attenuation = 0.f;

    const float level = p.volume * attenuation;
    if (dist < 1e-3f) {
        p.gain[0] = p.gain[1] = level;
        return;
    }

    // Pan by which side of the listener it is on. Constant-power, so a sound
    // crossing in front does not dip in the middle.
    const float side = (to[0] * right_[0] + to[1] * right_[1] + to[2] * right_[2]) / dist;
    const float pan = std::max(-1.f, std::min(1.f, side));
    const float angle = (pan + 1.f) * 0.25f * 3.14159265358979f;
    p.gain[0] = level * std::cos(angle);
    p.gain[1] = level * std::sin(angle);
}

void AudioEngine::Mix(float* out, int frames) {
    for (Playing& p : voices_) {
        if (!p.used || !p.playing || !p.real || p.paused || !p.sample) continue;

        const Sample& s = *p.sample;
        const size_t total = s.pcm.size() / size_t(s.channels);
        for (int f = 0; f < frames; ++f) {
            size_t idx = size_t(p.cursor);
            if (idx >= total) {
                if (p.loopsLeft < 0 || p.loopsLeft > 1) {
                    if (p.loopsLeft > 1) --p.loopsLeft;
                    p.cursor = 0.0;
                    idx = 0;
                } else {
                    p.playing = false;
                    Demote(p);
                    break;
                }
            }
            const float* src = &s.pcm[idx * size_t(s.channels)];
            const float l = src[0];
            const float r = s.channels > 1 ? src[1] : l;
            out[f * kChannels + 0] += l * p.gain[0];
            out[f * kChannels + 1] += r * p.gain[1];
            p.cursor += p.speed;
        }
    }

    // One soft clip at the end rather than per voice: a dozen sounds at once
    // will exceed 1.0 and hard clipping crackles.
    const int n = frames * kChannels;
    for (int i = 0; i < n; ++i) {
        float v = out[i] * masterVolume_;
        if (v > 1.f) v = 1.f;
        else if (v < -1.f) v = -1.f;
        out[i] = v;
    }
}

AudioEngine::Playing* AudioEngine::Resolve(Voice v) {
    if (v <= 0) return nullptr;
    const size_t index = size_t(uint32_t(v) & 0xffffu);
    const uint16_t gen = uint16_t(uint32_t(v) >> 16);
    if (index == 0 || index > voices_.size()) return nullptr;
    Playing& p = voices_[index - 1];
    if (!p.used || p.generation != gen) return nullptr;
    return &p;
}

const AudioEngine::Playing* AudioEngine::Resolve(Voice v) const {
    return const_cast<AudioEngine*>(this)->Resolve(v);
}

AudioEngine::Voice AudioEngine::Open(const std::string& name, bool positional, bool held) {
    if (!stream_) return 0;
    Sample* s = Load(name);
    if (!s) return 0;

    std::lock_guard<std::mutex> guard(lock_);
    size_t slot = voices_.size();
    for (size_t i = 0; i < voices_.size(); ++i)
        if (!voices_[i].used) { slot = i; break; }
    // The table grows rather than refusing. A held handle that is stopped and
    // never deleted - every burning-gas item does this - is a leaked record
    // in the original too (Sound3D_Stop keeps the object; only Sound3D_Delete
    // frees it), and a record that is not real costs nothing to mix. The
    // handle carries a 16-bit index, which is the only bound.
    if (slot == voices_.size()) {
        if (voices_.size() >= 0xfffe) return 0;
        voices_.emplace_back();
    }
    Playing& p = voices_[slot];
    const uint16_t gen = uint16_t(p.generation + 1 ? p.generation + 1 : 1);
    p = Playing{};
    p.generation = gen;
    p.sample = s;
    p.positional = positional;
    p.used = true;
    p.held = held;
    p.volume = 1.f;
    p.gain[0] = p.gain[1] = 1.f;
    ++started_;
    return MakeHandle(slot, gen);
}

AudioEngine::Voice AudioEngine::Create(const std::string& name, bool positional) {
    return Open(name, positional, true);
}

// Play2D/Play3D are FIRE AND FORGET. Almost every caller drops the handle
// without ever telling us, so these must not be held: a held voice keeps its
// slot after it finishes, and ninety-six dropped one-shots later nothing can
// play at all. The handle still works for the few callers that keep it - it
// just stops being valid once the sound ends, which is exactly what those
// callers are asking IsPlaying about.
AudioEngine::Voice AudioEngine::Play2D(const std::string& name, float volume,
                                       bool sameSpeedInBulletTime, bool noPitch) {
    // Recorded, not modelled: nothing scales voice speed with the game clock
    // yet. Docs/Reference/Sound.md
    (void)sameSpeedInBulletTime;
    const Voice v = Open(name, false, false);
    if (!v) return 0;
    std::lock_guard<std::mutex> guard(lock_);
    Playing* p = Resolve(v);
    if (!p) return 0;
    p->volume = volume > 0.f ? volume : 1.f;
    p->gain[0] = p->gain[1] = p->volume;
    // Never loops. A held 2D loop is SOUND2D.Create + Play, not this call.
    p->loopsLeft = 0;
    // A touch of pitch variation stops a repeated footfall sounding like a
    // machine; the soundsDef sets disablePitch where that would be wrong.
    p->speed = noPitch ? 1.0 : 0.95 + 0.1 * (double(SDL_rand(1000)) / 1000.0);
    p->playing = true;
    p->startedMs = NowMs();
    TryToPlayReal(*p, p->startedMs);
    return v;
}

AudioEngine::Voice AudioEngine::Play3D(const std::string& name, const float pos[3],
                                       float dist1, float dist2, bool noPitch) {
    const Voice v = Open(name, true, false);
    if (!v) return 0;
    std::lock_guard<std::mutex> guard(lock_);
    Playing* p = Resolve(v);
    if (!p) return 0;
    for (int c = 0; c < 3; ++c) p->pos[c] = pos[c];
    p->dist1 = dist1;
    p->dist2 = dist2;
    p->speed = noPitch ? 1.0 : 0.95 + 0.1 * (double(SDL_rand(1000)) / 1000.0);
    ComputeGains(*p);
    p->playing = true;
    p->startedMs = NowMs();
    TryToPlayReal(*p, p->startedMs);
    return v;
}

// ------------------------------------------------------------ real voices
//
// The original keeps every sound the scripts asked for as a LOGICAL sound and
// hands only some of them a Miles handle. What decides it, per file, is
// SOUND.SetSoundProperties - how many instances may sound at once and how
// close together two may start - and, per sound, how loud it would be:
// dist1 / distance. Recovered from MilesEngine::TryToPlayRealSound
// (0x101f43b0), Find3DSoundToStart/Stop (0x101f0c90 / 0x101f09a0) and Tick
// (0x101f49c0); the reasoning is in Docs/Reference/Sound.md.

float AudioEngine::Score(const Playing& p) const {
    // 2D sounds - the interface, the player's own weapon loops - are never
    // ranked against the world. The original scores them separately
    // (TryToPlayRealSound2D); here they simply always win.
    if (!p.positional) return 1e6f;
    float to[3];
    for (int c = 0; c < 3; ++c) to[c] = p.pos[c] - listener_[c];
    const float dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (p.dist2 > 0.f && dist > p.dist2) return 0.f;     // out of range
    return p.dist1 / std::max(dist, 1e-3f);
}

size_t AudioEngine::RealCount() const {
    size_t n = 0;
    for (const Playing& p : voices_)
        if (p.used && p.real) ++n;
    return n;
}

AudioEngine::Playing* AudioEngine::WeakestReal(const Sample* sameFile, float& score) {
    Playing* worst = nullptr;
    for (Playing& p : voices_) {
        if (!p.used || !p.real || (sameFile && p.sample != sameFile)) continue;
        const float s = Score(p);
        if (!worst || s < score) { worst = &p; score = s; }
    }
    return worst;
}

// The 2D victim: the oldest real instance of the file that is itself 2D.
// GetLongestPlaying2DSoundForFile (0x101f0bf0) ranks by priority then start
// tick and looks only at 2D handles, so a file whose instances are all 3D
// yields nothing and the 2D newcomer waits.
AudioEngine::Playing* AudioEngine::OldestReal2D(const Sample* sameFile) {
    Playing* oldest = nullptr;
    for (Playing& p : voices_) {
        if (!p.used || !p.real || p.positional || p.sample != sameFile) continue;
        if (!oldest || p.startedMs < oldest->startedMs) oldest = &p;
    }
    return oldest;
}

void AudioEngine::Demote(Playing& p) {
    if (!p.real) return;
    p.real = false;
    if (p.sample && p.sample->real > 0) --p.sample->real;
}

// Where a sound stands now, measured from its start: Start2DSample /
// Start3DSample seek the sample to (now - start) mod length and drop the
// passes already elapsed (FUN_101ed410, FUN_101eed10). False = it is over.
bool AudioEngine::Remaining(const Playing& p, uint32_t nowMs, double& cursor,
                            int& loopsLeft) const {
    const size_t total = p.sample->pcm.size() / size_t(p.sample->channels);
    if (total == 0) return false;
    const double elapsed =
        double(nowMs - p.startedMs) / 1000.0 * double(rate_) * std::max(p.speed, 1e-3);
    const double passes = std::floor(elapsed / double(total));
    loopsLeft = p.loopsLeft;
    if (p.loopsLeft >= 0) {
        const int plays = p.loopsLeft > 1 ? p.loopsLeft : 1;
        if (passes >= double(plays)) return false;
        loopsLeft = plays - int(passes);
    }
    cursor = elapsed - passes * double(total);
    return true;
}

void AudioEngine::TryToPlayReal(Playing& p, uint32_t nowMs) {
    if (!p.playing || p.real || !p.sample) return;
    const float score = Score(p);
    if (p.positional && score <= 0.f) return;              // out of range: waits
    double cursor = 0.0;
    int loopsLeft = p.loopsLeft;
    if (!Remaining(p, nowMs, cursor, loopsLeft)) {
        p.playing = false;                                  // waited itself out
        return;
    }
    Sample& file = *p.sample;
    const int maxInstances = file.maxInstances >= 0 ? file.maxInstances : defaultMaxInstances_;
    const int interval = file.minIntervalMs >= 0 ? file.minIntervalMs : defaultIntervalMs_;
    // The gap is measured from the file's last start whether or not that
    // instance is still audible (TryToPlayRealSound2D 0x101f44d0, +0x3c/+0x40).
    if (file.everStarted && nowMs - file.lastStartMs < uint32_t(interval)) return;

    // The file's own cap first, then the mixer's. A 3D newcomer takes a voice
    // only from something it clearly outscores; a 2D one always displaces the
    // file's OLDEST 2D instance - the rule that keeps a burst of menu hovers or
    // Painkiller wall hits from queueing up. Docs/Reference/Sound.md
    if (file.real >= maxInstances) {
        Playing* victim = nullptr;
        if (p.positional) {
            float weakest = 0.f;
            victim = WeakestReal(&file, weakest);
            if (victim && !(weakest < score - 0.1f)) victim = nullptr;
        } else {
            victim = OldestReal2D(&file);
            if (victim && !(victim->startedMs < p.startedMs)) victim = nullptr;
        }
        if (!victim) return;
        Demote(*victim);
    }
    if (RealCount() >= kMaxPlaying) {
        float weakest = 0.f;
        Playing* victim = WeakestReal(nullptr, weakest);
        if (!victim || !(weakest < score - 0.1f)) return;
        Demote(*victim);
    }
    p.real = true;
    p.cursor = cursor;
    p.loopsLeft = loopsLeft;
    ++file.real;
    file.lastStartMs = nowMs;
    file.everStarted = true;
}

void AudioEngine::SetSoundProperties(const std::string& name, int maxInstances,
                                     int intervalMs) {
    std::lock_guard<std::mutex> guard(lock_);
    if (name == "default") {
        defaultMaxInstances_ = maxInstances;
        defaultIntervalMs_ = intervalMs;
        return;
    }
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        it->second.maxInstances = maxInstances;
        it->second.minIntervalMs = intervalMs;
    } else {
        pendingProps_[name] = {maxInstances, intervalMs};
    }
}

// The setters all take the lock because the mixing callback reads what they
// write. They are short enough that the audio thread never waits long.
// A Voice is a PACKED handle - index in the low 16 bits, generation in the
// high 16 - which is what Open returns and what Resolve decodes. Treating it as
// a bare 1-based index made every setter here a silent no-op: the second voice
// of the second generation is 0x20002 = 131074, which fails the bounds check
// against 512 voices and returns. Nothing a script Created could be started,
// stopped, looped or moved - SOUND2D/SOUND3D.Create hands the script one of
// these handles and every call it then makes with it went nowhere.
#define PAINFUL_VOICE(v)                                                     \
    std::lock_guard<std::mutex> guard(lock_);                                \
    Playing* resolved = Resolve(v);                                          \
    if (!resolved) return;                                                   \
    Playing& p = *resolved;

void AudioEngine::Start(Voice v) {
    PAINFUL_VOICE(v)
    p.cursor = 0.0;
    p.playing = true;
    p.paused = false;
    p.startedMs = NowMs();
    TryToPlayReal(p, p.startedMs);
}

void AudioEngine::Stop(Voice v) {
    PAINFUL_VOICE(v)
    p.playing = false;
    Demote(p);
}

void AudioEngine::Pause(Voice v, bool paused) {
    PAINFUL_VOICE(v)
    p.paused = paused;
}

int AudioEngine::PauseCurrentlyPlaying() {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<Voice> set;
    for (size_t i = 0; i < voices_.size(); ++i) {
        Playing& p = voices_[i];
        // Already paused stays out of the set: a script paused it, and this
        // resume is not the one that should undo that.
        if (!p.used || !p.playing || p.paused) continue;
        p.paused = true;
        set.push_back(MakeHandle(i, p.generation));
    }
    const int token = nextPauseToken_++;
    pauseSets_[token] = std::move(set);
    return token;
}

void AudioEngine::ResumeSounds(int token) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = pauseSets_.find(token);
    if (it == pauseSets_.end()) return;
    // Resolve rejects a handle whose slot has since been reused, so a voice
    // that ended and was recycled while paused is simply skipped.
    for (Voice v : it->second)
        if (Playing* p = Resolve(v)) p->paused = false;
    pauseSets_.erase(it);
}

void AudioEngine::SetVolume(Voice v, float volume) {
    PAINFUL_VOICE(v)
    p.volume = volume;
    ComputeGains(p);
}

void AudioEngine::SetPosition(Voice v, const float pos[3]) {
    PAINFUL_VOICE(v)
    for (int c = 0; c < 3; ++c) p.pos[c] = pos[c];
    ComputeGains(p);
}

void AudioEngine::SetHearingDistance(Voice v, float dist1, float dist2) {
    PAINFUL_VOICE(v)
    p.dist1 = dist1;
    p.dist2 = dist2;
    ComputeGains(p);
}

void AudioEngine::SetLoopCount(Voice v, int count) {
    PAINFUL_VOICE(v)
    p.loopsLeft = count;
}

void AudioEngine::SetSpeed(Voice v, float speed) {
    PAINFUL_VOICE(v)
    if (speed > 0.f) p.speed = double(speed);
}

void AudioEngine::Release(Voice v, bool letFinish) {
    PAINFUL_VOICE(v)
    p.held = false;
    if (!letFinish) {
        Demote(p);
        p.playing = false;
        p.used = false;
    }
}

#undef PAINFUL_VOICE

bool AudioEngine::IsPlaying(Voice v) const {
    std::lock_guard<std::mutex> guard(lock_);
    const Playing* p = Resolve(v);
    return p && p->playing && !p->paused;
}

void AudioEngine::SetListener(const float pos[3], const float forward[3],
                              const float right[3]) {
    std::lock_guard<std::mutex> guard(lock_);
    for (int c = 0; c < 3; ++c) {
        listener_[c] = pos[c];
        forward_[c] = forward[c];
        right_[c] = right[c];
    }
}

void AudioEngine::Update() {
    if (!stream_) return;
    std::lock_guard<std::mutex> guard(lock_);
    const uint32_t now = NowMs();
    for (Playing& p : voices_) {
        if (!p.used) continue;
        // A voice nobody holds any more, that has finished, is free. One the
        // scripts still hold stays put: they are entitled to ask IsPlaying
        // about it, and to start it again.
        if (!p.held && !p.playing) {
            ++reaped_;
            const uint16_t gen = p.generation;
            p = Playing{};
            p.generation = gen;      // Open bumps it; keep the slot's history
            continue;
        }
        if (p.playing && p.positional) ComputeGains(p);
        if (!p.playing || p.paused || !p.sample) continue;
        if (p.real) {
            // MilesEngine::Tick stops whatever has left its hearing range
            // before it hands out anything new.
            if (p.positional && Score(p) <= 0.f) Demote(p);
            continue;
        }
        // Waiting. A one-shot that has waited its own length out is over -
        // it would have finished by now had it been heard.
        if (p.loopsLeft >= 0) {
            const size_t frames = p.sample->pcm.size() / size_t(p.sample->channels);
            const uint32_t lengthMs =
                uint32_t(double(frames) * 1000.0 / (double(rate_) * std::max(p.speed, 1e-3)) *
                         double(p.loopsLeft > 1 ? p.loopsLeft : 1));
            if (now - p.startedMs > lengthMs) {
                p.playing = false;
                continue;
            }
        }
        TryToPlayReal(p, now);
    }
}

// Make room at the mixing cap by STEALING the least audible voice.
//
// Sixty-four is the right number - the original's own log reports Miles at
// DIG_MIXER_CHANNELS: 64 - but dropping the newcomer is the wrong policy for
// it. A level running sixty-four ambient loops and monster sounds left no slot
// for the player's own jump, so the sound went missing exactly where there was
// most going on; on an empty TestFloor it always played. That asymmetry is the
// tell.
//
// Quietest first, by mixed gain, so a distant 3D sound loses to a close one.
// Held handles are never taken: a script that owns a flamethrower loop expects
// it to still be there, and stealing it would leave the handle pointing at
// whatever replaced it. Returns false when everything audible is spoken for,
// in which case the caller drops as before.
size_t AudioEngine::PlayingCount() const {
    std::lock_guard<std::mutex> guard(lock_);
    return RealCount();
}

void AudioEngine::LogRealVoices() const {
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& kv : cache_) {
        const Sample& s = kv.second;
        size_t waiting = 0;
        for (const Playing& p : voices_)
            if (p.used && p.playing && !p.real && p.sample == &s) ++waiting;
        if (s.real > 0 || waiting > 0)
            LogInfo("audio: %-48s real %d  waiting %zu  (max %d, gap %d ms)", kv.first.c_str(),
                    s.real, waiting,
                    s.maxInstances >= 0 ? s.maxInstances : defaultMaxInstances_,
                    s.minIntervalMs >= 0 ? s.minIntervalMs : defaultIntervalMs_);
    }
    LogInfo("audio: %zu real voices of %zu slots", RealCount(), voices_.size());
}

size_t AudioEngine::voicesPlaying() const {
    std::lock_guard<std::mutex> guard(lock_);
    return RealCount();
}

} // namespace painful
