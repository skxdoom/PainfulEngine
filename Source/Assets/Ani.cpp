#include "Ani.h"
#include <cstring>

namespace painful {

// The last key. After Load has rebased the timeline that IS the length, which
// is how the engine reports it: the loader stores the final rebased key into
// the field GetAnimationTotalTime reads back (Model::GetAnimationTotalTime,
// 0x101de730, returns *(float*)(anim + 0x10)). The engine takes it from the
// first track specifically; the scan is a fallback for a rig whose first track
// carries no keys.
float Animation::duration() const {
    if (!tracks.empty() && !tracks[0].keys.empty()) return tracks[0].keys.back().time;
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

    // Rebase the timeline to zero.
    //
    // A .ani is a SLICE of a longer authored take and keeps that take's own
    // timestamps rather than starting at zero. PKW.obrot's nine keys run
    // 2.84 -> 3.16; PKW.idle's run 1.4 -> 5.8. That is not a handful of odd
    // files: 103 of the 1228 shipped animations begin somewhere other than
    // zero, among them both Painkiller blade animations and the camera shakes.
    //
    // Played as authored, every one of them holds its first key until playback
    // reaches that first timestamp - the Painkiller's blades spin up, hand off
    // to the looping "obrot", and then sit frozen, because "obrot" spends its
    // whole 3.16s loop clamped to key 0 and only moves in the last 0.32s.
    //
    // Engine.dll's loader (the Animation vtable's Load, 0x10049310) takes the
    // first key of the FIRST track as the origin and subtracts it from every
    // key of every track:
    //     fVar3   = **(float **)(iVar2 + 0x10);   // track 0, key 0
    //     *pfVar5 = *pfVar1 - fVar3;              // every key, stride 0xa0
    // It then stores the last rebased key as the animation's total time, which
    // is why duration() reads the same place.
    //
    // The header float is NOT that length. It is the authored total including
    // one trailing frame step - 0.36 against obrot's 0.32, 0.52 against
    // rozkrecenie's 0.48 - so a script's `animTime == GetAnimLength` finish
    // test would never fire if we reported it.
    if (!out.tracks.empty() && !out.tracks[0].keys.empty()) {
        out.startTime = out.tracks[0].keys[0].time;
        if (out.startTime != 0.f)
            for (AnimTrack& t : out.tracks)
                for (AnimKey& k : t.keys) k.time -= out.startTime;
    }
    return out.error.empty();
}

} // namespace painful
