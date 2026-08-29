#include "Skeleton.h"
#include "Properties.h"
#include "../Core/Common.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

namespace painful {

void BuildHierarchy(std::vector<Bone>& bones) {
    std::vector<std::pair<int, int>> stack;   // {index, remaining children}
    for (size_t i = 0; i < bones.size(); ++i) {
        bones[i].parent = -1;
        if (!stack.empty()) {
            bones[i].parent = stack.back().first;
            stack.back().second -= 1;
            while (!stack.empty() && stack.back().second <= 0) stack.pop_back();
        }
        if (bones[i].childCount > 0)
            stack.emplace_back(static_cast<int>(i), static_cast<int>(bones[i].childCount));
    }
}

void ComputeBindWorld(const std::vector<Bone>& bones,
                      std::vector<Mat4>& bindWorld,
                      std::vector<Mat4>& inverseBind) {
    bindWorld.assign(bones.size(), Mat4{});
    inverseBind.assign(bones.size(), Mat4{});
    for (size_t i = 0; i < bones.size(); ++i) {
        int par = bones[i].parent;
        bindWorld[i] = (par >= 0) ? Mat4::Mul(bones[i].bind, bindWorld[par]) : bones[i].bind;
        inverseBind[i] = Mat4::InvertAffine(bindWorld[i]);
    }
}

void ComputeSkinningMatrices(const std::vector<Bone>& bones,
                             const std::vector<Mat4>& inverseBind,
                             const Animation& anim,
                             int keyIndex,
                             std::vector<Mat4>& outSkin) {
    std::unordered_map<std::string, const AnimTrack*> byName;
    for (const AnimTrack& t : anim.tracks) byName[t.name] = &t;

    std::vector<Mat4> animWorld(bones.size());
    outSkin.assign(bones.size(), Mat4{});
    for (size_t i = 0; i < bones.size(); ++i) {
        Mat4 local = bones[i].bind;
        auto it = byName.find(bones[i].name);
        if (it != byName.end() && !it->second->keys.empty()) {
            const std::vector<AnimKey>& keys = it->second->keys;
            size_t k = static_cast<size_t>(keyIndex) < keys.size()
                     ? static_cast<size_t>(keyIndex) : keys.size() - 1;
            local = keys[k].pose;
        }
        int par = bones[i].parent;
        animWorld[i] = (par >= 0) ? Mat4::Mul(local, animWorld[par]) : local;
        outSkin[i] = Mat4::Mul(inverseBind[i], animWorld[i]);
    }
}

void SkinMesh(const ModelMesh& mesh,
              const std::vector<Mat4>& skin,
              std::vector<float>& outPositions) {
    size_t vc = mesh.vertexCount();
    outPositions.assign(vc * 3, 0.f);
    if (!mesh.hasSkin()) {
        for (size_t i = 0; i < vc; ++i) {
            outPositions[i * 3 + 0] = mesh.verts[i * 8 + 0];
            outPositions[i * 3 + 1] = mesh.verts[i * 8 + 1];
            outPositions[i * 3 + 2] = mesh.verts[i * 8 + 2];
        }
        return;
    }
    for (size_t i = 0; i < vc; ++i) {
        double px = 0, py = 0, pz = 0;
        float x = mesh.verts[i * 8 + 0];
        float y = mesh.verts[i * 8 + 1];
        float z = mesh.verts[i * 8 + 2];
        for (const SkinInfluence& inf : mesh.skin[i]) {
            if (inf.bone >= skin.size()) continue;
            float t[3];
            skin[inf.bone].TransformPoint(x, y, z, t);
            px += static_cast<double>(t[0]) * inf.weight;
            py += static_cast<double>(t[1]) * inf.weight;
            pz += static_cast<double>(t[2]) * inf.weight;
        }
        outPositions[i * 3 + 0] = static_cast<float>(px);
        outPositions[i * 3 + 1] = static_cast<float>(py);
        outPositions[i * 3 + 2] = static_cast<float>(pz);
    }
}


void ResolveAnimTracks(const std::vector<Bone>& bones, const Animation& anim,
                       std::vector<const AnimTrack*>& outTracks) {
    std::unordered_map<std::string, const AnimTrack*> byName;
    for (const AnimTrack& t : anim.tracks) byName[t.name] = &t;

    outTracks.assign(bones.size(), nullptr);
    for (size_t i = 0; i < bones.size(); ++i) {
        auto it = byName.find(bones[i].name);
        if (it != byName.end() && !it->second->keys.empty()) outTracks[i] = it->second;
    }
}

namespace {

// Blends two key poses. A key holds a whole parent-relative matrix rather than
// a translation/rotation/scale triple, so the rotation has to be recovered
// before it can be blended: lerping the matrices entry by entry shrinks a bone
// as it turns and shears it halfway through a large rotation.
//
// The rows of the 3x3 are the basis vectors (row-vector convention, matching
// MakeTransform), so their lengths carry any per-axis scale. Those lerp
// linearly; what is left is a pure rotation, which goes through the
// quaternion. .ani stores about 25-30 keys a second, so this is the difference
// between animation that steps and animation that moves.
Mat4 BlendPose(const Mat4& a, const Mat4& b, float u) {
    float ra[9], rb[9], sa[3], sb[3];
    for (int r = 0; r < 3; ++r) {
        const float* rowA = &a.m[r * 4];
        const float* rowB = &b.m[r * 4];
        sa[r] = std::sqrt(rowA[0]*rowA[0] + rowA[1]*rowA[1] + rowA[2]*rowA[2]);
        sb[r] = std::sqrt(rowB[0]*rowB[0] + rowB[1]*rowB[1] + rowB[2]*rowB[2]);
        const float ia = sa[r] > 1e-8f ? 1.f / sa[r] : 0.f;
        const float ib = sb[r] > 1e-8f ? 1.f / sb[r] : 0.f;
        for (int c = 0; c < 3; ++c) {
            ra[r * 3 + c] = rowA[c] * ia;
            rb[r * 3 + c] = rowB[c] * ib;
        }
    }

    float qa[4], qb[4];
    EngineRot9ToQuat(ra, qa);
    EngineRot9ToQuat(rb, qb);

    // q and -q are the same rotation; without this the blend can take the long
    // way round and spin a bone through most of a turn between two keys.
    float dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
    if (dot < 0.f) { for (int c = 0; c < 4; ++c) qb[c] = -qb[c]; dot = -dot; }

    // Normalised lerp. Between keys a thirtieth of a second apart the angle is
    // small enough that its speed differs from a slerp's below what a frame
    // can show, and it cannot divide by a vanishing sine.
    float q[4];
    float len = 0.f;
    for (int c = 0; c < 4; ++c) {
        q[c] = qa[c] + (qb[c] - qa[c]) * u;
        len += q[c] * q[c];
    }
    len = std::sqrt(len);
    if (len > 1e-8f) { for (int c = 0; c < 4; ++c) q[c] /= len; }
    else             { for (int c = 0; c < 4; ++c) q[c] = qa[c]; }

    float rot[9];
    EngineQuatToRot9(q, rot);

    Mat4 out;
    for (int r = 0; r < 3; ++r) {
        const float scale = sa[r] + (sb[r] - sa[r]) * u;
        for (int c = 0; c < 3; ++c) out.m[r * 4 + c] = rot[r * 3 + c] * scale;
        out.m[r * 4 + 3] = 0.f;
    }
    for (int c = 0; c < 3; ++c)
        out.m[12 + c] = a.m[12 + c] + (b.m[12 + c] - a.m[12 + c]) * u;
    out.m[15] = 1.f;
    return out;
}

} // namespace

void ComputeBoneWorldAtTime(const std::vector<Bone>& bones,
                            const std::vector<const AnimTrack*>& tracks,
                            float time,
                            std::vector<Mat4>& outWorld,
                            const JointOverride* overrides,
                            size_t overrideCount) {
    outWorld.assign(bones.size(), Mat4{});
    for (size_t i = 0; i < bones.size(); ++i) {
        // A bone the animation does not drive keeps its bind pose, which is
        // what leaves an unanimated arm attached instead of at the origin.
        Mat4 local = bones[i].bind;
        const AnimTrack* track = i < tracks.size() ? tracks[i] : nullptr;
        if (track) {
            const std::vector<AnimKey>& keys = track->keys;
            size_t k = 0;
            while (k + 1 < keys.size() && keys[k + 1].time <= time) ++k;
            if (k + 1 < keys.size()) {
                const float span = keys[k + 1].time - keys[k].time;
                const float u = span > 1e-6f
                                    ? std::min(1.f, std::max(0.f, (time - keys[k].time) / span))
                                    : 0.f;
                local = BlendPose(keys[k].pose, keys[k + 1].pose, u);
            } else {
                local = keys[k].pose;      // past the last key, hold it
            }
        }
        // A script's own rotation on top of the animation. `local` maps this
        // bone's space into its parent's, so pre-multiplying applies the turn
        // in the BONE's frame - the head turns where it sits. Post-multiplying
        // would apply it in the parent's frame and swing the head around the
        // neck instead.
        for (size_t o = 0; o < overrideCount; ++o) {
            if (overrides[o].bone != int(i)) continue;
            const float* e = overrides[o].euler;
            if (e[0] == 0.f && e[1] == 0.f && e[2] == 0.f) break;
            float q[4], rot[9];
            EngineEulerToQuat(e[0], e[1], e[2], q);
            EngineQuatToRot9(q, rot);
            Mat4 r;
            for (int rr = 0; rr < 3; ++rr)
                for (int cc = 0; cc < 3; ++cc) r.m[rr * 4 + cc] = rot[rr * 3 + cc];
            local = Mat4::Mul(r, local);
            break;
        }

        // Bones are stored in preorder, so a parent is always already done.
        const int par = bones[i].parent;
        outWorld[i] = (par >= 0) ? Mat4::Mul(local, outWorld[par]) : local;
    }
}

void BoneWorldToSkinning(const std::vector<Mat4>& inverseBind,
                         const std::vector<Mat4>& boneWorld,
                         std::vector<Mat4>& outSkin) {
    const size_t n = boneWorld.size() < inverseBind.size() ? boneWorld.size()
                                                           : inverseBind.size();
    outSkin.assign(boneWorld.size(), Mat4{});
    for (size_t i = 0; i < n; ++i)
        outSkin[i] = Mat4::Mul(inverseBind[i], boneWorld[i]);
}

void ComputeSkinningMatricesAtTime(const std::vector<Bone>& bones,
                                   const std::vector<Mat4>& inverseBind,
                                   const std::vector<const AnimTrack*>& tracks,
                                   float time,
                                   std::vector<Mat4>& outSkin) {
    std::vector<Mat4> boneWorld;
    ComputeBoneWorldAtTime(bones, tracks, time, boneWorld);
    BoneWorldToSkinning(inverseBind, boneWorld, outSkin);
}

void SkinMeshVertices(const ModelMesh& mesh, const std::vector<Mat4>& skin,
                      std::vector<float>& outVerts) {
    outVerts = mesh.verts;                 // uv and the layout come through as-is
    if (!mesh.hasSkin()) return;

    const size_t vc = mesh.vertexCount();
    for (size_t i = 0; i < vc; ++i) {
        const float x = mesh.verts[i * 8 + 0];
        const float y = mesh.verts[i * 8 + 1];
        const float z = mesh.verts[i * 8 + 2];
        const float nx = mesh.verts[i * 8 + 3];
        const float ny = mesh.verts[i * 8 + 4];
        const float nz = mesh.verts[i * 8 + 5];

        double px = 0, py = 0, pz = 0;
        double dx = 0, dy = 0, dz = 0;
        for (const SkinInfluence& inf : mesh.skin[i]) {
            if (inf.bone >= skin.size()) continue;
            const Mat4& mtx = skin[inf.bone];
            float t[3];
            mtx.TransformPoint(x, y, z, t);
            px += double(t[0]) * inf.weight;
            py += double(t[1]) * inf.weight;
            pz += double(t[2]) * inf.weight;
            // The normal rides the same matrix WITHOUT its translation.
            // Feeding it through TransformPoint would drag it to wherever the
            // bone sits and light the model as if every face pointed there.
            const float rx = nx * mtx[0] + ny * mtx[4] + nz * mtx[8];
            const float ry = nx * mtx[1] + ny * mtx[5] + nz * mtx[9];
            const float rz = nx * mtx[2] + ny * mtx[6] + nz * mtx[10];
            dx += double(rx) * inf.weight;
            dy += double(ry) * inf.weight;
            dz += double(rz) * inf.weight;
        }
        outVerts[i * 8 + 0] = float(px);
        outVerts[i * 8 + 1] = float(py);
        outVerts[i * 8 + 2] = float(pz);

        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > 1e-8) {
            outVerts[i * 8 + 3] = float(dx / len);
            outVerts[i * 8 + 4] = float(dy / len);
            outVerts[i * 8 + 5] = float(dz / len);
        }
    }
}
} // namespace painful
