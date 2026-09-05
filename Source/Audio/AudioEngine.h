#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

namespace painful {

// The sound system behind SOUND / SOUND2D / SOUND3D.
//
// PainEngine's audio is entirely script-driven: the scripts name a sample, ask
// for it in 2D or at a world position, and hold a handle they later stop or
// query. Every one of the 3222 shipped samples is plain uncompressed PCM in a
// .wav, mostly mono at 22 kHz, so there is no codec problem to solve - the work
// is mixing, distance attenuation and getting the handle lifetimes right.
//
// One device stream, mixed here rather than by SDL: SDL3 will happily open a
// stream per sound, but then nothing can cap how many play at once, and a
// Painkiller fight is a lot of sounds at once.
class AudioEngine {
public:
    ~AudioEngine();

    bool Init(const std::string& soundsRoot);
    void Shutdown();
    bool ready() const { return stream_ != nullptr; }

    // A voice handle, or 0. Zero is never valid, so the scripts' `if not h`
    // works and a failed load is simply inaudible rather than an error.
    using Voice = int;

    // `name` is a path under Sounds without the extension, exactly as the
    // scripts write it: "actor/evilmonkv2/monk_attack", "misc/gas-outflow-5sec".
    // A ONE-SHOT. Arg 3 is the scripts sameSpeedInBulletTime, not a loop -
    // taking it as one left the checkpoint heartbeat stacking endless voices.
    // Docs/Reference/Sound.md
    Voice Play2D(const std::string& name, float volume, bool sameSpeedInBulletTime,
                 bool noPitch);
    // dist1 is where attenuation starts, dist2 where it reaches silence - the
    // soundsDef files carry both per sound.
    Voice Play3D(const std::string& name, const float pos[3], float dist1, float dist2,
                 bool noPitch);
    // Created but not started, for the scripts that keep a handle around and
    // start/stop it themselves (a looping flamethrower, an elevator). Held:
    // the slot is the caller's until it Deletes or Forgets it.
    Voice Create(const std::string& name, bool positional);

    void Start(Voice v);
    void Stop(Voice v);
    void Pause(Voice v, bool paused);
    bool IsPlaying(Voice v) const;
    void SetVolume(Voice v, float volume);
    void SetPosition(Voice v, const float pos[3]);
    void SetHearingDistance(Voice v, float dist1, float dist2);
    // Counts down: 0 or 1 plays once, n plays n times, negative loops forever.
    // NOT the scripts' convention - Miles reads 0 as forever - so the native
    // translates. See L_SND_SetLoopCount.
    void SetLoopCount(Voice v, int count);
    void SetSpeed(Voice v, float speed);

    // Release the handle. The voice keeps playing to its end if `letFinish`,
    // which is what SOUND*.Forget means - fire it and stop caring.
    void Release(Voice v, bool letFinish);

    // Where the player is, for distance and panning. Forward and right are the
    // camera basis; the engine feeds these from SOUND.SetPlayerPos /
    // SetPlayerOrientation every frame.
    void SetListener(const float pos[3], const float forward[3], const float right[3]);
    // SOUND.SetSoundProperties(name, maxInstances, intervalMs); the name
    // "default" sets what every file without an entry uses. The engine's own
    // defaults are 100 and 0 (MilesEngine ctor); Definitions.lua's
    // SoundsProperties table sets default 6 / 100 ms and 88 specific files.
    void SetSoundProperties(const std::string& name, int maxInstances, int intervalMs);

    // Recomputes every positional voice's gains and reaps finished ones. Called
    // once a frame so the mixing callback stays a memcpy-and-add.
    // The clock the voice policy runs on. Real time, and it keeps running
    // while the game is paused - freezing it made the menu's hover sounds
    // queue up instead of dropping the late ones. Docs/Reference/Sound.md
    void Advance(float dt) { clockMs_ += double(dt) * 1000.0; }
    void Update();

    // What the menu does: pause everything audible right now and keep the
    // token, then resume exactly that set. Sounds started after the pause -
    // the menu's own clicks - are untouched. PainMenu::PauseSounds 0x1004f730
    int PauseCurrentlyPlaying();
    void ResumeSounds(int token);

    void SetMasterVolume(float v) { masterVolume_ = v; }

    size_t voicesPlaying() const;
    // Every sample with a real voice right now, with how many are real and how
    // many are waiting: the headless check that the per-file caps hold.
    void LogRealVoices() const;
    // Cumulative, because the instantaneous count is a poor diagnostic: a
    // hundred one-shots can play and finish between two samples of it.
    size_t voicesStarted() const { return started_; }
    size_t voicesReaped() const { return reaped_; }
    size_t samplesLoaded() const { return cache_.size(); }
    size_t samplesMissing() const { return missing_; }

private:
    struct Sample {
        std::vector<float> pcm;    // interleaved at the device rate
        int channels = 1;
        bool ok = false;
        // SOUND.SetSoundProperties for this file: how many instances may be
        // audible at once and how close together two may start. -1 means the
        // "default" entry applies. MilesLoadedFile +0x48 / +0x40.
        int maxInstances = -1;
        int minIntervalMs = -1;
        int real = 0;              // instances holding a real voice now
        uint32_t lastStartMs = 0;  // MilesLoadedFile +0x3c; the gap is timed from it
        bool everStarted = false;
    };

    struct Playing {
        Sample* sample = nullptr;
        double cursor = 0.0;       // in frames; fractional for pitch/speed
        double speed = 1.0;
        float volume = 1.f;
        float gain[2] = {1.f, 1.f};
        float pos[3] = {0, 0, 0};
        float dist1 = 0.f, dist2 = 0.f;
        int loopsLeft = 0;         // <0 forever
        bool positional = false;
        // playing is the LOGICAL state - the script asked for it and it has
        // not finished. real is whether it currently owns one of the audible
        // voices; a logical sound without one waits, and a one-shot that
        // waits past its own length just ends (Miles3DSound wants-to-play,
        // 0x101ee6e0).
        bool playing = false;
        bool real = false;
        bool paused = false;
        bool held = false;         // a script still owns the handle
        bool used = false;
        uint32_t startedMs = 0;
        // Bumped every time the slot is reused. A handle carries the value it
        // was issued with, so a script that keeps a fire-and-forget handle and
        // later asks IsPlaying about it gets "no" rather than an answer about
        // whatever sound has since taken the slot.
        uint16_t generation = 1;
    };

    // Handle layout: (generation << 16) | (index + 1). Index 0 is never
    // issued, so 0 stays "no voice" and the scripts' `if not h` works.
    static Voice MakeHandle(size_t index, uint16_t gen) {
        return Voice(((uint32_t(gen) << 16) | uint32_t(index + 1)));
    }
    Playing* Resolve(Voice v);
    const Playing* Resolve(Voice v) const;

    Voice Open(const std::string& name, bool positional, bool held);
    size_t PlayingCount() const;
    // The virtual-voice policy (MilesEngine::TryToPlayRealSound 0x101f43b0,
    // TryToPlayRealSound2D 0x101f44d0, Tick 0x101f49c0): a logical sound gets
    // a real voice when it is in range, its file's start gap has passed, and
    // its file and the mixer have room - or it displaces one: a 3D sound the
    // weakest it outscores by 0.1, a 2D sound the file's oldest 2D instance.
    // It then plays from where it would be by now, not from the top.
    // Callers hold lock_.
    void TryToPlayReal(Playing& p, uint32_t nowMs);
    void Demote(Playing& p);
    bool Remaining(const Playing& p, uint32_t nowMs, double& cursor, int& loopsLeft) const;
    float Score(const Playing& p) const;
    size_t RealCount() const;
    Playing* WeakestReal(const Sample* sameFile, float& score);
    Playing* OldestReal2D(const Sample* sameFile);
    Sample* Load(const std::string& name);
    void Mix(float* out, int frames);
    void ComputeGains(Playing& p) const;
    static void SDLCALLBACK(void* userdata, SDL_AudioStream* stream, int more, int total);

    SDL_AudioStream* stream_ = nullptr;
    std::string root_;
    std::unordered_map<std::string, Sample> cache_;
    std::vector<Playing> voices_;
    mutable std::mutex lock_;      // guards voices_; the callback holds it briefly

    // One entry per outstanding PauseCurrentlyPlaying, so nested pauses each
    // resume only what they took.
    std::unordered_map<int, std::vector<Voice>> pauseSets_;
    int nextPauseToken_ = 1;
    std::vector<float> scratch_;

    float listener_[3] = {0, 0, 0};
    float forward_[3] = {0, 0, 1};
    float right_[3] = {1, 0, 0};
    float masterVolume_ = 1.f;
    size_t missing_ = 0;
    // Properties named before their file is loaded, applied at Load.
    std::unordered_map<std::string, std::pair<int, int>> pendingProps_;
    int defaultMaxInstances_ = 100;
    int defaultIntervalMs_ = 0;
    double clockMs_ = 0.0;
    uint32_t NowMs() const { return uint32_t(clockMs_); }
    size_t started_ = 0;
    size_t reaped_ = 0;
    int rate_ = 44100;
};

} // namespace painful
