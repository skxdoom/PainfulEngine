#include "SkeletonCache.h"

#include "../Core/FileSystem.h"

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
