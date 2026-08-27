#include "DebugLines.h"
#include "../Core/Common.h"
#include "../Core/Log.h"

#include <algorithm>

namespace painful {

namespace {

bgfx::ShaderHandle LoadShader(const std::string& path) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));
}

struct LineVertex {
    float x, y, z;
    uint32_t abgr;
};

} // namespace

bool DebugLines::Init(const std::string& shaderDir) {
    bgfx::ShaderHandle vs = LoadShader(shaderDir + "/vs_debug.bin");
    bgfx::ShaderHandle fs = LoadShader(shaderDir + "/fs_debug.bin");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        LogWarn("debug lines: missing vs_debug/fs_debug in %s", shaderDir.c_str());
        return false;
    }
    program_ = bgfx::createProgram(vs, fs, true);

    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return bgfx::isValid(program_);
}

void DebugLines::Shutdown() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
}

void DebugLines::Draw(bgfx::ViewId view, const std::vector<DebugLine>& lines) {
    drawn_ = 0;
    if (!bgfx::isValid(program_) || lines.empty()) return;

    // Transient buffers are per-frame and finite, so a level's worth of
    // wireframe goes out in batches rather than being dropped whole.
    size_t at = 0;
    while (at < lines.size()) {
        const uint32_t available = bgfx::getAvailTransientVertexBuffer(
            static_cast<uint32_t>((lines.size() - at) * 2), layout_);
        const uint32_t count = std::min<uint32_t>(available & ~1u,
                                                  static_cast<uint32_t>((lines.size() - at) * 2));
        if (count < 2) break;

        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, count, layout_);
        LineVertex* v = reinterpret_cast<LineVertex*>(tvb.data);
        for (uint32_t i = 0; i < count; i += 2) {
            const DebugLine& line = lines[at + i / 2];
            v[i] = {line.a[0], line.a[1], line.a[2], line.abgr};
            v[i + 1] = {line.b[0], line.b[1], line.b[2], line.abgr};
        }

        // No depth test at all, the way coronas are drawn. A collision hull
        // sits exactly on the surface the renderer already drew, so depth
        // testing rejects nearly every segment of it - which looks precisely
        // like the wireframe not working. Drawing over the scene also answers
        // the question this view exists for: where the shapes are relative to
        // what you can see.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_PT_LINES | BGFX_STATE_BLEND_ALPHA);
        bgfx::setVertexBuffer(0, &tvb, 0, count);
        bgfx::submit(view, program_);

        at += count / 2;
        drawn_ += count / 2;
    }
}

} // namespace painful
