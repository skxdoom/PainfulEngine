#pragma once
#include "../Core/Common.h"

namespace painful {

// One texture slot of a material. Every material carries exactly four; unused
// slots hold an empty name and an identity transform. Slot 0 is the diffuse map,
// slot 1 the lightmap on lightmapped geometry (its name is the mesh name plus the
// lightmap suffix, "_L_0000" by default).
struct TextureSlot {
    std::string name;
    float offsetU = 0, offsetV = 0, scaleU = 1, scaleV = 1;
    bool empty() const { return name.empty(); }
};

// A material covers a contiguous run of the object's index array.
struct Material {
    uint16_t firstIndex = 0;
    uint16_t triangleCount = 0;
    TextureSlot slots[4];
    const std::string& diffuse()  const { return slots[0].name; }
    const std::string& lightmap() const { return slots[1].name; }
};

// A single world-mesh object. The NAME carries engine semantics - see the shipped
// "Pain Engine - MPK Substrings" document. These substrings are the level's entity
// system (portals, antiportals, zones, barriers, breakable glass, physics props),
// so a source port must honour them rather than treating names as labels.
struct MapObject {
    std::string name;
    Mat4 transform;
    uint32_t uvChannels = 1;          // 1 = dynamically lit, 2 = lightmapped
    std::vector<float> verts;         // 8 floats per vertex
    std::vector<float> normals;       // 3 floats per vertex when stored separately
    float bboxMin[3] = {0,0,0};
    float bboxMax[3] = {0,0,0};
    std::vector<uint16_t> indices;
    std::vector<Material> materials;

    size_t vertexCount() const { return verts.size() / 8; }
    size_t triangleCount() const { return indices.size() / 3; }

    void position(size_t i, float out[3]) const {
        out[0] = verts[i * 8 + 0]; out[1] = verts[i * 8 + 1]; out[2] = verts[i * 8 + 2];
    }
    void normal(size_t i, float out[3]) const {
        if (normals.size() >= (i + 1) * 3) {
            out[0] = normals[i * 3]; out[1] = normals[i * 3 + 1]; out[2] = normals[i * 3 + 2];
        } else {
            out[0] = verts[i * 8 + 3]; out[1] = verts[i * 8 + 4]; out[2] = verts[i * 8 + 5];
        }
    }
    void uv(size_t i, float out[2]) const {
        // uvChannels == 1 keeps normals inline, pushing UVs to slots 6/7;
        // uvChannels == 2 has no inline normal, so UVs sit at 4/5.
        size_t o = (uvChannels == 1) ? 6 : 4;
        out[0] = verts[i * 8 + o]; out[1] = verts[i * 8 + o + 1];
    }
    // Second UV channel (lightmap/mask coordinates). In both layouts the last
    // two floats hold it: on 1-UV objects they are the only UVs, on 2-UV
    // objects the first channel sits at 4/5 and the second here at 6/7.
    void uv1(size_t i, float out[2]) const {
        out[0] = verts[i * 8 + 6]; out[1] = verts[i * 8 + 7];
    }

    bool nameHas(const char* token) const;
    bool isCollidable() const;
};

// PainEngine .mpk world mesh.
//   u32 magic = 0xDEAFBABE
//   objects until u32 terminator = 0xDEADBEEF
// See RE/FINDINGS.md for the full field layout.
struct MapMesh {
    static constexpr uint32_t kMagic = 0xDEAFBABEu;
    static constexpr uint32_t kTerminator = 0xDEADBEEFu;

    std::vector<MapObject> objects;
    bool terminated = false;
    std::string error;
    // Bytes the parser had to scan past because no valid record started there.
    size_t skippedBytes = 0;
    std::vector<std::pair<size_t, size_t>> skips;   // (offset, size) of unparsed regions
    size_t skippedRegions = 0;
    size_t parseEnd = 0;                            // file offset where parsing stopped
    size_t size = 0;

    static bool Load(const std::string& path, MapMesh& out);

    // Writes a .mpk the loader above reads back. This is what makes a level
    // authorable from code: everything else in a level directory is already
    // plain Lua text, and the world mesh was the one binary in the way.
    //
    // The record layout mirrors Load exactly - see Mpk.cpp for the field
    // order. Two conventions have to be honoured by anything building an
    // object by hand, and both are measured rather than assumed (the `map`
    // diagnostic reports the second):
    //
    //   * verts is 8 floats per vertex. With uvChannels == 1 that is
    //     position, normal, uv; with 2 it is position, uv0, uv1 and the
    //     normals live in their own array.
    //   * the exporter winds triangles so the geometric normal OPPOSES the
    //     vertex normal - 283457 of 283501 triangles in 1x01_Chaos do. The
    //     renderer culls CCW to suit, and PhysicsWorld reverses each triangle
    //     so Jolt's CCW-front rule agrees. Wound the intuitive way instead, a
    //     floor is invisible from above and bodies fall through it.
    static bool Write(const std::string& path, const MapMesh& mesh);
};

} // namespace painful
