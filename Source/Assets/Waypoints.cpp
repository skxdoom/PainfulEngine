#include "Waypoints.h"

#include "../Core/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

namespace painful {

namespace {

constexpr size_t kRecord = 23;      // 3 floats + a 24-bit floor + 2 u32
constexpr float kInfinite = 3.4e38f;

// One entry in A*'s frontier, ordered by f so `greater` gives a min-heap.
struct Open {
    float f = 0.f;
    int node = -1;
    bool operator>(const Open& o) const { return f > o.f; }
};

} // namespace

bool WaypointSet::Load(const std::string& path, WaypointSet& out) {
    out = WaypointSet{};

    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        out.error = "cannot read " + path;
        return false;
    }
    out.size = bytes.size();

    Reader r(bytes.data(), bytes.size());
    const uint32_t count = r.u32();
    if (!r.ok(0) || count == 0) {
        out.error = "no waypoints";
        return false;
    }
    // A record is 23 bytes; anything that does not fit is not this format.
    if (4 + size_t(count) * kRecord > bytes.size()) {
        out.error = "truncated: " + std::to_string(count) + " waypoints do not fit";
        return false;
    }

    out.nodes.resize(count);
    uint64_t expected = 0;            // running linkStart, for the CSR check
    for (uint32_t i = 0; i < count; ++i) {
        Node& n = out.nodes[i];
        for (int c = 0; c < 3; ++c) n.pos[c] = r.f32();
        n.floor = uint32_t(r.u8());
        n.floor |= uint32_t(r.u8()) << 8;
        n.floor |= uint32_t(r.u8()) << 16;
        n.linkStart = r.u32();
        n.linkCount = r.u32();

        // The invariant that confirms the stride: links are contiguous and in
        // order. If this ever fails the record size is wrong, and every
        // position after it is garbage - better to say so than to hand back a
        // graph that silently points nowhere.
        if (n.linkStart != expected) {
            out.error = "waypoint " + std::to_string(i) + " starts its links at " +
                        std::to_string(n.linkStart) + ", expected " +
                        std::to_string(expected);
            return false;
        }
        expected += n.linkCount;
    }
    out.consumed = 4 + size_t(count) * kRecord;

    const size_t totalLinks = size_t(expected);
    if (totalLinks == 0) {
        out.error = "no links";
        return false;
    }

    // The links repeat their own count, then come as TWO PARALLEL ARRAYS -
    // every cost, then every neighbour index - rather than interleaved. That
    // is what makes the section look like floats when read as records: the
    // whole first half is distances.
    if (!r.ok(4)) { out.error = "truncated before the link count"; return false; }
    const uint32_t declared = r.u32();
    if (declared != totalLinks) {
        out.error = "link count " + std::to_string(declared) + " disagrees with the " +
                    std::to_string(totalLinks) + " the waypoints ask for";
        return false;
    }
    if (!r.ok(totalLinks * 8)) {
        out.error = "truncated link arrays";
        return false;
    }

    out.costs.resize(totalLinks);
    for (size_t i = 0; i < totalLinks; ++i) out.costs[i] = r.f32();

    out.links.resize(totalLinks);
    for (size_t i = 0; i < totalLinks; ++i) {
        out.links[i] = r.u32();
        if (out.links[i] >= count) {
            out.error = "link " + std::to_string(i) + " points at waypoint " +
                        std::to_string(out.links[i]) + " of " + std::to_string(count);
            return false;
        }
    }
    out.consumed = r.pos();
    // Whatever is left is the FLOORS section - Pathfinder2::LoadFloors and the
    // Select_OnSelectedFloors family - which groups waypoints into regions.
    // Routing does not need it, so it is measured and left alone.
    out.floorBytes = bytes.size() - out.consumed;
    return true;
}

int WaypointSet::Closest(const float p[3], float maxDist) const {
    int best = -1;
    float bestDist = 0.f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        float d = 0.f;
        for (int c = 0; c < 3; ++c) {
            const float e = nodes[i].pos[c] - p[c];
            d += e * e;
        }
        if (best < 0 || d < bestDist) {
            best = int(i);
            bestDist = d;
        }
    }
    if (best >= 0 && maxDist > 0.f && bestDist > maxDist * maxDist) return -1;
    return best;
}

namespace {

float Distance(const WaypointSet::Node& a, const WaypointSet::Node& b) {
    float d = 0.f;
    for (int c = 0; c < 3; ++c) {
        const float e = a.pos[c] - b.pos[c];
        d += e * e;
    }
    return std::sqrt(d);
}

} // namespace

bool WaypointSet::FindPath(int from, int to, std::vector<int>& outNodes) const {
    outNodes.clear();
    if (from < 0 || to < 0 || size_t(from) >= nodes.size() || size_t(to) >= nodes.size())
        return false;
    if (from == to) {
        outNodes.push_back(from);
        return true;
    }

    const size_t n = nodes.size();
    std::vector<float> best(n, kInfinite);
    std::vector<int> came(n, -1);
    std::vector<uint8_t> done(n, 0);

    // Ordered by f = g + h. The graph is a few thousand nodes with a dozen
    // links each, so a binary heap is ample and there is no need for anything
    // cleverer.
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    best[size_t(from)] = 0.f;
    open.push({Distance(nodes[size_t(from)], nodes[size_t(to)]), from});

    while (!open.empty()) {
        const Open cur = open.top();
        open.pop();
        const size_t at = size_t(cur.node);
        if (done[at]) continue;
        done[at] = 1;
        if (cur.node == to) break;

        const Node& node = nodes[at];
        for (uint32_t k = 0; k < node.linkCount; ++k) {
            const size_t edge = size_t(node.linkStart) + k;
            if (edge >= links.size()) break;
            const size_t next = size_t(links[edge]);
            if (done[next]) continue;

            // The file's own cost, falling back to the straight-line distance
            // if a set ever ships one that is not positive.
            float step = edge < costs.size() ? costs[edge] : 0.f;
            if (!(step > 0.f)) step = Distance(node, nodes[next]);

            const float g = best[at] + step;
            if (g >= best[next]) continue;
            best[next] = g;
            came[next] = cur.node;
            open.push({g + Distance(nodes[next], nodes[size_t(to)]), int(next)});
        }
    }

    if (came[size_t(to)] < 0 && from != to) return false;
    for (int at = to; at >= 0; at = came[size_t(at)]) {
        outNodes.push_back(at);
        if (at == from) break;
    }
    if (outNodes.empty() || outNodes.back() != from) {
        outNodes.clear();
        return false;
    }
    std::reverse(outNodes.begin(), outNodes.end());
    return true;
}

} // namespace painful
