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
    Voice Play2D(const std::string& name, float volume, bool loop, bool noPitch);
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

    // Recomputes every positional voice's gains and reaps finished ones. Called
    // once a frame so the mixing callback stays a memcpy-and-add.
    void Update();

    void SetMasterVolume(float v) { masterVolume_ = v; }

    size_t voicesPlaying() const;
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
    };

    struct Playing {
        const Sample* sample = nullptr;
        double cursor = 0.0;       // in frames; fractional for pitch/speed
        double speed = 1.0;
        float volume = 1.f;
        float gain[2] = {1.f, 1.f};
        float pos[3] = {0, 0, 0};
        float dist1 = 0.f, dist2 = 0.f;
        int loopsLeft = 0;         // <0 forever
        bool positional = false;
        bool playing = false;
        bool paused = false;
        bool held = false;         // a script still owns the handle
        bool used = false;
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
    const Sample* Load(const std::string& name);
    void Mix(float* out, int frames);
    void ComputeGains(Playing& p) const;
    static void SDLCALLBACK(void* userdata, SDL_AudioStream* stream, int more, int total);

    SDL_AudioStream* stream_ = nullptr;
    std::string root_;
    std::unordered_map<std::string, Sample> cache_;
    std::vector<Playing> voices_;
    mutable std::mutex lock_;      // guards voices_; the callback holds it briefly
    std::vector<float> scratch_;

    float listener_[3] = {0, 0, 0};
    float forward_[3] = {0, 0, 1};
    float right_[3] = {1, 0, 0};
    float masterVolume_ = 1.f;
    size_t missing_ = 0;
    size_t started_ = 0;
    size_t reaped_ = 0;
    int rate_ = 44100;
};

} // namespace painful
