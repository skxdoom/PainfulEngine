#pragma once
#include <cmath>

namespace painful {

// Simple fly camera: yaw/pitch look with WASD movement. Enough to inspect a
// level; the real player controller will come from the game scripts later.
struct Camera {
    float pos[3] = {0.f, 0.f, 0.f};
    float yaw = 0.f;      // radians, around world up
    float pitch = 0.f;    // radians, clamped to avoid gimbal flip
    float fovDegrees = 70.f;
    // Has to clear the closest the eye can ever get to a surface, or standing
    // against a wall cuts a hole in it. The pawn's collision sphere is 0.40
    // and the slide keeps a 0.02 skin, so the eye can be 0.38 from a wall -
    // 0.5 was inside that, which is why walls clipped. 0.1 leaves nearly four
    // times the margin and still costs nothing in depth precision: the far
    // plane is the level's own FarClipDist (around 1024), so the ratio stays
    // near 10^4, well inside what a 24-bit buffer resolves.
    float nearPlane = 0.1f;
    // PAINFUL_NEAR overrides it, to tell near-plane clipping apart from
    // missing geometry: a viewmodel held at the eye is the one thing in the
    // scene close enough to be sliced by it.

    float farPlane = 8000.f;
    // Units per second. A unit is about a metre - the player body is two units
    // tall - so this is already several times a running pace; shift multiplies
    // it by four for crossing a level.
    float moveSpeed = 30.f;

    void Forward(float out[3]) const {
        const float cp = std::cos(pitch);
        out[0] = std::cos(yaw) * cp;
        out[1] = std::sin(pitch);
        out[2] = std::sin(yaw) * cp;
    }

    void Right(float out[3]) const {
        // cross(forward, up) in a right-handed system.
        out[0] = -std::sin(yaw);
        out[1] = 0.f;
        out[2] = std::cos(yaw);
    }

    void Look(float deltaYaw, float deltaPitch) {
        constexpr float kLimit = 1.55f;   // just under 90 degrees
        yaw += deltaYaw;
        pitch += deltaPitch;
        if (pitch > kLimit) pitch = kLimit;
        if (pitch < -kLimit) pitch = -kLimit;
    }

    void Move(float forwardAmount, float rightAmount, float upAmount) {
        float f[3], r[3];
        Forward(f);
        Right(r);
        for (int i = 0; i < 3; ++i) pos[i] += f[i] * forwardAmount + r[i] * rightAmount;
        pos[1] += upAmount;
    }
};

} // namespace painful
