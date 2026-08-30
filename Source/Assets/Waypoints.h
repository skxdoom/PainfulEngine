#pragma once
#include "../Core/Common.h"

#include <string>
#include <vector>

namespace painful {

// PainEngine .wps waypoint set - the navigation graph an actor walks.
//
// One ships beside every map (`1x01_Chaos.mpk` / `1x01_Chaos.wps`), and
// `WPT.Load` hands it to Pathfinder2, which `PATH.GetShortest` then searches.
// Without it monsters can only walk straight at whatever they are chasing.
//
// The layout, read off the file and checked against every shipped set:
//
//   u32  count
//   count x {
//       f32 x, y, z          world position
//       u8  flags[3]
//       u32 linkStart        index into the link arrays
//       u32 linkCount        how many links belong to this waypoint
//   }                        23 bytes, unpadded
//   u32  linkTotal           repeats the sum of every linkCount
//   f32  cost[linkTotal]     TWO PARALLEL ARRAYS, not interleaved records -
//   u32  index[linkTotal]    every cost, then every neighbour
//   ...                      the floors section, which routing does not need
//
// The adjacency is stored the way a compressed sparse row is: every
// waypoint's links are contiguous, and linkStart[i] + linkCount[i] is exactly
// linkStart[i+1]. That invariant is what confirms the record stride, and
// `painful wps` checks it on load rather than trusting it.
struct WaypointSet {
    struct Node {
        float pos[3] = {0, 0, 0};
        // 24-bit floor index. The Cathedral uses 0..52 and its floors section
        // opens with 53, which is what identifies these three bytes.
        uint32_t floor = 0;
        uint32_t linkStart = 0;
        uint32_t linkCount = 0;
    };

    std::vector<Node> nodes;
    std::vector<uint32_t> links;      // neighbour indices, indexed by linkStart
    std::vector<float> costs;         // parallel to links: the edge weights
    std::string error;
    size_t size = 0, consumed = 0;
    size_t floorBytes = 0;            // the trailing floors section, unparsed

    bool ok() const { return error.empty() && !nodes.empty(); }

    // Nearest node to a point, or -1 when the set is empty. `maxDist` of 0 or
    // less accepts any distance; otherwise a node further away than that is
    // no answer at all, which is how an actor standing somewhere the level
    // designer never marked ends up walking straight instead of teleporting
    // its route across the map.
    int Closest(const float p[3], float maxDist = 0.f) const;

    // Shortest route from one waypoint to another, as node indices INCLUDING
    // both ends. Returns false when they are not connected.
    //
    // A* over the file's own edge costs, with straight-line distance as the
    // heuristic. The costs are stored rather than derived because the level
    // designers could weight a link - a route the AI should avoid costs more
    // than its length.
    bool FindPath(int from, int to, std::vector<int>& outNodes) const;

    static bool Load(const std::string& path, WaypointSet& out);
};

} // namespace painful
