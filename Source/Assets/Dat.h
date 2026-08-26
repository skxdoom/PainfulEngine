#pragma once
#include "../Core/Common.h"
#include "Mpk.h"

namespace painful {

// A .dat item pack: the mesh container behind entities that declare o.Pack.
// It is a thin wrapper around the same WorldMesh object layout .mpk uses:
//
//   u32 version (3)
//   str  own filename          (strings are u32 length incl. NUL, then bytes)
//   str  mesh name             (matches the instance's o.Mesh)
//   str  class name            ("WorldMesh")
//   u32 x4 unknown counts
//   u32 payloadSize, u32 headerSize   (headerSize + payloadSize == file size)
//   payload, per object:
//     str  object name
//     u32  flags
//     f32 x3 bboxMin, f32 x3 bboxMax
//     Mat4 transform
//     u32  unknown
//     str  x2 texture names
//     u32  unknown
//     str  diffuse texture name
//     u32  triangleCount, u32 indexCount, u16 indices[indexCount]
//     u32  vertexCount, f32 verts[vertexCount * 8]   (pos, normal, uv)
//
// Objects are reused as MapObject so the renderer treats them like world
// geometry (uvChannels == 1 layout: normals inline, UVs at floats 6/7).
struct DatPack {
    std::string meshName;                // from the header
    std::vector<MapObject> objects;
    std::string error;
    size_t size = 0;

    static bool Load(const std::string& path, DatPack& out);
};

} // namespace painful
