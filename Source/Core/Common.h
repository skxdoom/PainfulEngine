#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace painful {

// PainEngine stores 4x4 affine matrices ROW-MAJOR in ROW-VECTOR convention
// (v' = v * M), with the translation in row 3. Note that glTF's column-major
// column-vector layout is the transpose, which means the same 16 floats can be
// handed to glTF verbatim - no transposition is needed.
struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    float&       operator[](int i)       { return m[i]; }
    const float& operator[](int i) const { return m[i]; }

    static Mat4 Mul(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
                r.m[i * 4 + j] = static_cast<float>(s);
            }
        return r;
    }

    // Inverse of an affine matrix [R 0; t 1] -> [R^-1 0; -t*R^-1 1].
    static Mat4 InvertAffine(const Mat4& x);

    void TransformPoint(float px, float py, float pz, float out[3]) const {
        out[0] = px * m[0] + py * m[4] + pz * m[8]  + m[12];
        out[1] = px * m[1] + py * m[5] + pz * m[9]  + m[13];
        out[2] = px * m[2] + py * m[6] + pz * m[10] + m[14];
    }
};

// Little-endian readers over an in-memory buffer. All PainEngine formats are
// byte-packed with no alignment padding, so everything must be read
// sequentially rather than by computed offsets into aligned structs.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : d_(data), n_(size) {}

    size_t pos() const { return p_; }
    size_t size() const { return n_; }
    void seek(size_t p) { p_ = p; }
    bool ok(size_t need) const { return p_ + need <= n_; }
    const uint8_t* raw() const { return d_; }

    uint32_t u32() { uint32_t v; std::memcpy(&v, d_ + p_, 4); p_ += 4; return v; }
    uint16_t u16() { uint16_t v; std::memcpy(&v, d_ + p_, 2); p_ += 2; return v; }
    uint8_t  u8()  { return d_[p_++]; }
    float    f32() { float v; std::memcpy(&v, d_ + p_, 4); p_ += 4; return v; }

    uint32_t peekU32(size_t at) const {
        uint32_t v; std::memcpy(&v, d_ + at, 4); return v;
    }
    uint16_t peekU16(size_t at) const {
        uint16_t v; std::memcpy(&v, d_ + at, 2); return v;
    }
    float peekF32(size_t at) const {
        float v; std::memcpy(&v, d_ + at, 4); return v;
    }

    void readMat4(Mat4& out) { for (int i = 0; i < 16; ++i) out.m[i] = f32(); }

private:
    const uint8_t* d_;
    size_t n_;
    size_t p_ = 0;
};

bool ReadFile(const std::string& path, std::vector<uint8_t>& out);

// --- the engine's rotation convention -------------------------------------
//
// Quaternions are engine order, (w, x, y, z). These live in Core because both
// the script layer and the asset layer speak this convention and neither
// should depend on the other.

// Euler angles (radians) to a quaternion, composed the way the engine does
// it: qz * qy * qx, so X is applied first. Read out of the native behind
// 0x1011C390, whose maths is FUN_1011bea0. Single authority - the
// EulerToQuat the scripts call comes through here.
void EngineEulerToQuat(float ax, float ay, float az, float out[4]);

// out = a * b. Rotations compose left of right, so a * b applies b first.
void EngineQuatMul(const float a[4], const float b[4], float out[4]);

// Rotates a vector by a quaternion.
void EngineQuatRotate(const float q[4], const float v[3], float out[3]);

// Row-vector 3x3 back to a quaternion - the inverse of EngineQuatToRot9, so
// that a rotation built as "row i is where local axis i lands" can be handed
// to anything that stores orientation as a quaternion.
void EngineRot9ToQuat(const float m[9], float out[4]);

// out = a * b as row-vector matrices, i.e. apply a first and then b.
void EngineRot9Mul(const float a[9], const float b[9], float out[9]);

} // namespace painful
