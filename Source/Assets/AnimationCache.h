#pragma once
#include "Ani.h"

#include <string>
#include <unordered_map>

namespace painful {

// Loads and keeps the .ani files a level asks for.
//
// Animations are named by convention rather than by any index in the data:
// `<Model>.<anim>.ani` beside the model, so `evilmonk.pkmdl` playing "idle"
// wants `evilmonk.idle.ani`. The shipped data has 1228 of them across 284
// models, and an actor changes animation constantly, so they are loaded on
// first use and kept.
//
// A model with no such track is a normal, expected answer - the scripts test
// `MDL.SetAnim` for a negative result and carry on without it - so a failed
// load is cached as a miss rather than retried every time an actor asks.
class AnimationCache {
public:
    void SetRoot(const std::string& modelsRoot) { root_ = modelsRoot; }

    // The parsed animation, or nullptr when the model has no such track.
    const Animation* Get(const std::string& model, const std::string& anim);

    size_t loaded() const { return loaded_; }
    size_t missing() const { return missing_; }

private:
    struct Entry {
        Animation anim;
        bool ok = false;
    };

    std::string root_;
    std::unordered_map<std::string, Entry> cache_;   // "model|anim"
    size_t loaded_ = 0, missing_ = 0;
};

} // namespace painful
