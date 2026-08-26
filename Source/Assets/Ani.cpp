#include "Ani.h"
#include <cstring>

namespace painful {

float Animation::duration() const {
    float d = 0;
    for (const AnimTrack& t : tracks)
        if (!t.keys.empty() && t.keys.back().time > d) d = t.keys.back().time;
    return d;
}

bool Animation::Load(const std::string& path, Animation& out) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) { out.error = "cannot read file"; return false; }
    out.size = data.size();
    if (data.size() < 12 || std::memcmp(data.data(), "skel", 4) != 0) {
        out.error = "bad magic";
        return false;
    }
    Reader r(data.data(), data.size());
    r.seek(4);
    out.frameTime = r.f32();
    out.boneCount = r.u32();
    if (out.boneCount > 4096) { out.error = "implausible bone count"; return false; }

    for (uint32_t i = 0; i < out.boneCount; ++i) {
        if (r.pos() + 4 > data.size()) { out.error = "truncated"; break; }
        uint32_t nameLen = r.u32();
        // Some rigs store full Maya DAG paths, well past a hundred characters.
        if (nameLen == 0 || nameLen > 1024 || r.pos() + nameLen + 4 > data.size()) {
            out.error = "bad name length";
            break;
        }
        AnimTrack track;
        track.name.assign(reinterpret_cast<const char*>(data.data() + r.pos()), nameLen);
        r.seek(r.pos() + nameLen);
        uint32_t keyCount = r.u32();
        if (keyCount > 100000 || r.pos() + static_cast<size_t>(keyCount) * 68 > data.size()) {
            out.error = "bad key count for " + track.name;
            break;
        }
        track.keys.resize(keyCount);
        for (uint32_t k = 0; k < keyCount; ++k) {
            track.keys[k].time = r.f32();
            r.readMat4(track.keys[k].pose);
        }
        out.tracks.push_back(std::move(track));
    }
    out.consumed = r.pos();
    return out.error.empty();
}

} // namespace painful
