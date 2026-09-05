#pragma once
#include "../Core/Log.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace painful {

// bgfx hands back an invalid handle - silently, in a release build - once a
// pool (BGFX_CONFIG_MAX_*_BUFFERS) is spent, and a draw with one reads
// whatever indices happen to be bound. Say so, once per pool.
// Docs/Reference/Physics.md, "Active meshes".
inline bgfx::IndexBufferHandle MakeIndexBuffer(const uint16_t* indices, uint32_t count) {
    const bgfx::IndexBufferHandle h =
        bgfx::createIndexBuffer(bgfx::copy(indices, count * uint32_t(sizeof(uint16_t))));
    static bool said = false;
    if (!bgfx::isValid(h) && !said) {
        said = true;
        LogWarn("gpu: out of index buffer handles (BGFX_CONFIG_MAX_INDEX_BUFFERS); "
                "meshes made from here on draw with wrong indices");
    }
    return h;
}

inline bgfx::VertexBufferHandle MakeVertexBuffer(const void* data, uint32_t bytes,
                                                 const bgfx::VertexLayout& layout) {
    const bgfx::VertexBufferHandle h = bgfx::createVertexBuffer(bgfx::copy(data, bytes), layout);
    static bool said = false;
    if (!bgfx::isValid(h) && !said) {
        said = true;
        LogWarn("gpu: out of vertex buffer handles (BGFX_CONFIG_MAX_VERTEX_BUFFERS); "
                "meshes made from here on do not draw");
    }
    return h;
}

} // namespace painful
