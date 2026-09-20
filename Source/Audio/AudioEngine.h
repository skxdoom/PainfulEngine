#pragma once
#include <atomic>
#include <cstdint>
#include "../Core/Vectors.h"
#include <memory>
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
	// Both out of line: MusicStream is complete only in the .cpp.
	AudioEngine();
	~AudioEngine();

	bool Init(const std::string& soundsRoot);
	void Shutdown();
	bool ready() const { return stream_ != nullptr; }

	// A voice as the engine keys it: the script's ID doubled, plus one for 3D.
	// 2D and 3D sounds count their IDs separately from 0 (MilesEngine+0x220 /
	// +0x2a4), so one number can name both. -1 is none, as Create returns.
	// Sound.md, "Handles are Miles IDs"
	using Voice = int;
	static constexpr Voice kNoVoice = -1;
	static Voice Key(bool positional, int id) {
		return id < 0 || id > 0x3fffffff ? kNoVoice : id * 2 + (positional ? 1 : 0);
	}
	static int IdOf(Voice v) { return v < 0 ? -1 : v / 2; }

	// `name` is a path under Sounds without the extension, exactly as the
	// scripts write it: "actor/evilmonkv2/monk_attack", "misc/gas-outflow-5sec".
	// A ONE-SHOT. Arg 3 is the scripts sameSpeedInBulletTime, not a loop -
	// taking it as one left the checkpoint heartbeat stacking endless voices.
	// Docs/Reference/Sound.md
	Voice Play2D(const std::string& name, float volume, bool sameSpeedInBulletTime,
			bool noPitch);
	// dist1 is where attenuation starts, dist2 where it reaches silence - the
	// soundsDef files carry both per sound.
	Voice Play3D(const std::string& name, const Vec3& pos, float dist1, float dist2,
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
	void SetPosition(Voice v, const Vec3& pos);
	void SetHearingDistance(Voice v, float dist1, float dist2);
	// Counts down: 0 or 1 plays once, n plays n times, negative loops forever.
	// NOT the scripts' convention - Miles reads 0 as forever - so the native
	// translates. See L_SND_SetLoopCount.
	void SetLoopCount(Voice v, int count);
	void SetSpeed(Voice v, float speed);
	// SOUND2D.Create's arguments 2 and 4 (Sound2D_CreateEx 0x101f60f0): keeps its
	// rate under SetWorldSpeed; left out of a save (record flag 0x10).
	void SetSameSpeed(Voice v, bool on);
	void SetDontSave(Voice v);
	// WORLD.SetWorldSpeed's audio: every voice not marked sameSpeed plays at
	// this rate, and the mix is low-passed at sqrt(rate) of Nyquist (0.2..1).
	// MilesEngine::SetSpeed / SetLowPass, 0x101F2090 / 0x101F13C0. Sound.md
	void SetWorldSpeed(float rate);

	// Release the handle. The voice keeps playing to its end if `letFinish`,
	// which is what SOUND*.Forget means - fire it and stop caring.
	void Release(Voice v, bool letFinish);

	// Where the player is, for distance and panning. Forward and right are the
	// camera basis; the engine feeds these from SOUND.SetPlayerPos /
	// SetPlayerOrientation every frame.
	void SetListener(const Vec3& pos, const Vec3& forward, const Vec3& right);
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

	// The three sliders, composed the way Engine.dll composes them: samples
	// play at master * master * effects (the master is applied once per
	// sample and once more by Miles' digital master), streams at master *
	// streaming. Docs/Reference/Sound.md, "The master applies twice".
	void SetMasterVolume(float v) { masterVolume_ = v; RecomputeBusGains(); }
	void SetEffectsVolume(float v) { effectsVolume_ = v; RecomputeBusGains(); }
	void SetStreamingVolume(float v) { streamingVolume_ = v; RecomputeBusGains(); }
	// SOUND.Set3DSoundFalloff -> AIL_set_3D_rolloff_factor: the k in
	// dist1 / (dist1 + k * (d - dist1)).
	void SetRolloff(float k) { rolloff_ = k > 0.f ? k : 1.f; }
	float rolloff() const { return rolloff_; }

	// --- music streams (the SOUND.Stream* natives) ---
	// Slots are the scripts' own small integers (0 ambient, 1 battle, 2 the
	// stats loop). An .mp3 under <Data>/Music, decoded as it plays.
	bool StreamLoad(int slot, const std::string& name);
	void StreamDelete(int slot);
	// Starts from the beginning at volume 0, as StreamPlay (0x10124870) does.
	void StreamPlay(int slot, bool loop);
	void StreamPause(int slot);
	void StreamResume(int slot);
	void StreamSetVolume(int slot, float volume);
	float StreamGetVolume(int slot) const;
	bool StreamIsPlaying(int slot) const;
	void StreamSetLowPass(int slot, float cutoff);
	float StreamGetLowPass(int slot) const;
	size_t streamsPlaying() const;
	// A slot as a save keeps it (MilesEngine::SaveAudio's stream records): the file,
	// the byte decoding had reached, the volume, and whether it plays.
	struct StreamState {
		std::string name;
		size_t offset = 0;
		float volume = 0.f;
		bool playing = false, paused = false, loop = false;
	};
	// One per slot, with an empty name where the slot holds nothing.
	std::vector<StreamState> StreamStates() const;
	// Loads the file into the slot and carries on from the saved byte.
	bool RestoreStream(int slot, const StreamState& state);

	// A sound as a save keeps it (Miles2DSound / Miles3DSound records). Saving
	// stops what plays and files it in a pause set, which SaveGame_ResumeSounds
	// lifts after the load: `resumes`. Formats.md, "The audio chunk"
	struct VoiceState {
		int id = -1;
		bool positional = false;
		std::string name; // under Sounds, no extension
		bool forget = false; // nobody holds it: freed once stopped
		bool resumes = false;
		bool sameSpeed = false;
		int loopCount = 1; // Miles' count: 0 forever
		uint32_t offset = 0; // bytes into the file's sample data, as AIL reports it
		float volume = 1.f;
		float speed = 1.f;
		Vec3 pos;
		float dist1 = 0.f, dist2 = 0.f;
	};
	std::vector<VoiceState> VoiceStates() const;
	int NextId(bool positional) const;
	// Every sound replaced by these, at their IDs, stopped; the counters continue
	// from next2D / next3D. LoadAudio (0x101f6d40).
	void RestoreVoices(const std::vector<VoiceState>& voices, int next2D, int next3D);
	// SOUND.SaveGame_ResumeSounds (0x101f5500): the restored `resumes` sounds
	// play on from their offsets.
	void ResumeSaved();
	// What SetSoundProperties gave the file, or the defaults.
	void GetSoundProperties(const std::string& name, int& maxInstances, int& intervalMs) const;

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
		std::vector<float> pcm; // interleaved at the device rate
		int channels = 1;
		bool ok = false;
		std::string name; // the cache key, which a save records
		int srcRate = 0, srcBytesPerFrame = 0; // the file's own format, for AIL offsets
		// SOUND.SetSoundProperties for this file: how many instances may be
		// audible at once and how close together two may start. -1 means the
		// "default" entry applies. MilesLoadedFile +0x48 / +0x40.
		int maxInstances = -1;
		int minIntervalMs = -1;
		int real = 0; // instances holding a real voice now
		uint32_t lastStartMs = 0; // MilesLoadedFile +0x3c; the gap is timed from it
		bool everStarted = false;
	};

	struct Playing {
		Sample* sample = nullptr;
		double cursor = 0.0; // in frames; fractional for pitch/speed
		double speed = 1.0;
		bool sameSpeed = false; // keeps its rate under SetWorldSpeed
		float volume = 1.f;
		float gain[2] = {1.f, 1.f};
		Vec3 pos;
		float dist1 = 0.f, dist2 = 0.f;
		int loopsLeft = 0; // <0 forever
		bool positional = false;
		// playing is the LOGICAL state - the script asked for it and it has
		// not finished. real is whether it currently owns one of the audible
		// voices; a logical sound without one waits, and a one-shot that
		// waits past its own length just ends (Miles3DSound wants-to-play,
		// 0x101ee6e0).
		bool playing = false;
		bool real = false;
		bool paused = false;
		bool held = false; // a script still owns the handle
		bool used = false;
		uint32_t startedMs = 0;
		// IDs are never reused, so a kept handle whose sound was freed resolves
		// to nothing rather than to whatever took the slot.
		Voice key = kNoVoice;
		bool dontSave = false;
	};

	Playing* Resolve(Voice v);
	const Playing* Resolve(Voice v) const;

	Voice Open(const std::string& name, bool positional, bool held);
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
	std::unordered_map<Voice, size_t> slotOf_; // key -> index into voices_
	int nextId_[2] = {0, 0}; // 2D, 3D
	std::vector<Voice> savedResume_; // RestoreVoices' `resumes`, for ResumeSaved
	double FramesOf(const Sample& s, uint32_t bytes) const;
	uint32_t BytesOf(const Sample& s, double frames) const;
	mutable std::mutex lock_; // guards voices_; the callback holds it briefly

	// One entry per outstanding PauseCurrentlyPlaying, so nested pauses each
	// resume only what they took.
	// What one PauseCurrentlyPlaying stopped: voices and music streams, as
	// MilesEngine::PauseCurrentlyPlayingSounds (0x101F4DF0) pauses every
	// playing AIL stream too and ResumeSounds restarts them.
	struct PauseSet {
		std::vector<Voice> voices;
		std::vector<int> streams;
	};
	std::unordered_map<int, PauseSet> pauseSets_;
	int nextPauseToken_ = 1;
	std::vector<float> scratch_;

	Vec3 listener_;
	Vec3 forward_{0, 0, 1};
	Vec3 right_{1, 0, 0};
	float masterVolume_ = 1.f;
	float effectsVolume_ = 1.f;
	float streamingVolume_ = 1.f;
	// The mixer thread reads these; the game thread writes them from the
	// volume sliders. Atomic rather than locked - a slider is not worth
	// making the audio callback wait.
	std::atomic<float> sampleGain_{1.f}; // master * master * effects
	std::atomic<float> streamGain_{1.f}; // master * streaming
	float rolloff_ = 1.f;
	std::atomic<float> worldRate_{1.f};
	std::atomic<float> lowPassCut_{1.f};
	float lowPass_[2] = {0.f, 0.f}; // one-pole state, mixer thread only
	double Rate(const Playing& p) const {
		return p.speed * (p.sameSpeed ? 1.0 : double(worldRate_.load()));
	}
	void RecomputeBusGains() {
		sampleGain_ = masterVolume_ * masterVolume_ * effectsVolume_;
		streamGain_ = masterVolume_ * streamingVolume_;
	}
	// One music stream: the file, the decoder position, and an SDL stream
	// that converts decoded frames to the device format. Defined in the .cpp
	// so the decoder header stays out of this one.
	struct MusicStream;
	std::vector<std::unique_ptr<MusicStream>> streams_;
	std::string musicRoot_;
	std::vector<float> streamScratch_;
	void RefillStreams();
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
