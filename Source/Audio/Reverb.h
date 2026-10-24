#pragma once
#include <vector>

namespace painful {

// Miles' software reverb on the digital master (mss32.dll): what
// SOUND.SetRoomType selects through AIL_set_digital_master_room_type. A room
// is one of 26 presets - reverb time, damping, predelay and the master wet
// level. The wet sends, already summed to mono, go through the predelay, a
// two-pole low-pass for the damping, four combs in parallel and two allpasses
// in series, and the result is added to both channels. Docs/Reference/Sound.md,
// "Reverb"
class Reverb {
public:
	void Init(int sampleRate);
	// 0 is no room (wet 0); 1..25 the presets; anything else is 0.
	void SetRoom(int type);
	int room() const { return room_; }
	// The preset's wet level: what every send is scaled by.
	float masterWet() const { return masterWet_; }
	// Adds the reverb of `wet` (one mono send per frame) to `out` (stereo,
	// interleaved).
	void Process(const float* wet, float* out, int frames);

private:
	struct Line {
		std::vector<float> buf;
		size_t pos = 0;
	};
	static void Size(Line& line, int samples);

	int rate_ = 44100;
	int room_ = 0;
	float masterWet_ = 0.f;
	int predelay_ = 0; // samples
	float combGain_[4] = {};
	float apGain_[2] = {};
	float a0_ = 0.f, b1_ = 0.f, b2_ = 0.f; // the damping low-pass
	float x1_ = 0.f, x2_ = 0.f, y1_ = 0.f, y2_ = 0.f;
	Line pre_;
	Line comb_[4];
	Line ap_[2];
};

} // namespace painful
