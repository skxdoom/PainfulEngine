#include "SkeletonCache.h"

#include "../Core/FileSystem.h"

#include <algorithm>

namespace painful {

const SkeletonCache::Entry* SkeletonCache::Get(const std::string& model) {
    if (model.empty()) return nullptr;

    auto it = cache_.find(model);
    if (it != cache_.end()) return it->second.ok ? &it->second.entry : nullptr;

    Slot& slot = cache_[model];
    const std::string path = root_ + "/" + model + ".pkmdl";

    Model loaded;
    if (FileSystem::Get().Exists(path) && Model::Load(path, loaded) &&
        !loaded.bones.empty()) {
        slot.entry.bones = std::move(loaded.bones);
        BuildHierarchy(slot.entry.bones);
        ComputeBindWorld(slot.entry.bones, slot.entry.bindWorld, slot.entry.inverseBind);

        for (int c = 0; c < 3; ++c) { slot.entry.lo[c] = 1e30f; slot.entry.hi[c] = -1e30f; }
        for (const ModelMesh& mesh : loaded.meshes)
            for (size_t i = 0; i < mesh.vertexCount(); ++i)
                for (int c = 0; c < 3; ++c) {
                    const float v = mesh.verts[i * 8 + size_t(c)];
                    slot.entry.lo[c] = std::min(slot.entry.lo[c], v);
                    slot.entry.hi[c] = std::max(slot.entry.hi[c], v);
                }
        for (int c = 0; c < 3; ++c)
            if (slot.entry.lo[c] > slot.entry.hi[c]) slot.entry.lo[c] = slot.entry.hi[c] = 0.f;
        slot.ok = true;
        ++loaded_;
        return &slot.entry;
    }

    // Cached as a miss for the same reason the animations are: a script asks
    // the same boneless prop for a joint on every pass, and a failed load must
    // not become a per-frame file probe.
    slot.ok = false;
    ++missing_;
    return nullptr;
}

} // namespace painful
