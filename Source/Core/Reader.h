#pragma once
#include <cstdint>
#include <cstring>

#include "Matrix.h" // Reader::readMat4

// Little-endian reads over an in-memory buffer. All PainEngine formats are
// byte-packed with no alignment padding, so everything must be read
// sequentially rather than by computed offsets into aligned structs.
//
// A read past the end yields zero and latches `overran()`, so a count out of a
// truncated file cannot send a loop off the buffer. Latched, not per-field: a
// parser checks once at the end. Docs/Reference/Diagnostics.md
namespace painful {

class Reader {
public:
	Reader(const uint8_t* data, size_t size) : d_(data), n_(size) {}

	size_t pos() const { return p_; }
	size_t size() const { return n_; }
	void seek(size_t p) { p_ = p; }
	bool ok(size_t need) const { return p_ + need <= n_; }
	const uint8_t* raw() const { return d_; }
	// Any read so far fell outside the buffer.
	bool overran() const { return overran_; }

	uint32_t u32() { uint32_t v = peekU32(p_); p_ += 4; return v; }
	uint16_t u16() { uint16_t v = peekU16(p_); p_ += 2; return v; }
	uint8_t u8() { return InRange(p_, 1) ? d_[p_++] : (++p_, uint8_t(0)); }
	float f32() { float v = peekF32(p_); p_ += 4; return v; }

	uint32_t peekU32(size_t at) const {
		if (!InRange(at, 4)) return 0;
		uint32_t v; std::memcpy(&v, d_ + at, 4); return v;
	}
	uint16_t peekU16(size_t at) const {
		if (!InRange(at, 2)) return 0;
		uint16_t v; std::memcpy(&v, d_ + at, 2); return v;
	}
	float peekF32(size_t at) const {
		if (!InRange(at, 4)) return 0.f;
		float v; std::memcpy(&v, d_ + at, 4); return v;
	}

	void readMat4(Mat4& out) { for (int i = 0; i < 16; ++i) out.m[i] = f32(); }

private:
	bool InRange(size_t at, size_t need) const {
		if (at <= n_ && need <= n_ - at) return true;
		overran_ = true;
		return false;
	}

	const uint8_t* d_;
	size_t n_;
	size_t p_ = 0;
	mutable bool overran_ = false;
};

} // namespace painful
