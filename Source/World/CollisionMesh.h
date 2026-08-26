#pragma once
#include "../Assets/Mpk.h"
#include <cstdint>
#include <vector>

namespace painful {

// A ray-queryable copy of the level's solid geometry.
//
// The original keeps this in Havok: World::LoadMeshPak hands the collidable map
// objects to the physics world, and gameplay code asks it questions with
// PhysicsWorld::LineTraceFirstHit. Coronas are the first thing in this port to
// need such a query - Billboard::Draw traces from the camera to each corona to
// decide whether it is visible - so this is a small standalone BVH rather than
// a physics engine. It is deliberately not tied to billboards: line-of-sight,
// projectile hits and AI visibility all want the same query.
//
// Triangles are stored in RENDERED space (raw mesh coordinates times the level
// o.Scale), which is the space entity positions and the camera already live in.
class CollisionMesh {
public:
    // Uses only the objects MapObject::isCollidable accepts - portals, zones,
    // volumetric-light helpers and anything named "noclip" are not solid, and
    // the original does not give them physics bodies either.
    void Build(const MapMesh& map, float worldScale);
    void Clear();

    // True when the segment hits anything solid. There is no hit position:
    // the corona test only asks "is the line of sight clear?", and stopping at
    // the first hit is much cheaper than finding the nearest one.
    bool Occluded(const float from[3], const float to[3]) const;

    bool empty() const { return tris_.empty(); }
    size_t triangleCount() const { return tris_.size(); }
    size_t nodeCount() const { return nodes_.size(); }

private:
    // Edge form, ready for Moller-Trumbore without touching the vertex array.
    struct Tri {
        float v0[3], e1[3], e2[3];
    };
    // Leaves carry a triangle run; interior nodes keep the left child adjacent
    // and store the right child's index, which is the usual flat layout.
    struct Node {
        float lo[3], hi[3];
        uint32_t start = 0, count = 0, right = 0;
    };

    uint32_t BuildNode(uint32_t begin, uint32_t end, int depth);

    std::vector<Tri> tris_;
    std::vector<Node> nodes_;
    // Centroids, kept only while building.
    std::vector<float> centroids_;
};

} // namespace painful
