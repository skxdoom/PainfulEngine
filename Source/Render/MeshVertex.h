#pragma once
#include <bgfx/bgfx.h>

namespace painful {

// The engine's single vertex format. MPK stores two different 32-byte layouts
// and PKMDL a third; all are normalised into this on upload so one shader
// covers world geometry and models alike.
struct MeshVertex {
    float x, y, z;
    float nx, ny, nz;
    float u0, v0;      // diffuse
    float u1, v1;      // lightmap (copies u0/v0 when there is none)
};

inline bgfx::VertexLayout MakeMeshLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

} // namespace painful
