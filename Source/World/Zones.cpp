#include "Zones.h"
#include "../Render/Frustum.h"
#include "../Core/Log.h"

#include <algorithm>
#include <string>
#include <cstring>

namespace painful {

namespace {

// World::BuildZones (Engine.dll 0x1005c1c0) validates each portal against the
// zone volumes with tolerance 2.0 and links with tolerance 0.1 - in world
// units, converted to this graph's raw space by the caller's scale.
constexpr float kValiditySlack = 2.0f;
constexpr float kLinkSlack = 0.1f;

void BoundsOf(const MapObject& o, float lo[3], float hi[3]) {
    lo[0] = lo[1] = lo[2] = 1e30f;
    hi[0] = hi[1] = hi[2] = -1e30f;
    for (size_t i = 0; i < o.vertexCount(); ++i) {
        float p[3];
        o.position(i, p);
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
    }
}

bool Overlaps(const float aLo[3], const float aHi[3], const float bLo[3],
              const float bHi[3], float slack) {
    for (int a = 0; a < 3; ++a) {
        if (aHi[a] < bLo[a] - slack || aLo[a] > bHi[a] + slack) return false;
    }
    return true;
}

// The visibility zones are plain "zone" objects; death/ladder/acoustic zones
// and the like are gameplay volumes, not part of the portal system.
bool IsVisibilityZone(const MapObject& o) {
    if (!o.nameHas("zone")) return false;
    return !o.nameHas("death") && !o.nameHas("ladder") && !o.nameHas("portal") &&
           !o.nameHas("acoustic");
}

bool IsPortal(const MapObject& o) {
    return o.nameHas("portal") && !o.nameHas("antyp");
}

} // namespace

void ZoneGraph::Build(const MapMesh& map, float worldScale) {
    zones_.clear();
    portals_.clear();
    const float scale = worldScale > 1e-6f ? worldScale : 1.f;
    const float validity = kValiditySlack / scale;
    const float link = kLinkSlack / scale;

    for (const MapObject& o : map.objects) {
        if (!IsVisibilityZone(o) || o.vertexCount() == 0) continue;
        Box z;
        BoundsOf(o, z.lo, z.hi);
        zones_.push_back(z);
    }
    if (zones_.empty()) return;

    for (const MapObject& o : map.objects) {
        if (!IsPortal(o) || o.vertexCount() == 0) continue;
        Portal p;
        BoundsOf(o, p.box.lo, p.box.hi);
        // Like the engine: link the portal to EVERY zone it touches, and
        // drop portals touching fewer than two (World::BuildZones deletes
        // those outright).
        int touched = 0;
        for (size_t z = 0; z < zones_.size(); ++z) {
            if (Overlaps(p.box.lo, p.box.hi, zones_[z].lo, zones_[z].hi, validity))
                ++touched;
            if (Overlaps(p.box.lo, p.box.hi, zones_[z].lo, zones_[z].hi, link))
                p.zones.push_back(int(z));
        }
        if (touched < 2) continue;
        // The link pass is stricter than the validity pass; make sure at
        // least two links survive so the portal actually connects something.
        if (p.zones.size() < 2) {
            p.zones.clear();
            for (size_t z = 0; z < zones_.size(); ++z) {
                if (Overlaps(p.box.lo, p.box.hi, zones_[z].lo, zones_[z].hi, validity))
                    p.zones.push_back(int(z));
            }
        }
        portals_.push_back(std::move(p));
    }
}

void ZoneGraph::ZonesAt(const float pos[3], std::vector<int>& out) const {
    out.clear();
    for (size_t z = 0; z < zones_.size(); ++z) {
        if (zones_[z].Contains(pos, 0.f)) out.push_back(int(z));
    }
}

void ZoneGraph::ZonesForBox(const float lo[3], const float hi[3],
                            std::vector<int>& out) const {
    out.clear();
    for (size_t z = 0; z < zones_.size(); ++z) {
        if (Overlaps(lo, hi, zones_[z].lo, zones_[z].hi, 0.f)) out.push_back(int(z));
    }
}

void ZoneGraph::VisibleZones(const Frustum& frustum, const std::vector<int>& startZones,
                             float worldScale, std::vector<bool>& visible) const {
    visible.assign(zones_.size(), false);
    // A camera outside every zone sees the open world; from there any zone
    // could be visible, so only the frustum culls.
    if (startZones.empty()) {
        visible.assign(zones_.size(), true);
        return;
    }

    std::vector<int> queue;
    for (int z : startZones) {
        if (z >= 0 && z < int(zones_.size()) && !visible[z]) {
            visible[z] = true;
            queue.push_back(z);
        }
    }
    while (!queue.empty()) {
        const int zone = queue.back();
        queue.pop_back();
        for (const Portal& p : portals_) {
            bool touchesZone = false;
            for (int z : p.zones) {
                if (z == zone) { touchesZone = true; break; }
            }
            if (!touchesZone) continue;
            const float lo[3] = {p.box.lo[0] * worldScale, p.box.lo[1] * worldScale,
                                 p.box.lo[2] * worldScale};
            const float hi[3] = {p.box.hi[0] * worldScale, p.box.hi[1] * worldScale,
                                 p.box.hi[2] * worldScale};
            if (!frustum.VisibleAabb(lo, hi)) continue;
            // A visible portal opens every zone it touches.
            for (int other : p.zones) {
                if (other == zone || visible[other]) continue;
                visible[other] = true;
                queue.push_back(other);
            }
        }
    }
}

void ZoneGraph::Dump(float worldScale) const {
    for (size_t z = 0; z < zones_.size(); ++z) {
        const Box& b = zones_[z];
        LogInfo("  zone %2zu  x[%7.1f %7.1f] y[%7.1f %7.1f] z[%7.1f %7.1f]", z,
                b.lo[0] * worldScale, b.hi[0] * worldScale, b.lo[1] * worldScale,
                b.hi[1] * worldScale, b.lo[2] * worldScale, b.hi[2] * worldScale);
    }
    for (size_t p = 0; p < portals_.size(); ++p) {
        const Portal& pt = portals_[p];
        std::string links;
        for (int z : pt.zones) links += std::to_string(z) + " ";
        LogInfo("  portal %2zu  links %-12s x[%7.1f %7.1f] y[%7.1f %7.1f] z[%7.1f %7.1f]",
                p, links.c_str(), pt.box.lo[0] * worldScale, pt.box.hi[0] * worldScale,
                pt.box.lo[1] * worldScale, pt.box.hi[1] * worldScale,
                pt.box.lo[2] * worldScale, pt.box.hi[2] * worldScale);
    }
}

} // namespace painful
