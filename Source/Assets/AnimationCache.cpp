#include "AnimationCache.h"

#include "../Core/FileSystem.h"
#include "../Core/Log.h"

namespace painful {

const Animation* AnimationCache::Get(const std::string& model, const std::string& anim) {
    if (model.empty() || anim.empty()) return nullptr;

    const std::string key = model + "|" + anim;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.ok ? &it->second.anim : nullptr;

    Entry& entry = cache_[key];
    const std::string path = root_ + "/" + model + "." + anim + ".ani";
    if (FileSystem::Get().Exists(path) && Animation::Load(path, entry.anim) &&
        entry.anim.error.empty()) {
        entry.ok = true;
        ++loaded_;
        return &entry.anim;
    }

    // Cached as a miss on purpose: an actor asks for the same missing track
    // every time it tries that state, and a failed load must not become a
    // per-frame file probe.
    entry.ok = false;
    ++missing_;
    return nullptr;
}

} // namespace painful
