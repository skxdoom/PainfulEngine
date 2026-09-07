#pragma once
#include "Vectors.h"

// The engine's transform convention: the 4x4 affine matrix, and the
// conversions between a row-vector 3x3 and a quaternion.
//
// Both the script layer and the asset layer speak this convention and neither
// should depend on the other, which is why it lives in Core.
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

    // The value form, which is what every caller actually wants.
    Vec3 TransformPoint(const Vec3& p) const {
        Vec3 out;
        TransformPoint(p.x, p.y, p.z, out);
        return out;
    }

    void TransformPoint(float px, float py, float pz, Vec3& out) const {
        out[0] = px * m[0] + py * m[4] + pz * m[8]  + m[12];
        out[1] = px * m[1] + py * m[5] + pz * m[9]  + m[13];
        out[2] = px * m[2] + py * m[6] + pz * m[10] + m[14];
    }
};

// Engine-order (w,x,y,z) quaternion to the row-vector 3x3 the renderers use -
// the engine's own textbook form (FUN_1000bb90), NOT transposed. Pre-transposing
// mirrored every rotation. A quaternion shorter than 1e-6 yields the identity.
void EngineQuatToRot9(const Quat& q, float out[9]);

// Row-vector 3x3 back to a quaternion - the inverse of EngineQuatToRot9, so
// that a rotation built as "row i is where local axis i lands" can be handed
// to anything that stores orientation as a quaternion.
Quat EngineRot9ToQuat(const float m[9]);

// out = a * b as row-vector matrices, i.e. apply a first and then b.
void EngineRot9Mul(const float a[9], const float b[9], float out[9]);

}  // namespace painful
