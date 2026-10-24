#include "Reverb.h"

#include <algorithm>
#include <cmath>

namespace painful {

namespace {

// AIL_set_digital_master_room_type's table (mss32 0x21155A10): wet level,
// reverb time, damping, predelay, by room type.
struct Preset {
	float wet, time, damping, predelay;
};
constexpr Preset kPresets[26] = {
	{0.f, 1.493f, 0.5f, 0.011f}, {0.25f, 0.1f, 0.1f, 0.002f},
	{0.417f, 0.4f, 0.666f, 0.003f}, {0.75f, 1.499f, 0.166f, 0.011f},
	{0.208f, 0.478f, 0.1f, 0.004f}, {0.3f, 2.309f, 0.68f, 0.017f},
	{0.403f, 4.279f, 0.5f, 0.03f}, {0.5f, 3.961f, 0.5f, 0.029f},
	{0.5f, 2.886f, 1.304f, 0.022f}, {0.361f, 7.284f, 0.332f, 0.03f},
	{0.5f, 10.f, 0.3f, 0.03f}, {0.153f, 0.259f, 0.55f, 0.03f},
	{0.361f, 1.493f, 0.5f, 0.011f}, {0.444f, 2.697f, 0.638f, 0.02f},
	{0.25f, 1.752f, 0.776f, 0.011f}, {0.111f, 3.145f, 0.472f, 0.088f},
	{0.111f, 2.767f, 0.224f, 0.011f}, {0.2f, 0.8f, 0.2f, 0.3f},
	{0.55f, 3.f, 0.55f, 0.043f}, {0.12f, 0.8f, 0.18f, 0.1f},
	{0.208f, 1.652f, 1.5f, 0.012f}, {0.652f, 2.886f, 0.25f, 0.021f},
	{1.f, 1.499f, 0.1f, 0.011f}, {0.875f, 8.392f, 1.388f, 0.011f},
	{0.139f, 10.f, 1.5f, 0.011f}, {0.486f, 7.563f, 2.5f, 0.011f},
};

// The line lengths in seconds (0x21143A68): the combs, then the allpasses.
constexpr float kCombDelay[4] = {0.0297f, 0.0371f, 0.0411f, 0.0437f};
constexpr float kApDelay[2] = {0.005f, 0.0017f};
// The allpass gains are 0.001 raised to these, fixed (0x2114BF40 / BF38).
constexpr double kApExponent[2] = {0.05162435823280926, 0.051634066746556816};
// The damping's cutoff is damping / 8 of the driver rate, and the game opens
// its driver at 44100 (the MilesEngine constructor): held in hertz here.
constexpr float kDriverRate = 44100.f;
// The longest predelay any preset asks for.
constexpr float kMaxPredelay = 0.3f;
// FUN_2112AE10's guard against denormals in the low-pass, on Miles' 16-bit
// scale; the port mixes in -1..1, so the same share of full scale.
constexpr float kDenormal = 2.3841858e-6f / 32768.f;

} // namespace

void Reverb::Size(Line& line, int samples) {
	line.buf.assign(size_t(std::max(samples, 1)), 0.f);
	line.pos = 0;
}

void Reverb::Init(int sampleRate) {
	rate_ = sampleRate > 0 ? sampleRate : 44100;
	Size(pre_, int(std::ceil(kMaxPredelay * float(rate_))) + 1);
	for (int i = 0; i < 4; ++i) Size(comb_[i], int(std::lround(kCombDelay[i] * float(rate_))));
	for (int i = 0; i < 2; ++i) Size(ap_[i], int(std::lround(kApDelay[i] * float(rate_))));
	x1_ = x2_ = y1_ = y2_ = 0.f;
	const int room = room_;
	room_ = -1;
	SetRoom(room);
}

// AIL_set_digital_master_room_type (mss32 0x21115AE0) -> set_digital_master_reverb
// (FUN_2112B120) and FUN_2112AD00's coefficients.
void Reverb::SetRoom(int type) {
	if (type < 0 || type > 25) type = 0;
	if (type == room_) return;
	room_ = type;
	const Preset& p = kPresets[type];
	masterWet_ = p.wet;
	predelay_ = std::min(int(std::lround(p.predelay * float(rate_))), int(pre_.buf.size()) - 1);
	// A comb loses 60 dB over the reverb time.
	for (int i = 0; i < 4; ++i)
		combGain_[i] = float(std::pow(0.001, double(kCombDelay[i]) / double(p.time)));
	for (int i = 0; i < 2; ++i) apGain_[i] = float(std::pow(0.001, kApExponent[i]));
	// The two-pole Butterworth low-pass, bilinear, at damping * rate / 8.
	if (p.damping < 1e-4f) {
		a0_ = b1_ = b2_ = 0.f;
		return;
	}
	const float cutoff = std::min(p.damping * kDriverRate / 8.f, 0.49f * float(rate_));
	const float c = 1.f / std::tan(3.14159265f * cutoff / float(rate_));
	const float r2 = std::sqrt(2.f);
	a0_ = 1.f / (c * c + r2 * c + 1.f);
	b1_ = 2.f * (1.f - c * c) * a0_;
	b2_ = (1.f - r2 * c + c * c) * a0_;
}

// FUN_2112AE10, per frame: the predelayed send through the low-pass, the
// combs (each buf = gain * buf + in, summed), the first allpass, then the
// second, entered at 0.25 for the left channel and at 0.15 for the right.
void Reverb::Process(const float* wet, float* out, int frames) {
	if (masterWet_ <= 0.f) return;
	const size_t preSize = pre_.buf.size();
	for (int f = 0; f < frames; ++f) {
		pre_.buf[pre_.pos] = wet[f];
		const size_t readAt = (pre_.pos + preSize - size_t(predelay_)) % preSize;
		const float x = pre_.buf[readAt];
		pre_.pos = (pre_.pos + 1) % preSize;

		const float y = a0_ * (x + 2.f * x1_ + x2_) - b1_ * y1_ - b2_ * y2_ + kDenormal;
		x2_ = x1_;
		x1_ = x;
		y2_ = y1_;
		y1_ = y;

		float sum = 0.f;
		for (int i = 0; i < 4; ++i) {
			Line& c = comb_[i];
			const float d = c.buf[c.pos];
			c.buf[c.pos] = combGain_[i] * d + y;
			c.pos = (c.pos + 1) % c.buf.size();
			sum += d;
		}

		Line& a = ap_[0];
		const float d1 = a.buf[a.pos];
		const float w1 = apGain_[0] * d1 + sum;
		a.buf[a.pos] = w1;
		a.pos = (a.pos + 1) % a.buf.size();
		const float out1 = d1 - apGain_[0] * w1;

		Line& b = ap_[1];
		const float d2 = b.buf[b.pos];
		const float w2 = out1 * 0.25f + apGain_[1] * d2;
		b.buf[b.pos] = w2;
		b.pos = (b.pos + 1) % b.buf.size();
		out[f * 2 + 0] += d2 - w2 * apGain_[1];
		out[f * 2 + 1] += d2 - (out1 * 0.15f + apGain_[1] * d2) * apGain_[1];
	}
}

} // namespace painful
