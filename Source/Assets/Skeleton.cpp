#include "Skeleton.h"
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

} // namespace painful
