#pragma once
// Shared by the viewer and the level report: what a loaded map adds up to.
#include "Assets/Mpk.h"
#include "World/Level.h"

namespace painful {

// Totals worth showing while the renderer is still being built out.
struct LevelStats {
    size_t objects = 0, verts = 0, tris = 0, materials = 0, collidable = 0;
    // Objects physics promotes to bodies, and their material runs: each body
    // is drawn by the entity path with its own GPU buffers.
    size_t active = 0, activeRuns = 0, activePinned = 0;
};

inline LevelStats Summarise(const Level& level) {
    LevelStats s;
    if (!level.mapLoaded()) return s;
    const MapMesh& m = level.map();
    s.objects = m.objects.size();
    for (const MapObject& o : m.objects) {
        s.verts += o.vertexCount();
        s.tris += o.triangleCount();
        s.materials += o.materials.size();
        if (o.isCollidable()) ++s.collidable;
        if (o.isActiveMesh() && o.vertexCount() > 0) {
            ++s.active;
            s.activeRuns += o.materials.size();
            if (o.isPinned()) ++s.activePinned;
        }
    }
    return s;
}

}  // namespace painful
