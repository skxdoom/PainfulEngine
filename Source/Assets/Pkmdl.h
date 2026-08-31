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

// One MATERIAL SLOT: a texture and the run of triangles it covers.
//
// A mesh is not one material. The nun's polySurfaceShape41 is three - a face,
// a habit and a trim - and 17 of the 113 meshes across the monsters we have
// looked at carry more than one. Drawing the whole mesh with the first one
// puts the wrong image on whole regions of a model, and where that image is an
// alpha-cut one (NUNtexture3.tga, NUNtexture4.tga) under palskinned's
// `alphatest greater 128` the region does not draw at all.
//
// The range is in the file and was being skipped as an unknown separator:
//
//     [u32 len][name\0][u32 firstIndex][u32 triangleCount]
//
// firstIndex indexes the INDEX array, so it advances by 3 per triangle.
// Verified on nun.pkmdl: polySurfaceShape41 (1000 tris) reads
// [0,148] [444,240] [1164,612] - 444 = 148*3, 1164 = 444 + 240*3, and
// 148+240+612 = 1000; polySurfaceShape1044 (186 tris) reads [0,98] [294,88].
struct ModelMaterial {
    std::string texture;
    uint32_t firstIndex = 0;
    uint32_t triangles = 0;
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

    // The material slots, in order. See ModelMaterial: each carries the
    // triangle run it covers, so a mesh draws as one call per slot.
    std::vector<ModelMaterial> materials;
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
