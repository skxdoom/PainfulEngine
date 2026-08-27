#pragma once
#include "../World/PhysicsWorld.h"
#include "Camera.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

// Draws world-space line segments, for looking at things that are not
// normally visible - collision shapes first among them.
//
// The physics world is the one part of the engine with no picture of its own:
// the renderer draws a barrel from its model, the simulation collides against
// a convex hull built from that model, and nothing says the two agree. This
// draws what the simulation actually holds.
class DebugLines {
public:
    ~DebugLines() { Shutdown(); }

    bool Init(const std::string& shaderDir);
    void Shutdown();

    void Draw(bgfx::ViewId view, const std::vector<DebugLine>& lines);

    size_t drawn() const { return drawn_; }

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    size_t drawn_ = 0;
};

} // namespace painful
