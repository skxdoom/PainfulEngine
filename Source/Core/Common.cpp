#include "Common.h"
#include "FileSystem.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace painful {

Mat4 Mat4::InvertAffine(const Mat4& x) {
    const float* m = x.m;
    double a = m[0], b = m[1], c = m[2];
    double d = m[4], e = m[5], f = m[6];
    double g = m[8], h = m[9], i = m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det > -1e-12 && det < 1e-12) det = 1e-12;
    double id = 1.0 / det;
    Mat4 r;
    r.m[0]  = static_cast<float>((e * i - f * h) * id);
    r.m[1]  = static_cast<float>((c * h - b * i) * id);
    r.m[2]  = static_cast<float>((b * f - c * e) * id);
    r.m[3]  = 0.f;
    r.m[4]  = static_cast<float>((f * g - d * i) * id);
    r.m[5]  = static_cast<float>((a * i - c * g) * id);
    r.m[6]  = static_cast<float>((c * d - a * f) * id);
    r.m[7]  = 0.f;
    r.m[8]  = static_cast<float>((d * h - e * g) * id);
    r.m[9]  = static_cast<float>((b * g - a * h) * id);
    r.m[10] = static_cast<float>((a * e - b * d) * id);
    r.m[11] = 0.f;
    double tx = m[12], ty = m[13], tz = m[14];
    r.m[12] = static_cast<float>(-(tx * r.m[0] + ty * r.m[4] + tz * r.m[8]));
    r.m[13] = static_cast<float>(-(tx * r.m[1] + ty * r.m[5] + tz * r.m[9]));
    r.m[14] = static_cast<float>(-(tx * r.m[2] + ty * r.m[6] + tz * r.m[10]));
    r.m[15] = 1.f;
    return r;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    // Mounted .pak archives shadow loose files, matching the original
    // engine's mount order; anything they don't serve is read from disk.
    if (FileSystem::Get().ReadPakFile(path, out)) return true;

    FILE* fp = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) return false;
#else
    fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
#endif
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n < 0) { std::fclose(fp); return false; }
    out.resize(static_cast<size_t>(n));
    size_t rd = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    return rd == out.size();
}


void EngineEulerToQuat(float ax, float ay, float az, float out[4]) {
    const float cx = std::cos(ax * 0.5f), sx = std::sin(ax * 0.5f);
    const float cy = std::cos(ay * 0.5f), sy = std::sin(ay * 0.5f);
    const float cz = std::cos(az * 0.5f), sz = std::sin(az * 0.5f);
    // Verbatim from FUN_1011bea0, which is qz * qy * qx written out.
    out[0] = cz * cy * cx + sx * sz * sy;
    out[1] = cz * cy * sx - sz * sy * cx;
    out[2] = sy * cz * cx + sz * sx * cy;
    out[3] = sz * cy * cx - sy * sx * cz;
}

void EngineQuatMul(const float a[4], const float b[4], float out[4]) {
    const float w = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    const float x = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    const float y = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    const float z = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
    out[0] = w;
    out[1] = x;
    out[2] = y;
    out[3] = z;
}

// The engine rotates a vector as q^-1 * v * q, not the textbook q * v * q^-1.
// That is the same transpose the Jolt bridge already has to undo (an engine
// quaternion reaches Jolt conjugated), and it is visible in the scripts too:
// their VectorRotateByQuat is our inverse rotation and vice versa. Getting it
// the textbook way round leaves a rotation roughly fixed in world space while
// whatever it should follow turns underneath it.
void EngineQuatRotate(const float q[4], const float v[3], float out[3]) {
    const float vq[4] = {0.f, v[0], v[1], v[2]};
    const float inv[4] = {q[0], -q[1], -q[2], -q[3]};
    float t[4], r[4];
    EngineQuatMul(inv, vq, t);
    EngineQuatMul(t, q, r);
    out[0] = r[1];
    out[1] = r[2];
    out[2] = r[3];
}

void EngineRot9ToQuat(const float m[9], float out[4]) {
    const float trace = m[0] + m[4] + m[8];
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        out[0] = 0.25f * s;
        out[1] = (m[7] - m[5]) / s;
        out[2] = (m[2] - m[6]) / s;
        out[3] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2.f;
        out[0] = (m[7] - m[5]) / s;
        out[1] = 0.25f * s;
        out[2] = (m[1] + m[3]) / s;
        out[3] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2.f;
        out[0] = (m[2] - m[6]) / s;
        out[1] = (m[1] + m[3]) / s;
        out[2] = 0.25f * s;
        out[3] = (m[5] + m[7]) / s;
    } else {
        const float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2.f;
        out[0] = (m[3] - m[1]) / s;
        out[1] = (m[2] + m[6]) / s;
        out[2] = (m[5] + m[7]) / s;
        out[3] = 0.25f * s;
    }
}

void EngineRot9Mul(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] +
                             a[r * 3 + 2] * b[2 * 3 + c];
}


bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    FILE* fp = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || !fp) return false;
#else
    fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
#endif
    const bool ok = data.empty() ||
                    std::fwrite(data.data(), 1, data.size(), fp) == data.size();
    std::fclose(fp);
    return ok;
}
} // namespace painful
