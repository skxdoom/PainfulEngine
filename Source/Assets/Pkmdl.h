#pragma once
#include "../Core/Common.h"

namespace painful {

// Bones are serialised in PREORDER (depth-first) order and each record's trailing
// byte is that bone's CHILD COUNT. Walking that with a stack recovers parent links
// exactly - see BuildHierarchy in Skeleton.h.
struct Bone {
    std::string name;
    Mat4 bind;              // parent-relative bind pose
    uint8_t childCount = 0;
    int parent = -1;
};

struct SkinInfluence {
    uint16_t bone = 0;
    float weight = 0;
};

struct ModelMesh {
    std::string name;
    std::vector<std::string> textures;
    std::vector<float> verts;                      // 8 floats: pos3, normal3, uv2
    std::vector<uint16_t> indices;
    std::vector<std::vector<SkinInfluence>> skin;  // one entry per vertex
    size_t offset = 0;
    size_t indexOffset = 0;

    // Same convention the world meshes use: the NAME carries the material
    // variant. evilmonkv2's robe is "polySurfa_2sided", and skin.shader ships
    // palskinned2sided for it.
    bool nameHas(const char* token) const;

    // One texture name per material, in order. Unlike .mpk materials these carry
    // only a texture reference - no triangle range or UV transform.
    std::vector<std::string> materials;
    // True when the material header parsed and landed EXACTLY on the geometry
    // header, i.e. the layout is fully accounted for.
    bool materialsExact = false;

    size_t vertexCount() const { return verts.size() / 8; }
    size_t triangleCount() const { return indices.size() / 3; }
    bool hasSkin() const { return !verts.empty() && skin.size() == vertexCount(); }
};

// PainEngine .pkmdl model - a serialised engine object graph.
//
// Geometry is located by anchoring on the vertex block, because the fields that
// precede the index array are not laid out identically in every model. Three
// traps, each of which silently DROPS meshes rather than erroring:
//   * never advance the scan past the variable-length skin block;
//   * never jump the scan forward on string matches (a spurious length-prefixed
//     string can leap over an entire vertex block);
//   * bone names may be full Maya DAG paths ("joint1|joint2|..."), so any
//     name-length cap must be generous.
struct Model {
    uint32_t version = 0;
    std::string name, sourcePath, className;
    std::vector<Bone> bones;
    std::vector<ModelMesh> meshes;
    size_t size = 0;

    static bool Load(const std::string& path, Model& out);
};

} // namespace painful
