#pragma once
#include <cmath>
#include "../Core/Vectors.h"

namespace painful {

// Simple fly camera: yaw/pitch look with WASD movement. Enough to inspect a
// level; the real player controller will come from the game scripts later.
struct Camera {
    Vec3 pos;
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

    // The view axes. Returned rather than written through an out-parameter -
    // every caller wants the value.
    Vec3 Forward() const {
        const float cp = std::cos(pitch);
        return Vec3(std::cos(yaw) * cp, std::sin(pitch), std::sin(yaw) * cp);
    }

    Vec3 Right() const {
        // cross(forward, up) in a right-handed system.
        return Vec3(-std::sin(yaw), 0.f, std::cos(yaw));
    }

    void Look(float deltaYaw, float deltaPitch) {
        constexpr float kLimit = 1.55f;   // just under 90 degrees
        yaw += deltaYaw;
        pitch += deltaPitch;
        if (pitch > kLimit) pitch = kLimit;
        if (pitch < -kLimit) pitch = -kLimit;
    }

    void Move(float forwardAmount, float rightAmount, float upAmount) {
        pos += Forward() * forwardAmount + Right() * rightAmount;
        pos[1] += upAmount;
    }
};

// The free camera collides as a sphere this wide.
//
// The player body's own widest sphere is 0.4 - EngineGame::CreatePlayer asks
// for BodyTypes.Player at bodyScale 1.0, and the shape factory builds that as a
// stack of four spheres in units of 0.2, the widest of them 2.0 units of that.
// The camera is deliberately fatter: it is not a player, it has no body to see
// clipping into a wall, and at player width it slides so close to surfaces that
// the near plane cuts through them.
constexpr float kCameraRadius = 1.2f;

// How far around the camera the static world's wireframe is collected. The
// whole level is 300k triangles, so the debug view is local by necessity.
constexpr float kPhysicsDebugRadius = 20.f;

} // namespace painful
