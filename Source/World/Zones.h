#pragma once
#include "../Assets/Mpk.h"
#include <vector>

namespace painful {

struct Frustum;

// PainEngine's visibility system, rebuilt from the helper geometry every map
// ships: "zone" objects are box volumes partitioning the level, "portal"
// objects are quads on the boundaries between them. A zone is drawn only when
// a chain of portals leads to it from the camera's zone.
//
// Some maps encode the linkage in portal names (portal4_zone5_zoneshape6),
// but many do not - and one zone is literally named "zoneshape123421" - so the
// graph is built geometrically: a portal connects the zones its quad touches.
// Everything errs on the side of drawing: geometry outside every zone, a
// camera outside every zone, or a portal with fewer than two zones all fall
// back to "visible".
class ZoneGraph {
public:
    // Reads the zone and portal helper objects out of the map. Coordinates
    // stay in raw mesh space; callers query with raw coordinates too.
    // worldScale converts the engine's authored tolerances (which are in
    // world units) into this raw space.
    void Build(const MapMesh& map, float worldScale);

    bool empty() const { return zones_.empty(); }
    // Prints every zone and portal with its links (world-space bounds).
    void Dump(float worldScale) const;
    size_t zoneCount() const { return zones_.size(); }
    size_t portalCount() const { return portals_.size(); }

    // Every zone containing the point. Zone volumes overlap in the shipped
    // maps, so a single "the" zone does not exist - starting visibility from
    // just one overlapping candidate culls rooms the camera is really in.
    void ZonesAt(const float pos[3], std::vector<int>& out) const;

    // Every zone the box overlaps. A chunk is drawn if any of them is
    // visible; a chunk overlapping no zone is always drawn.
    void ZonesForBox(const float lo[3], const float hi[3], std::vector<int>& out) const;

    // Marks every zone reachable from the start set through portals whose
    // quad passes the frustum test (portal boxes are pre-scaled by worldScale
    // to match the rendered space). An empty start set marks everything
    // visible - never guess and over-cull the playable area.
    void VisibleZones(const Frustum& frustum, const std::vector<int>& startZones,
                      float worldScale, std::vector<bool>& visible) const;

private:
    struct Box {
        float lo[3], hi[3];
        bool Contains(const float p[3], float slack) const {
            return p[0] >= lo[0] - slack && p[0] <= hi[0] + slack &&
                   p[1] >= lo[1] - slack && p[1] <= hi[1] + slack &&
                   p[2] >= lo[2] - slack && p[2] <= hi[2] + slack;
        }
    };
    struct Portal {
        Box box;
        // Every zone the portal touches. World::BuildZones links portals to
        // ALL touching zones (an adjacency list, not a pair) - Cemetery's
        // ceiling portals touch a ground zone, the air-layer zone above it
        // and more, and pair-linking breaks exactly there.
        std::vector<int> zones;
    };

    std::vector<Box> zones_;
    std::vector<Portal> portals_;
};

} // namespace painful
