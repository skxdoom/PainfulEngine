// Numeric self-checks for the small maths types, run by `PainfulTools selftest`.
//
// The engine has no unit-test rig and does not need one: every subsystem is
// checked by a report against real game data. Vec3 has no game data - it is
// pure arithmetic - so this is its report. It exists to be run after every
// float[3] -> Vec3 conversion. Docs/Reference/Vectors.md

#include "Commands.h"

#include "Core/Common.h"
#include "Core/Vec3.h"

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
    const float* p = v;                    // implicit, the read side
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
    Mat4 m;                       // identity
    m.m[12] = 10.f; m.m[13] = 20.f; m.m[14] = 30.f;   // translation in row 3
    Vec3 out;
    m.TransformPoint(1.f, 2.f, 3.f, out.p());
    Ok(out == Vec3(11.f, 22.f, 33.f), "Mat4::TransformPoint writes through Vec3::p()");

    const Vec3 in(1.f, 2.f, 3.f);
    float raw[3];
    m.TransformPoint(in.x, in.y, in.z, raw);
    Ok(AsVec3(raw) == out, "the same result either way in");
}

void TestQuaternionInterop() {
    // Vec3 must survive the engine's (w,x,y,z) rotation helpers untouched.
    float q[4];
    EngineEulerToQuat(0.f, kPi * 0.5f, 0.f, q);        // 90 degrees about Y
    Vec3 r;
    EngineQuatRotate(q, Vec3(1.f, 0.f, 0.f), r.p());
    Ok(Near(r.Length(), 1.f), "rotation preserves length");
    Ok(Near(std::fabs(r.z), 1.f, 1e-4f) && Near(r.x, 0.f, 1e-4f),
       "90 degrees about Y takes +X onto the Z axis");

    float id[4] = {1.f, 0.f, 0.f, 0.f};
    Vec3 same;
    EngineQuatRotate(id, Vec3(1.f, 2.f, 3.f), same.p());
    Ok(Near(same, Vec3(1.f, 2.f, 3.f)), "the identity quaternion is a no-op");
}

}  // namespace

int SelfTestCmd() {
    g_failed = 0;
    g_ran = 0;
    TestLayout();
    TestArithmetic();
    TestGeometry();
    TestDegenerate();
    TestAgainstMat4();
    TestQuaternionInterop();
    if (g_failed == 0) {
        LogInfo("selftest: %zu checks passed", g_ran);
        return 0;
    }
    LogWarn("selftest: %zu of %zu checks FAILED", g_failed, g_ran);
    return 1;
}


