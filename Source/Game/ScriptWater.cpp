// ScriptEngine: water surfaces, and the ENTITY.IsWater the weapons ask about.
//
// A surface is water because of its NAME. WorldMesh::SetupFlags (0x101d7050)
// runs plain lowercase strstr tests over the object name and sets 0x8000000 for
// "water" - and 0x8000000 is bit 27, which is exactly the bit ENTITY.IsWater
// (0x10136050) reads off the entity after checking its type is 1, ETypes.Mesh.
// So the native answers "is this a world-mesh object whose name says water",
// and nothing about it is file data. See Docs/Reference/Water.md.
//
// The port has a complication the original does not. In PainEngine the level's
// geometry IS entities, so a trace naturally reports which object it hit and
// the scripts can ask about it. Here the collidable world is one anonymous
// body, and - worse - every shipped water object is also named `noclip`
// (`water_noclip_ashape` in City on Water), which is one of the tokens
// MapObject::isCollidable rejects. Water is therefore not in the physics world
// at all: correct, because you swim through it, but it means a trace can never
// come back holding it, and every `if ENTITY.IsWater(e)` in the weapon scripts
// was dead no matter what this native did.
//
// So water is registered separately, one entity per surface, and the traces
// test against it directly. The original keeps its own pointer to the water
// mesh for the same reason - SetupFlags stores it at World+0x778 rather than
// leaving it to the general geometry path.

#include "ScriptEngineInternal.h"

namespace painful {

namespace {

// The engine's test is case-sensitive and the shipped names are lowercase.
bool NameSaysWater(const std::string& name) {
    return name.find("water") != std::string::npos;
}

}  // namespace

// Called once the map is loaded. Each water object becomes a world-object
// entity, the same kind WORLD.FindEntityByName hands out: it has a name and a
// handle the scripts can carry, but no body and no renderer instance.
void ScriptEngine::BuildWaterSurfaces() {
    water_.clear();
    if (!mapLoaded_) return;

    const float scale = world_.scale > 0.f ? world_.scale : 1.f;
    for (const MapObject& o : map_.objects) {
        if (!NameSaysWater(o.name)) continue;
        // Flat by construction - the shipped surfaces are a single horizontal
        // plane, which is why their y bounds are equal. Take the top either
        // way, so a surface with any thickness still reads as its surface.
        WaterSurface w;
        w.y = o.bboxMax[1] * scale;
        w.lo[0] = o.bboxMin[0] * scale;
        w.lo[1] = o.bboxMin[2] * scale;
        w.hi[0] = o.bboxMax[0] * scale;
        w.hi[1] = o.bboxMax[2] * scale;

        Entity e;
        e.type = kMesh;
        e.name = o.name;
        e.worldObject = true;
        e.inWorld = true;
        e.visible = false;          // the renderer draws the world mesh itself
        w.entity = nextHandle_++;
        entities_.emplace(w.entity, e);
        ++created_;
        water_.push_back(w);
        LogInfo("water surface \"%s\" at y=%.2f, x[%.0f..%.0f] z[%.0f..%.0f]",
                o.name.c_str(), w.y, w.lo[0], w.hi[0], w.lo[1], w.hi[1]);
    }
}

// Where the segment first crosses a surface, as a fraction along it. Only a
// crossing counts: a segment entirely above or entirely below the plane has
// not hit the water, which is what keeps a shot fired across a lake from
// reporting one.
bool ScriptEngine::TraceWater(const float from[3], const float to[3], float& t,
                              int& entity) const {
    bool got = false;
    float best = 1.f;
    for (const WaterSurface& w : water_) {
        const float dy = to[1] - from[1];
        if (std::fabs(dy) < 1e-6f) continue;              // parallel to the surface
        const float k = (w.y - from[1]) / dy;
        if (k < 0.f || k > 1.f || k > best) continue;
        const float x = from[0] + (to[0] - from[0]) * k;
        const float z = from[2] + (to[2] - from[2]) * k;
        if (x < w.lo[0] || x > w.hi[0] || z < w.lo[1] || z > w.hi[1]) continue;
        best = k;
        entity = w.entity;
        got = true;
    }
    if (got) t = best;
    return got;
}

// ENTITY.IsWater(e)
//
// Type ETypes.Mesh and a name that says water, which is the pair 0x10136050
// tests. Answers false for anything else, including the world handle 0 - a
// solid wall is not water, and the scripts branch on exactly that.
int ScriptEngine::L_ENTITY_IsWater(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e && e->type == kMesh && NameSaysWater(e->name) ? 1 : 0);
    return 1;
}

}  // namespace painful
