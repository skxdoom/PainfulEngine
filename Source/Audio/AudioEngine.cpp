#include "AudioEngine.h"

#include "../Core/Check.h"
#include "../Core/Vectors.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <SDL3/SDL.h>

// The music decoder (CC0, External/minimp3). Frames are decoded on the game
// thread in RefillStreams and handed to an SDL_AudioStream for conversion.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include <minimp3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

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

// One SOUND.Stream* slot. The whole .mp3 stays in memory (a few MB) and is
// decoded a frame at a time ahead of the mixer; `conv` converts the decoded
// frames to the device format and buffers them. Sound.md, "Music streams".
struct AudioEngine::MusicStream {
	std::string name;
	std::vector<uint8_t> file;
	mp3dec_t dec{};
	size_t offset = 0; // next byte to decode
	SDL_AudioStream* conv = nullptr;
	int rate = 0, channels = 0;
	bool ok = false;
	bool playing = false, paused = false, loop = false, eof = false;
	float volume = 0.f;
	float lowPass = 0.f; // recorded, not filtered

	~MusicStream() {
		if (conv) SDL_DestroyAudioStream(conv);
	}
	void Rewind() {
		mp3dec_init(&dec);
		offset = 0;
		eof = false;
		if (conv) SDL_ClearAudioStream(conv);
	}
	// Decodes one frame into conv. False at the end of the file.
	bool DecodeFrame() {
		static thread_local short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
		while (offset < file.size()) {
			mp3dec_frame_info_t info{};
			const int samples = mp3dec_decode_frame(&dec, file.data() + offset,
					int(file.size() - offset), pcm, &info);
			if (info.frame_bytes <= 0) break;
			offset += size_t(info.frame_bytes);
			if (samples <= 0) continue; // an ID3 tag or garbage, skipped
			if (!conv) {
				rate = info.hz;
				channels = info.channels;
				const SDL_AudioSpec src{SDL_AUDIO_S16, channels, rate};
				const SDL_AudioSpec dst{SDL_AUDIO_F32, kChannels, 44100};
				conv = SDL_CreateAudioStream(&src, &dst);
				if (!conv) return false;
			}
			SDL_PutAudioStreamData(conv, pcm, samples * channels * int(sizeof(short)));
			return true;
		}
		return false;
	}
};

AudioEngine::AudioEngine() = default;
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
	// "../Data/Music/%s.mp3" (StreamLoad, 0x101247A0): beside Sounds.
	{
		const size_t slash = root_.find_last_of("/\\");
		musicRoot_ = (slash == std::string::npos ? std::string(".") : root_.substr(0, slash)) +
				"/Music";
	}
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
		slotOf_.clear();
		savedResume_.clear();
		streams_.clear();
	}
	cache_.clear();
}

AudioEngine::Sample* AudioEngine::Load(const std::string& name) {
	auto it = cache_.find(name);
	if (it != cache_.end()) return it->second.ok ? &it->second : nullptr;

	Sample& s = cache_[name];
	s.name = name;
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
	// closeio = true, so SDL closes the stream on the failure path too
	// (SDL_wave.c: `if (closeio && src) SDL_CloseIO(src)` under `done:`).
	// Closing it again here was a double free.
	if (!io || !SDL_LoadWAV_IO(io, true, &have, &raw, &rawLen)) {
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
	want.channels = have.channels > 1 ? 2 : 1; // mono stays mono, so it pans
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

	s.srcRate = have.freq;
	s.srcBytesPerFrame = int(SDL_AUDIO_FRAMESIZE(have));
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

	Vec3 to;
	for (int c = 0; c < 3; ++c) to[c] = p.pos[c] - listener_[c];
	const float dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);

	// AIL_set_3D_sample_distances(max = dist2, min = dist1) under the rolloff
	// factor Set3DSoundFalloff sets: full inside dist1, dist1 / (dist1 + k *
	// (d - dist1)) beyond it, inaudible past dist2. Sound.md, "Falloff".
	float attenuation = 1.f;
	if (p.dist2 > 0.f && dist > p.dist2)
		attenuation = 0.f;
	else if (p.dist1 > 0.f && dist > p.dist1)
		attenuation = p.dist1 / (p.dist1 + rolloff_ * (dist - p.dist1));

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
	// One atomic load per buffer, not per sample: the sliders and the
	// bullet-time rate change at most once a game frame.
	const float sampleGain = sampleGain_.load(std::memory_order_relaxed);
	for (Playing& p : voices_) {
		if (!p.used || !p.playing || !p.real || p.paused || !p.sample) continue;

		const Sample& s = *p.sample;
		const double rate = Rate(p);
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
			out[f * kChannels + 0] += l * p.gain[0] * sampleGain;
			out[f * kChannels + 1] += r * p.gain[1] * sampleGain;
			p.cursor += rate;
		}
	}

	// The bullet-time low-pass, one pole per channel on the voice mix. The
	// streams are left alone: the scripts pause them for the duration.
	const float cut = lowPassCut_.load();
	if (cut < 1.f) {
		const float a = 1.f - std::exp(-3.14159265f * cut);
		for (int f = 0; f < frames; ++f)
			for (int c = 0; c < kChannels; ++c) {
				float& v = out[f * kChannels + c];
				lowPass_[c] += (v - lowPass_[c]) * a;
				v = lowPass_[c];
			}
	}

	// The music streams, already converted to the device format by their
	// SDL streams (which are thread-safe; only the flags need the lock).
	const int n = frames * kChannels;
	for (const std::unique_ptr<MusicStream>& ms : streams_) {
		if (!ms || !ms->playing || ms->paused || !ms->conv) continue;
		if (streamScratch_.size() < size_t(n)) streamScratch_.assign(size_t(n), 0.f);
		const int got = SDL_GetAudioStreamData(ms->conv, streamScratch_.data(),
				n * int(sizeof(float)));
		const int gotSamples = got > 0 ? got / int(sizeof(float)) : 0;
		const float g = ms->volume * streamGain_.load(std::memory_order_relaxed);
		for (int i = 0; i < gotSamples; ++i) out[i] += streamScratch_[size_t(i)] * g;
		// Drained after the end of the file: a one-shot stream is over.
		if (gotSamples < n && ms->eof) ms->playing = false;
	}

	// One soft clip at the end rather than per voice: a dozen sounds at once
	// will exceed 1.0 and hard clipping crackles.
	for (int i = 0; i < n; ++i) {
		float v = out[i];
		if (v > 1.f) v = 1.f;
		else if (v < -1.f) v = -1.f;
		out[i] = v;
	}
}

AudioEngine::Playing* AudioEngine::Resolve(Voice v) {
	if (v < 0) return nullptr;
	const auto it = slotOf_.find(v);
	if (it == slotOf_.end()) return nullptr;
	Playing& p = voices_[it->second];
	return p.used && p.key == v ? &p : nullptr;
}

const AudioEngine::Playing* AudioEngine::Resolve(Voice v) const {
	return const_cast<AudioEngine*>(this)->Resolve(v);
}

AudioEngine::Voice AudioEngine::Open(const std::string& name, bool positional, bool held) {
	if (!stream_) return kNoVoice;
	Sample* s = Load(name);
	if (!s) return kNoVoice;

	std::lock_guard<std::mutex> guard(lock_);
	size_t slot = voices_.size();
	for (size_t i = 0; i < voices_.size(); ++i)
		if (!voices_[i].used) { slot = i; break; }
	// The table grows rather than refusing. A held handle that is stopped and
	// never deleted - every burning-gas item does this - is a leaked record
	// in the original too (Sound3D_Stop keeps the object; only Sound3D_Delete
	// frees it), and a record that is not real costs nothing to mix.
	if (slot == voices_.size()) voices_.emplace_back();
	Playing& p = voices_[slot];
	p = Playing{};
	// Sound2D_Create / Sound3D_Create (0x101f6010 / 0x101f6260): the next ID of
	// the kind.
	p.key = Key(positional, nextId_[positional ? 1 : 0]++);
	slotOf_[p.key] = slot;
	p.sample = s;
	p.positional = positional;
	p.used = true;
	p.held = held;
	p.volume = 1.f;
	p.gain[0] = p.gain[1] = 1.f;
	++started_;
	return p.key;
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
	const Voice v = Open(name, false, false);
	if (v < 0) return kNoVoice;
	std::lock_guard<std::mutex> guard(lock_);
	Playing* p = Resolve(v);
	if (!p) return kNoVoice;
	p->sameSpeed = sameSpeedInBulletTime;
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

AudioEngine::Voice AudioEngine::Play3D(const std::string& name, const Vec3& pos,
		float dist1, float dist2, bool noPitch) {
	const Voice v = Open(name, true, false);
	if (v < 0) return kNoVoice;
	std::lock_guard<std::mutex> guard(lock_);
	Playing* p = Resolve(v);
	if (!p) return kNoVoice;
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
	Vec3 to;
	for (int c = 0; c < 3; ++c) to[c] = p.pos[c] - listener_[c];
	const float dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
	if (p.dist2 > 0.f && dist > p.dist2) return 0.f; // out of range
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
		double(nowMs - p.startedMs) / 1000.0 * double(rate_) * std::max(Rate(p), 1e-3);
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
	if (p.positional && score <= 0.f) return; // out of range: waits
	double cursor = 0.0;
	int loopsLeft = p.loopsLeft;
	if (!Remaining(p, nowMs, cursor, loopsLeft)) {
		p.playing = false; // waited itself out
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
// write. They are short enough that the audio thread never waits long. Each
// goes through Resolve: a Voice is a key, never an index into voices_.
#define PAINFUL_VOICE(v) \
	std::lock_guard<std::mutex> guard(lock_); \
	Playing* resolved = Resolve(v); \
	if (!resolved) return; \
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
	PauseSet set;
	for (Playing& p : voices_) {
		// Already paused stays out of the set: a script paused it, and this
		// resume is not the one that should undo that.
		if (!p.used || !p.playing || p.paused) continue;
		p.paused = true;
		set.voices.push_back(p.key);
	}
	// The music too: MilesEngine::PauseCurrentlyPlayingSounds walks the AIL
	// streams after the samples (AIL_pause_stream), and ResumeSounds restarts them.
	for (size_t i = 0; i < streams_.size(); ++i) {
		MusicStream* ms = streams_[i].get();
		if (!ms || !ms->playing || ms->paused) continue;
		ms->paused = true;
		set.streams.push_back(int(i));
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
	for (Voice v : it->second.voices)
		if (Playing* p = Resolve(v)) p->paused = false;
	for (int slot : it->second.streams)
		if (size_t(slot) < streams_.size() && streams_[size_t(slot)])
			streams_[size_t(slot)]->paused = false;
	pauseSets_.erase(it);
}

void AudioEngine::SetVolume(Voice v, float volume) {
	PAINFUL_VOICE(v)
	p.volume = volume;
	ComputeGains(p);
}

void AudioEngine::SetPosition(Voice v, const Vec3& pos) {
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

void AudioEngine::SetSameSpeed(Voice v, bool on) {
	PAINFUL_VOICE(v)
	p.sameSpeed = on;
}

void AudioEngine::SetDontSave(Voice v) {
	PAINFUL_VOICE(v)
	p.dontSave = true;
}

// WORLD.SetWorldSpeed's audio half. The cut-off follows MilesEngine::SetLowPass:
// sqrt of the rate, 1 and above unfiltered, below 0.2 held at 0.2.
void AudioEngine::SetWorldSpeed(float rate) {
	if (rate <= 0.f) rate = 1e-3f;
	worldRate_.store(rate);
	float cut = std::sqrt(rate);
	if (cut > 1.f) cut = 1.f;
	else if (cut < 0.2f) cut = 0.2f;
	lowPassCut_.store(cut);
}

void AudioEngine::Release(Voice v, bool letFinish) {
	PAINFUL_VOICE(v)
	p.held = false;
	if (!letFinish) {
		Demote(p);
		slotOf_.erase(p.key);
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

void AudioEngine::SetListener(const Vec3& pos, const Vec3& forward,
		const Vec3& right) {
	std::lock_guard<std::mutex> guard(lock_);
	for (int c = 0; c < 3; ++c) {
		listener_[c] = pos[c];
		forward_[c] = forward[c];
		right_[c] = right[c];
	}
}

void AudioEngine::Update() {
	if (!stream_) return;
	RefillStreams();
	std::lock_guard<std::mutex> guard(lock_);
	const uint32_t now = NowMs();
	for (Playing& p : voices_) {
		if (!p.used) continue;
		// A voice nobody holds any more, that has finished, is free. One the
		// scripts still hold stays put: they are entitled to ask IsPlaying
		// about it, and to start it again.
		if (!p.held && !p.playing) {
			++reaped_;
			slotOf_.erase(p.key);
			p = Playing{};
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
				uint32_t(double(frames) * 1000.0 / (double(rate_) * std::max(Rate(p), 1e-3)) *
						double(p.loopsLeft > 1 ? p.loopsLeft : 1));
			if (now - p.startedMs > lengthMs) {
				p.playing = false;
				continue;
			}
		}
		TryToPlayReal(p, now);
	}
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
	for (size_t i = 0; i < streams_.size(); ++i) {
		const MusicStream* ms = streams_[i].get();
		if (!ms) continue;
		LogInfo("audio: music slot %zu %s %s%s volume %.0f%% at byte %zu of %zu", i,
				ms->name.c_str(), ms->playing ? "playing" : "stopped",
				ms->paused ? " (paused)" : "", ms->volume * 100.f, ms->offset, ms->file.size());
	}
}

size_t AudioEngine::voicesPlaying() const {
	std::lock_guard<std::mutex> guard(lock_);
	return RealCount();
}

// ---------------------------------------------------------------- music

bool AudioEngine::StreamLoad(int slot, const std::string& name) {
	// 16 music slots (SOUND.StreamLoad); outside that is a script bug, not a
	// slot that happens to be empty.
	if (!PAINFUL_CHECK(slot >= 0 && slot <= 15, "audio: music slot %d out of 0..15", slot))
		return false;
	StreamDelete(slot);
	if (name.empty()) return false;
	auto ms = std::make_unique<MusicStream>();
	ms->name = name;
	const std::string path = musicRoot_ + "/" + name + ".mp3";
	if (!ReadFile(path, ms->file) || ms->file.empty()) {
		LogWarn("audio: music %s not found", path.c_str());
		return false;
	}
	ms->Rewind();
	// The first frame tells the rate and channel count.
	if (!ms->DecodeFrame() || !ms->conv) {
		LogWarn("audio: music %s does not decode", path.c_str());
		return false;
	}
	ms->ok = true;
	std::lock_guard<std::mutex> guard(lock_);
	if (streams_.size() <= size_t(slot)) streams_.resize(size_t(slot) + 1);
	streams_[size_t(slot)] = std::move(ms);
	LogInfo("audio: music slot %d = %s (%d Hz x%d, %zu KB)", slot, name.c_str(),
			streams_[size_t(slot)]->rate, streams_[size_t(slot)]->channels,
			streams_[size_t(slot)]->file.size() / 1024);
	return true;
}

void AudioEngine::StreamDelete(int slot) {
	std::unique_ptr<MusicStream> gone;
	{
		std::lock_guard<std::mutex> guard(lock_);
		if (slot < 0 || size_t(slot) >= streams_.size()) return;
		gone = std::move(streams_[size_t(slot)]);
		// A pause set must not restart whatever is put in the slot next, as Resolve
		// refuses a recycled voice. A load deletes the streams and restores its own
		// with their own pause state; LoadAudio drops the old sets the same way.
		for (auto& kv : pauseSets_) {
			std::vector<int>& taken = kv.second.streams;
			taken.erase(std::remove(taken.begin(), taken.end(), slot), taken.end());
		}
	}
	// Destroyed outside the lock: the SDL stream teardown is not instant.
}

#define PAINFUL_STREAM(slot) \
	std::lock_guard<std::mutex> guard(lock_); \
	if (slot < 0 || size_t(slot) >= streams_.size() || !streams_[size_t(slot)]) \
		return; \
	MusicStream& ms = *streams_[size_t(slot)];

void AudioEngine::StreamPlay(int slot, bool loop) {
	PAINFUL_STREAM(slot)
	ms.Rewind();
	ms.DecodeFrame();
	ms.loop = loop;
	ms.volume = 0.f;
	ms.paused = false;
	ms.playing = ms.ok;
}

void AudioEngine::StreamPause(int slot) {
	PAINFUL_STREAM(slot)
	ms.paused = true;
}

void AudioEngine::StreamResume(int slot) {
	PAINFUL_STREAM(slot)
	ms.paused = false;
}

void AudioEngine::StreamSetVolume(int slot, float volume) {
	PAINFUL_STREAM(slot)
	ms.volume = std::max(0.f, std::min(1.f, volume));
}

void AudioEngine::StreamSetLowPass(int slot, float cutoff) {
	PAINFUL_STREAM(slot)
	ms.lowPass = cutoff;
}

#undef PAINFUL_STREAM

float AudioEngine::StreamGetVolume(int slot) const {
	std::lock_guard<std::mutex> guard(lock_);
	if (slot < 0 || size_t(slot) >= streams_.size() || !streams_[size_t(slot)]) return 0.f;
	return streams_[size_t(slot)]->volume;
}

float AudioEngine::StreamGetLowPass(int slot) const {
	std::lock_guard<std::mutex> guard(lock_);
	if (slot < 0 || size_t(slot) >= streams_.size() || !streams_[size_t(slot)]) return 0.f;
	return streams_[size_t(slot)]->lowPass;
}

std::vector<AudioEngine::StreamState> AudioEngine::StreamStates() const {
	std::lock_guard<std::mutex> guard(lock_);
	std::vector<StreamState> out(streams_.size());
	for (size_t i = 0; i < streams_.size(); ++i) {
		const MusicStream* ms = streams_[i].get();
		if (!ms || !ms->ok) continue;
		StreamState& s = out[i];
		s.name = ms->name;
		s.offset = ms->offset;
		s.volume = ms->volume;
		s.playing = ms->playing;
		s.paused = ms->paused;
		s.loop = ms->loop;
	}
	return out;
}

bool AudioEngine::RestoreStream(int slot, const StreamState& state) {
	if (!StreamLoad(slot, state.name)) return false;
	std::lock_guard<std::mutex> guard(lock_);
	MusicStream& ms = *streams_[size_t(slot)];
	// The offset is a byte of the file, as AIL_stream_position reports it; minimp3
	// finds the next frame header from wherever decoding starts.
	ms.Rewind();
	ms.offset = std::min(state.offset, ms.file.size());
	ms.DecodeFrame();
	ms.loop = state.loop;
	ms.volume = std::max(0.f, std::min(1.f, state.volume));
	ms.paused = state.paused;
	ms.playing = state.playing && ms.ok;
	return true;
}

// ------------------------------------------------------------ saved sounds

// An AIL offset counts bytes of the file's own sample data; a cursor counts
// frames at the mixing rate.
double AudioEngine::FramesOf(const Sample& s, uint32_t bytes) const {
	if (s.srcBytesPerFrame <= 0 || s.srcRate <= 0) return 0.0;
	return double(bytes / uint32_t(s.srcBytesPerFrame)) * double(rate_) / double(s.srcRate);
}

uint32_t AudioEngine::BytesOf(const Sample& s, double frames) const {
	if (s.srcBytesPerFrame <= 0 || s.srcRate <= 0 || frames <= 0.0) return 0;
	return uint32_t(frames * double(s.srcRate) / double(rate_)) * uint32_t(s.srcBytesPerFrame);
}

std::vector<AudioEngine::VoiceState> AudioEngine::VoiceStates() const {
	std::lock_guard<std::mutex> guard(lock_);
	const uint32_t now = NowMs();
	std::vector<VoiceState> out;
	for (const Playing& p : voices_) {
		// SaveAudio skips flag 0x10; a finished sound nobody holds is freed already.
		if (!p.used || p.dontSave || !p.sample || (!p.held && !p.playing)) continue;
		VoiceState s;
		s.id = IdOf(p.key);
		s.positional = p.positional;
		s.name = p.sample->name;
		s.forget = !p.held;
		s.sameSpeed = p.sameSpeed;
		// SaveGame_PauseSounds stops what is audible into the pause set. ASSUMED: a
		// script's own Pause is a stop there (two SOUND2D natives reach Sound2D_Stop).
		s.resumes = p.playing && !p.paused;
		double cursor = p.cursor;
		int loopsLeft = p.loopsLeft;
		if (p.playing && !p.real && !Remaining(p, now, cursor, loopsLeft)) {
			if (!p.held) continue;
			s.resumes = false;
			cursor = 0.0;
			loopsLeft = p.loopsLeft;
		}
		s.loopCount = loopsLeft < 0 ? 0 : std::max(loopsLeft, 1);
		s.offset = p.playing ? BytesOf(*p.sample, cursor) : 0;
		s.volume = p.volume;
		s.speed = float(p.speed);
		s.pos = p.pos;
		s.dist1 = p.dist1;
		s.dist2 = p.dist2;
		out.push_back(s);
	}
	std::sort(out.begin(), out.end(), [](const VoiceState& a, const VoiceState& b) {
		return a.positional != b.positional ? !a.positional : a.id < b.id;
	});
	return out;
}

int AudioEngine::NextId(bool positional) const {
	std::lock_guard<std::mutex> guard(lock_);
	return nextId_[positional ? 1 : 0];
}

void AudioEngine::RestoreVoices(const std::vector<VoiceState>& voices, int next2D, int next3D) {
	// Loaded outside the lock, as Open does.
	std::vector<Sample*> samples;
	samples.reserve(voices.size());
	for (const VoiceState& s : voices) samples.push_back(stream_ && !s.name.empty() ? Load(s.name) : nullptr);

	std::lock_guard<std::mutex> guard(lock_);
	// LoadAudio starts from LoadReset: nothing from before the load survives, and
	// no pause set may resume a key a restored sound now owns.
	for (Playing& p : voices_) {
		if (p.used) Demote(p);
		p = Playing{};
	}
	slotOf_.clear();
	savedResume_.clear();
	for (auto& kv : pauseSets_) kv.second.voices.clear();
	if (voices_.size() < voices.size()) voices_.resize(voices.size());

	int next[2] = {std::max(next2D, 0), std::max(next3D, 0)};
	size_t slot = 0;
	for (size_t i = 0; i < voices.size(); ++i) {
		const VoiceState& s = voices[i];
		const Voice key = Key(s.positional, s.id);
		if (key < 0 || slotOf_.count(key)) continue;
		Playing& p = voices_[slot];
		p.key = key;
		p.sample = samples[i];
		p.positional = s.positional;
		p.used = true;
		p.held = !s.forget;
		p.sameSpeed = s.sameSpeed;
		p.loopsLeft = s.loopCount == 0 ? -1 : s.loopCount;
		p.cursor = p.sample ? FramesOf(*p.sample, s.offset) : 0.0;
		p.volume = s.volume;
		p.speed = s.speed > 0.f ? double(s.speed) : 1.0;
		p.pos = s.pos;
		p.dist1 = s.dist1;
		p.dist2 = s.dist2;
		ComputeGains(p);
		slotOf_[key] = slot++;
		int& n = next[s.positional ? 1 : 0];
		n = std::max(n, s.id + 1);
		if (s.resumes) savedResume_.push_back(key);
	}
	nextId_[0] = next[0];
	nextId_[1] = next[1];
}

void AudioEngine::ResumeSaved() {
	std::lock_guard<std::mutex> guard(lock_);
	const uint32_t now = NowMs();
	for (Voice v : savedResume_) {
		Playing* p = Resolve(v);
		if (!p || p->playing || !p->sample) continue;
		// Resume (FUN_101ecd90) plays on from the stopped offset: dating the start
		// back by it makes Remaining arrive at that cursor.
		const double framesPerMs = double(rate_) * std::max(Rate(*p), 1e-3) / 1000.0;
		p->startedMs = now - uint32_t(p->cursor / framesPerMs);
		p->playing = true;
		p->paused = false;
		TryToPlayReal(*p, now);
	}
	savedResume_.clear();
}

void AudioEngine::GetSoundProperties(const std::string& name, int& maxInstances,
		int& intervalMs) const {
	std::lock_guard<std::mutex> guard(lock_);
	maxInstances = defaultMaxInstances_;
	intervalMs = defaultIntervalMs_;
	const auto it = cache_.find(name);
	if (it != cache_.end()) {
		if (it->second.maxInstances >= 0) maxInstances = it->second.maxInstances;
		if (it->second.minIntervalMs >= 0) intervalMs = it->second.minIntervalMs;
		return;
	}
	const auto pending = pendingProps_.find(name);
	if (pending != pendingProps_.end()) {
		maxInstances = pending->second.first;
		intervalMs = pending->second.second;
	}
}

bool AudioEngine::StreamIsPlaying(int slot) const {
	std::lock_guard<std::mutex> guard(lock_);
	if (slot < 0 || size_t(slot) >= streams_.size() || !streams_[size_t(slot)]) return false;
	return streams_[size_t(slot)]->playing && !streams_[size_t(slot)]->paused;
}

size_t AudioEngine::streamsPlaying() const {
	std::lock_guard<std::mutex> guard(lock_);
	size_t n = 0;
	for (const auto& ms : streams_)
		if (ms && ms->playing && !ms->paused) ++n;
	return n;
}

// Keeps half a second of converted audio ahead of the mixer for every stream
// that is playing. Decoding happens outside the mixer lock; the SDL stream is
// its own critical section.
void AudioEngine::RefillStreams() {
	const int targetBytes = 44100 / 2 * kChannels * int(sizeof(float));
	for (size_t i = 0; i < streams_.size(); ++i) {
		MusicStream* ms = nullptr;
		{
			std::lock_guard<std::mutex> guard(lock_);
			ms = streams_[i].get();
			if (!ms || !ms->playing || ms->paused || !ms->conv) continue;
		}
		while (!ms->eof && SDL_GetAudioStreamAvailable(ms->conv) < targetBytes) {
			if (ms->DecodeFrame()) continue;
			if (ms->loop && ms->offset > 0) {
				// AIL loop count 0: back to the start without a gap.
				mp3dec_init(&ms->dec);
				ms->offset = 0;
				if (!ms->DecodeFrame()) { ms->eof = true; break; }
				continue;
			}
			// Flush BEFORE the flag: a callback that sees eof with data still
			// inside conv reads short, clears playing, and drops the tail.
			SDL_FlushAudioStream(ms->conv);
			ms->eof = true;
		}
	}
}

} // namespace painful
