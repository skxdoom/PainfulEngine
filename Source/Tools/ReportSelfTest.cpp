// Self-checks for the small types and the engine rules that are pure logic,
// run by `PainfulTools selftest`.
//
// The engine has no unit-test rig and does not need one: every subsystem is
// checked by a report against real game data. These types have no game data -
// they are pure arithmetic - so this is their report. Run it after every
// float array -> Vec3/Quat conversion. Docs/Reference/Vectors.md

#include "Commands.h"

#include "../Core/Matrix.h"
#include "../Core/Vectors.h"
#include "../Game/Input.h"

#include <cmath>

namespace {
using namespace painful;

size_t g_failed = 0;
size_t g_ran = 0;

void Ok(bool cond, const char* what) {
	++g_ran;
	if (cond) return;
	++g_failed;
	LogWarn("FAIL %s", what);
}

// Tolerant compare: these are float expressions, and an exact match would be
// testing the optimiser rather than the maths.
bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
bool Near(const Vec3& a, const Vec3& b, float eps = 1e-5f) {
	return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

void TestLayout() {
	// The whole premise of the incremental conversion: a Vec3 IS a float[3].
	Vec3 v(1.f, 2.f, 3.f);
	const float* p = v; // implicit, the read side
	Ok(p[0] == 1.f && p[1] == 2.f && p[2] == 3.f, "implicit const float* sees the members");
	v.p()[1] = 9.f;
	Ok(v.y == 9.f, "p() writes through to the member");

	float raw[3] = {4.f, 5.f, 6.f};
	Ok(AsVec3(raw) == Vec3(4.f, 5.f, 6.f), "AsVec3 reads a float[3] in place");
	AsVec3(raw).z = 7.f;
	Ok(raw[2] == 7.f, "AsVec3 writes a float[3] in place");

	float out[3] = {0, 0, 0};
	Vec3(1.f, 2.f, 3.f).Store(out);
	Ok(out[0] == 1.f && out[1] == 2.f && out[2] == 3.f, "Store fills a float[3]");

	Ok(Vec3(2.f) == Vec3(2.f, 2.f, 2.f), "the splat constructor");
	Ok(Vec3(raw) == Vec3(4.f, 5.f, 7.f), "construction from a float[3]");

	Vec3 idx;
	idx[0] = 1.f; idx[1] = 2.f; idx[2] = 3.f;
	Ok(idx == Vec3(1.f, 2.f, 3.f), "operator[] agrees with x/y/z");
}

void TestArithmetic() {
	const Vec3 a(1.f, 2.f, 3.f), b(4.f, -5.f, 6.f);
	Ok(a + b == Vec3(5.f, -3.f, 9.f), "add");
	Ok(a - b == Vec3(-3.f, 7.f, -3.f), "subtract");
	Ok(a * 2.f == Vec3(2.f, 4.f, 6.f), "scale on the right");
	Ok(2.f * a == Vec3(2.f, 4.f, 6.f), "scale on the left");
	Ok(Near(a / 2.f, Vec3(0.5f, 1.f, 1.5f)), "divide");
	Ok(-a == Vec3(-1.f, -2.f, -3.f), "negate");
	Ok(a != b, "inequality");

	Vec3 acc(1.f, 1.f, 1.f);
	acc += Vec3(1.f, 2.f, 3.f);
	acc -= Vec3(0.f, 1.f, 0.f);
	acc *= 2.f;
	Ok(acc == Vec3(4.f, 4.f, 8.f), "compound assignment");
}

void TestGeometry() {
	const Vec3 a(1.f, 2.f, 3.f), b(4.f, -5.f, 6.f);
	Ok(Near(Dot(a, b), 1.f * 4.f + 2.f * -5.f + 3.f * 6.f), "dot");
	Ok(Near(Dot(a, b), Dot(b, a)), "dot commutes");

	// Right-handed: x cross y is z, and the result is perpendicular to both.
	Ok(Cross(Vec3(1, 0, 0), Vec3(0, 1, 0)) == Vec3(0, 0, 1), "cross x,y = z");
	Ok(Cross(Vec3(0, 1, 0), Vec3(0, 0, 1)) == Vec3(1, 0, 0), "cross y,z = x");
	Ok(Cross(Vec3(0, 0, 1), Vec3(1, 0, 0)) == Vec3(0, 1, 0), "cross z,x = y");
	const Vec3 c = Cross(a, b);
	Ok(Near(Dot(c, a), 0.f, 1e-4f) && Near(Dot(c, b), 0.f, 1e-4f), "cross is perpendicular");
	Ok(Cross(a, b) == -Cross(b, a), "cross anticommutes");

	Ok(Near(Vec3(3.f, 4.f, 0.f).Length(), 5.f), "length");
	Ok(Near(Vec3(3.f, 4.f, 0.f).LengthSq(), 25.f), "length squared");
	Ok(Near(Vec3(3.f, 4.f, 0.f).Normalized().Length(), 1.f), "normalised is unit");
	Ok(Near(Distance(Vec3(1, 0, 0), Vec3(4, 4, 0)), 5.f), "distance");
	Ok(Near(DistanceSq(Vec3(1, 0, 0), Vec3(4, 4, 0)), 25.f), "distance squared");
	Ok(Near(Lerp(Vec3(0, 0, 0), Vec3(2, 4, 6), 0.5f), Vec3(1, 2, 3)), "lerp");
	Ok(Min(a, b) == Vec3(1.f, -5.f, 3.f), "component min");
	Ok(Max(a, b) == Vec3(4.f, 2.f, 6.f), "component max");
}

void TestDegenerate() {
	// The case that motivates Normalized's guard: a script divides a velocity
	// by its own length the frame the thing is standing still.
	Ok(Vec3().Normalized() == Vec3(), "normalising zero yields zero, not NaN");
	Ok(Vec3().Normalized().IsFinite(), "normalising zero stays finite");
	Vec3 z;
	Ok(z.Normalize() == 0.f, "Normalize reports zero length");
	Ok(z == Vec3(), "Normalize leaves zero alone");

	Vec3 v(3.f, 0.f, 4.f);
	Ok(Near(v.Normalize(), 5.f), "Normalize returns the old length");
	Ok(Near(v.Length(), 1.f), "Normalize leaves a unit vector");

	Ok(!Vec3(std::nanf(""), 0.f, 0.f).IsFinite(), "NaN is not finite");
	Ok(!Vec3(INFINITY, 0.f, 0.f).IsFinite(), "infinity is not finite");
	Ok(Vec3(0.f, 0.f, 0.f).IsFinite(), "zero is finite");
}

// Vec3 against the Mat4 it will be handed to: the row-vector convention has
// to survive the conversion, or every transform silently transposes.
void TestAgainstMat4() {
	Mat4 m; // identity
	m.m[12] = 10.f; m.m[13] = 20.f; m.m[14] = 30.f; // translation in row 3
	Vec3 out;
	m.TransformPoint(1.f, 2.f, 3.f, out);
	Ok(out == Vec3(11.f, 22.f, 33.f), "Mat4::TransformPoint fills a Vec3 out-parameter");

	const Vec3 in(1.f, 2.f, 3.f);
	const Vec3 byValue = m.TransformPoint(in);
	Ok(byValue == out, "the value form agrees with the out-parameter form");
}

// Quat carries the engine's (w,x,y,z) order and its qz*qy*qx composition, both
// recovered rules. It is the single authority for them, so the checks are
// against the geometry itself and against the 3x3 form the renderers use.
void TestQuat() {
	Ok(sizeof(Quat) == 16, "Quat is four packed floats");
	const Quat id;
	Ok(id.w == 1.f && id.x == 0.f && id.y == 0.f && id.z == 0.f, "default Quat is the identity");

	// Order: element 0 is W. A (x,y,z,w) library would fail this.
	const Quat q(0.5f, 1.f, 2.f, 3.f);
	Ok(q[0] == 0.5f && q[1] == 1.f && q[2] == 2.f && q[3] == 3.f, "operator[] is w,x,y,z");
	const float* raw = q;
	Ok(raw[0] == 0.5f && raw[3] == 3.f, "the implicit const float* is w-first");

	float stored[4] = {0, 0, 0, 0};
	q.Store(stored);
	Ok(stored[0] == 0.5f && stored[1] == 1.f && stored[2] == 2.f && stored[3] == 3.f,
			"Store writes w,x,y,z");
	float rawQ[4] = {0.5f, 1.f, 2.f, 3.f};
	Ok(AsQuat(rawQ) == q, "AsQuat reads a float[4] in place");

	// FromEuler is qz*qy*qx (FUN_1011bea0): a single-axis turn must put the
	// half-angle in that axis alone, and X must be the one applied first.
	const Quat rx = Quat::FromEuler(0.6f, 0.f, 0.f);
	Ok(Near(rx.w, std::cos(0.3f)) && Near(rx.x, std::sin(0.3f)) &&
			Near(rx.y, 0.f) && Near(rx.z, 0.f), "FromEuler about X alone");
	const Quat rz = Quat::FromEuler(0.f, 0.f, 0.6f);
	Ok(Near(rz.w, std::cos(0.3f)) && Near(rz.z, std::sin(0.3f)) &&
			Near(rz.x, 0.f) && Near(rz.y, 0.f), "FromEuler about Z alone");
	Ok(Near(Dot(Quat::FromEuler(0.3f, -0.7f, 1.1f),
			Quat::FromEuler(0.f, 0.f, 1.1f) * Quat::FromEuler(0.f, -0.7f, 0.f) *
			Quat::FromEuler(0.3f, 0.f, 0.f)),
			1.f, 1e-4f),
			"FromEuler(x,y,z) is qz*qy*qx");

	// 90 degrees about Y takes +X onto the Z axis.
	const Quat ry = Quat::FromEuler(0.f, kPi * 0.5f, 0.f);
	const Vec3 turned = ry.Rotate(Vec3(1.f, 0.f, 0.f));
	Ok(Near(turned.Length(), 1.f), "Quat::Rotate preserves length");
	Ok(Near(std::fabs(turned.z), 1.f, 1e-4f) && Near(turned.x, 0.f, 1e-4f),
			"Quat::Rotate takes +X onto Z for 90 degrees about Y");
	Ok(Near(Quat().Rotate(Vec3(1.f, 2.f, 3.f)), Vec3(1.f, 2.f, 3.f)),
			"the identity Quat is a no-op");

	// Composition order: a * b applies a FIRST. Rotate is conj(q) * v * q, so
	// conj(ab) v (ab) = conj(b) (conj(a) v a) b - row-vector order, the same one
	// Matrix.h states for EngineRot9Mul.
	const Quat qa = Quat::FromEuler(0.4f, 0.f, 0.f);
	const Quat qb = Quat::FromEuler(0.f, 0.9f, 0.f);
	const Vec3 v(0.3f, -1.2f, 2.f);
	Ok(Near((qa * qb).Rotate(v), qb.Rotate(qa.Rotate(v)), 1e-4f), "a * b applies a first");
	Ok(!Near((qa * qb).Rotate(v), qa.Rotate(qb.Rotate(v)), 1e-3f),
			"the two composition orders differ, so the check above discriminates");

	// The quaternion order must match the 3x3 one. Row i of a row-vector
	// rotation matrix is the image of basis vector i, so R(a*b) can be compared
	// against EngineRot9Mul(R(a), R(b)) directly.
	const auto Rot9 = [](const Quat& r, float out[9]) {
		r.Rotate(Vec3(1.f, 0.f, 0.f)).Store(out);
		r.Rotate(Vec3(0.f, 1.f, 0.f)).Store(out + 3);
		r.Rotate(Vec3(0.f, 0.f, 1.f)).Store(out + 6);
	};
	float ra[9], rb[9], rab[9], rmul[9];
	Rot9(qa, ra);
	Rot9(qb, rb);
	Rot9(qa * qb, rab);
	EngineRot9Mul(ra, rb, rmul);
	bool rot9Same = true;
	for (int i = 0; i < 9; ++i) rot9Same = rot9Same && Near(rab[i], rmul[i], 1e-4f);
	Ok(rot9Same, "Quat and EngineRot9Mul compose in the same order");

	Ok(Near(qa.Conjugate().Rotate(qa.Rotate(v)), v, 1e-4f),
			"the conjugate undoes the rotation");

	// Dot, negate, normalise: what the animation blend is built out of.
	Ok(Near(Dot(qa, qa), 1.f), "a unit Quat dots with itself to 1");
	Ok(Near(Dot(qa, -qa), -1.f), "negation flips the dot");
	Ok(Near((-qa).Rotate(v), qa.Rotate(v), 1e-4f), "q and -q are the same rotation");
	Ok(Near(Quat(0.f, 3.f, 0.f, 4.f).Length(), 5.f), "Quat::Length");
	Ok(Near(Quat(0.f, 3.f, 0.f, 4.f).Normalized().Length(), 1.f), "Normalized is unit");
	Ok(Quat(0.f, 0.f, 0.f, 0.f).Normalized() == Quat(),
			"normalising zero yields the identity, not a NaN");

	// Nlerp: the endpoints, the midpoint's unit length, and the short-way-round
	// flip, which is what stops a bone spinning most of a turn between keys.
	Ok(Near(Dot(Nlerp(qa, qb, 0.f), qa), 1.f, 1e-4f), "Nlerp at 0 is a");
	Ok(Near(Dot(Nlerp(qa, qb, 1.f), qb), 1.f, 1e-4f), "Nlerp at 1 is b");
	Ok(Near(Nlerp(qa, qb, 0.5f).Length(), 1.f), "Nlerp stays unit");
	Ok(Near(Dot(Nlerp(qa, -qb, 0.5f), Nlerp(qa, qb, 0.5f)), 1.f, 1e-4f),
			"Nlerp takes the short way round, so -b blends the same as b");
	// The flip makes the inputs non-antipodal, so only a degenerate input can
	// reach the fallback at all.
	const Quat zero(0.f, 0.f, 0.f, 0.f);
	Ok(Nlerp(zero, zero, 0.5f) == zero, "a degenerate blend falls back to a");
}

// INP.Reset consumes the keys that are down: they do not count again until
// released. The original never polls - a key only enters "pressed" on a down
// event (ProcessEvents 0x1003e670) and Reset (0x1003a6c0) zeroes its pressed
// list - so a held key cannot re-arm. We poll the window, so it is explicit.
// EndLevel:Tick depends on it: one held click is several frames, and without
// it the first click both skipped the stats crawl and took the exit.
void TestInputReset() {
	Input in;
	in.BeginFrame();
	in.SetKeyDown(1, true);
	Ok(in.IsDown(1), "a pressed key is down");

	in.Reset();
	Ok(!in.IsDown(1), "Reset clears the key");

	in.BeginFrame();
	in.SetKeyDown(1, true);          // still physically held
	Ok(!in.IsDown(1), "a key held across Reset does not come back");
	in.BeginFrame();
	in.SetKeyDown(1, true);
	Ok(!in.IsDown(1), "and stays gone however long it is held");

	in.BeginFrame();
	in.SetKeyDown(1, false);         // released
	in.BeginFrame();
	in.SetKeyDown(1, true);          // pressed again
	Ok(in.IsDown(1), "a fresh press after the release counts");

	in.Reset();
	in.BeginFrame();
	in.SetKeyDown(2, true);
	Ok(in.IsDown(2), "a key that was up during Reset is not suppressed");
}
} // namespace

int SelfTestCmd() {
	g_failed = 0;
	g_ran = 0;
	TestLayout();
	TestArithmetic();
	TestGeometry();
	TestDegenerate();
	TestAgainstMat4();
	TestQuat();
	TestInputReset();
	if (g_failed == 0) {
		LogInfo("selftest: %zu checks passed", g_ran);
		return 0;
	}
	LogWarn("selftest: %zu of %zu checks FAILED", g_failed, g_ran);
	return 1;
}


