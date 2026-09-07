#pragma once
#include <cmath>
#include <type_traits>

// The engine's vector types: Vec3 now, Quat beside it.
//
// A 3-vector laid out exactly as `float[3]`.
//
// The engine passes positions and normals as raw `float[3]` because that is
// what the file formats, the Lua stack, Jolt and bgfx all speak. Vec3 keeps
// that layout and converts implicitly to `const float*`, so it can be adopted
// one function at a time without touching those boundaries; `.p()` hands out
// the mutable pointer an out-parameter wants, and stays explicit so a write
// through it is visible. Docs/Reference/Vectors.md
namespace painful {

inline constexpr float kPi = 3.14159265358979f;

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
	Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
	Vec3& operator/=(float s) { return *this *= (1.f / s); }

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
		else *this = Vec3();
		return n;
	}

	bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

	friend Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
	friend Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
	friend Vec3 operator*(Vec3 a, float s) { return a *= s; }
	friend Vec3 operator*(float s, Vec3 a) { return a *= s; }
	friend Vec3 operator/(Vec3 a, float s) { return a /= s; }
	friend Vec3 operator-(const Vec3& a) { return Vec3(-a.x, -a.y, -a.z); }
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



// The engine's rotation, in ENGINE ORDER (w, x, y, z) - not the (x,y,z,w) most
// libraries use, and the source of the port's most persistent class of bug.
// The type exists mainly so the order and the composition rule are enforced by
// the compiler rather than by comments: a bgfx uniform or a UV transform is
// also four floats, and used to be indistinguishable from a rotation.
//
// Layout-compatible with float[4], the same way Vec3 is with float[3], so it
// still meets the Lua stack, the .pkmdl/.ani files and Jolt where they speak
// arrays. Docs/Reference/Vectors.md
struct Quat {
	float w = 1.f, x = 0.f, y = 0.f, z = 0.f; // identity

	constexpr Quat() = default;
	constexpr Quat(float aw, float ax, float ay, float az) : w(aw), x(ax), y(ay), z(az) {}
	explicit Quat(const float a[4]) : w(a[0]), x(a[1]), y(a[2]), z(a[3]) {}

	float* p() { return &w; }
	const float* p() const { return &w; }
	operator const float*() const { return &w; }

	float& operator[](int i) { return i == 0 ? w : i == 1 ? x : i == 2 ? y : z; }
	const float& operator[](int i) const { return i == 0 ? w : i == 1 ? x : i == 2 ? y : z; }

	void Store(float out[4]) const { out[0] = w; out[1] = x; out[2] = y; out[3] = z; }

	// Euler angles (radians) composed the way the engine does it: qz * qy * qx,
	// so X is applied first. Read out of the native behind 0x1011C390, whose
	// maths is FUN_1011bea0.
	static Quat FromEuler(float ax, float ay, float az);

	// The conjugate. Unit quaternions only, which every rotation here is.
	Quat Conjugate() const { return Quat(w, -x, -y, -z); }

	// Rotates a vector. conj(q) * v * q, the order the engine uses.
	Vec3 Rotate(const Vec3& v) const;

	float Length() const;
	// Zero length yields the identity, not a NaN - the same rule Vec3 follows.
	Quat Normalized() const;

	friend bool operator==(const Quat& a, const Quat& b) {
		return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
	}
	friend bool operator!=(const Quat& a, const Quat& b) { return !(a == b); }
	friend Quat operator-(const Quat& q) { return Quat(-q.w, -q.x, -q.y, -q.z); }
};

// a * b applies a first: Rotate is conj(q) * v * q, so this is row-vector
// order, the same one EngineRot9Mul uses.
Quat operator*(const Quat& a, const Quat& b);

inline float Dot(const Quat& a, const Quat& b) {
	return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

// Normalised lerp, taking the short way round - q and -q are the same rotation,
// and without the flip a blend spins a bone most of a turn between two keys.
// Degenerate result falls back to `a`. Docs/Reference/Vectors.md
Quat Nlerp(const Quat& a, const Quat& b, float u);

static_assert(sizeof(Quat) == 4 * sizeof(float), "Quat must be four packed floats");
static_assert(alignof(Quat) == alignof(float), "Quat must not be over-aligned");
static_assert(std::is_standard_layout_v<Quat>, "Quat must be standard layout");
static_assert(std::is_trivially_copyable_v<Quat>, "Quat must be memcpy-able");

// A float[4] read as a Quat without copying - for the file formats and the
// Lua stack, which hand out arrays.
inline const Quat& AsQuat(const float a[4]) { return *reinterpret_cast<const Quat*>(a); }
inline Quat& AsQuat(float a[4]) { return *reinterpret_cast<Quat*>(a); }

} // namespace painful
