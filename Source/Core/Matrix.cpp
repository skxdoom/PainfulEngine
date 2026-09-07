#include "Matrix.h"

#include <cmath>
#include <cstring>

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

void EngineQuatToRot9(const Quat& q, float out[9]) {
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(out, identity, sizeof(identity));
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-6f) return;
    const float iw = q.w / n, ix = q.x / n, iy = q.y / n, iz = q.z / n;
    out[0] = 1 - 2 * (iy * iy + iz * iz);
    out[1] = 2 * (ix * iy - iz * iw);
    out[2] = 2 * (ix * iz + iy * iw);
    out[3] = 2 * (ix * iy + iz * iw);
    out[4] = 1 - 2 * (ix * ix + iz * iz);
    out[5] = 2 * (iy * iz - ix * iw);
    out[6] = 2 * (ix * iz - iy * iw);
    out[7] = 2 * (iy * iz + ix * iw);
    out[8] = 1 - 2 * (ix * ix + iy * iy);
}

Quat EngineRot9ToQuat(const float m[9]) {
    const float trace = m[0] + m[4] + m[8];
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        return Quat(0.25f * s, (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s);
    }
    if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2.f;
        return Quat((m[7] - m[5]) / s, 0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s);
    }
    if (m[4] > m[8]) {
        const float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2.f;
        return Quat((m[2] - m[6]) / s, (m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s);
    }
    const float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2.f;
    return Quat((m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s);
}

void EngineRot9Mul(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] +
                             a[r * 3 + 2] * b[2 * 3 + c];
}

}  // namespace painful
