#pragma once
#include "../Core/Common.h"

namespace painful {

// View frustum as six inward-facing planes, extracted from a row-vector
// view*projection matrix (clip = v * V * P, the convention everywhere in this
// engine). Gribb-Hartmann: each plane is a combination of matrix COLUMNS.
struct Frustum {
    float plane[6][4];        // nx, ny, nz, d - inside is dot(n,p)+d >= 0

    static Frustum FromViewProj(const float view[16], const float proj[16]) {
        Mat4 a, b;
        for (int i = 0; i < 16; ++i) { a.m[i] = view[i]; b.m[i] = proj[i]; }
        const Mat4 mm = Mat4::Mul(a, b);
        const float* m = mm.m;
        Frustum f;
        auto set = [&](int i, float a0, float a1, float a2, float a3) {
            f.plane[i][0] = a0; f.plane[i][1] = a1; f.plane[i][2] = a2; f.plane[i][3] = a3;
        };
        set(0, m[3] + m[0], m[7] + m[4], m[11] + m[8],  m[15] + m[12]);   // left
        set(1, m[3] - m[0], m[7] - m[4], m[11] - m[8],  m[15] - m[12]);   // right
        set(2, m[3] + m[1], m[7] + m[5], m[11] + m[9],  m[15] + m[13]);   // bottom
        set(3, m[3] - m[1], m[7] - m[5], m[11] - m[9],  m[15] - m[13]);   // top
        set(4, m[2],        m[6],        m[10],         m[14]);           // near (D3D z>=0)
        set(5, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);   // far
        return f;
    }

    // Conservative AABB test: a box is culled only when it is fully outside
    // one plane (the classic positive-vertex test).
    bool VisibleAabb(const float lo[3], const float hi[3]) const {
        for (int i = 0; i < 6; ++i) {
            const float* p = plane[i];
            const float x = p[0] >= 0.f ? hi[0] : lo[0];
            const float y = p[1] >= 0.f ? hi[1] : lo[1];
            const float z = p[2] >= 0.f ? hi[2] : lo[2];
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.f) return false;
        }
        return true;
    }
};

} // namespace painful
