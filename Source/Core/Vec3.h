#pragma once
#include <cmath>
#include <type_traits>

// A 3-vector laid out exactly as `float[3]`.
//
// The engine passes positions and normals as raw `float[3]` because that is
// what the file formats, the Lua stack, Jolt and bgfx all speak. Vec3 keeps
// that layout and converts implicitly to `const float*`, so it can be adopted
// one function at a time without touching those boundaries; `.p()` hands out
// the mutable pointer an out-parameter wants, and stays explicit so a write
// through it is visible. Docs/Reference/Vectors.md
namespace painful {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    constexpr Vec3() = default;
    constexpr Vec3(float ax, float ay, float az) : x(ax), y(ay), z(az) {}
    constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
    explicit Vec3(const float a[3]) : x(a[0]), y(a[1]), z(a[2]) {}

    // The layout promise, relied on by p() and by every reinterpret at a
    // format or library boundary. Checked below the class.
    float* p() { return &x; }
    const float* p() const { return &x; }
    operator const float*() const { return &x; }

    // Branching rather than (&x)[i]: same code after optimisation, and it does
    // not depend on the layout promise the way p() does.
    float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    const float& operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }

    void Store(float out[3]) const { out[0] = x; out[1] = y; out[2] = z; }

    Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
    Vec3& operator*=(float s)       { x *= s;   y *= s;   z *= s;   return *this; }
    Vec3& operator/=(float s)       { return *this *= (1.f / s); }

    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    // Zero length yields the zero vector rather than a NaN: a script can
    // divide a velocity by its own length the frame it is standing still, and
    // a NaN reaching the solver takes the process down.
    Vec3 Normalized() const {
        const float n = Length();
        return n > 0.f ? *this * (1.f / n) : Vec3();
    }
    // The length before normalising, for callers that want both.
    float Normalize() {
        const float n = Length();
        if (n > 0.f) *this *= 1.f / n;
        else         *this = Vec3();
        return n;
    }

    bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

    friend Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
    friend Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
    friend Vec3 operator*(Vec3 a, float s)       { return a *= s; }
    friend Vec3 operator*(float s, Vec3 a)       { return a *= s; }
    friend Vec3 operator/(Vec3 a, float s)       { return a /= s; }
    friend Vec3 operator-(const Vec3& a)         { return Vec3(-a.x, -a.y, -a.z); }
    // Exact, so it answers "is this the same value", not "is it close".
    friend bool operator==(const Vec3& a, const Vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    friend bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }
};

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline float Distance(const Vec3& a, const Vec3& b) { return (a - b).Length(); }
inline float DistanceSq(const Vec3& a, const Vec3& b) { return (a - b).LengthSq(); }

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 Min(const Vec3& a, const Vec3& b) {
    return Vec3(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z);
}
inline Vec3 Max(const Vec3& a, const Vec3& b) {
    return Vec3(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z);
}

// What p() and every boundary cast depend on. If any of these ever fail, the
// type is no longer a drop-in for float[3] and the conversions must go.
static_assert(sizeof(Vec3) == 3 * sizeof(float), "Vec3 must be three packed floats");
static_assert(alignof(Vec3) == alignof(float), "Vec3 must not be over-aligned");
static_assert(std::is_standard_layout_v<Vec3>, "Vec3 must be standard layout");
static_assert(std::is_trivially_copyable_v<Vec3>, "Vec3 must be memcpy-able");

// A float[3] read as a Vec3 without copying. Both directions, because the
// asset and physics layers hand out arrays the caller wants to do maths on.
inline const Vec3& AsVec3(const float a[3]) { return *reinterpret_cast<const Vec3*>(a); }
inline Vec3& AsVec3(float a[3]) { return *reinterpret_cast<Vec3*>(a); }

}  // namespace painful
