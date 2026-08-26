#pragma once
#include "../Core/Common.h"

namespace painful {

struct AnimKey {
    float time = 0;
    Mat4 pose;          // PARENT-RELATIVE
};

struct AnimTrack {
    std::string name;   // matches a bone name in the corresponding .pkmdl
    std::vector<AnimKey> keys;
};

// PainEngine .ani skeletal animation.
//   char[4] "skel"
//   f32     frameTime
//   u32     boneCount
//   per bone: u32 nameLen (NOT including a NUL, unlike .pkmdl), name,
//             u32 keyCount, then keyCount * 68-byte keys of [f32 time][f32 m[16]]
//
// Every bone carries its own key count, so tracks are variable length and file
// sizes never factor into boneCount * frames * stride.
struct Animation {
    float frameTime = 0;
    uint32_t boneCount = 0;
    std::vector<AnimTrack> tracks;
    std::string error;
    size_t size = 0, consumed = 0;

    bool parsedExactly() const {
        return error.empty() && consumed == size && tracks.size() == boneCount;
    }
    float duration() const;

    static bool Load(const std::string& path, Animation& out);
};

} // namespace painful
