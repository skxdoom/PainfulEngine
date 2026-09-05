#pragma once
#include "Skeleton.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace painful {

// Loads and keeps the skeletons of the .pkmdl models a level asks for.
//
// The renderer already loads whole models for drawing, but the joint natives
// have to answer where a bone IS, and they answer headlessly - the scripts ask
// for a weapon's bind position long before anything is on screen, and a run
// with no window is how everything else in this engine gets verified. So the
// skeleton is cached on its own: bones, their bind-pose world matrices and the
// inverses, which never change once loaded.
//
// A model without a skeleton is a normal answer, not an error - most of what a
// level places is scenery - so a miss is cached rather than reloaded on every
// query.
class SkeletonCache {
public:
    struct Entry {
        std::vector<Bone> bones;         // hierarchy already resolved
        std::vector<Mat4> bindWorld;
        std::vector<Mat4> inverseBind;
        // Model-space bounds of the meshes, for callers that need the shape
        // rather than the skeleton - a character radius is the horizontal
        // half-extent, and a T-posed humanoid's widest axis is its ARMS.
        float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        // The lowest vertex in the idle pose (first frame of idle / idle1),
        // or lo[1] when there is none. The body sizer measures from the
        // entity's local box, which is the POSED model's: the Giant's bind
        // pose floats 0.48 above its origin while every animation plants its
        // feet below it. Docs/Reference/MonsterMovement.md, "The body".
        float poseLo = 0.f, poseHi = 0.f;
    };

    void SetRoot(const std::string& modelsRoot) { root_ = modelsRoot; }

    // The skeleton, or nullptr when the model has no bones or cannot be read.
    // The cache is node-based, so the pointer stays valid as more load.
    const Entry* Get(const std::string& model);

    size_t loaded() const { return loaded_; }
    size_t missing() const { return missing_; }

private:
    struct Slot {
        Entry entry;
        bool ok = false;
    };

    std::string root_;
    std::unordered_map<std::string, Slot> cache_;
    size_t loaded_ = 0, missing_ = 0;
};

} // namespace painful
