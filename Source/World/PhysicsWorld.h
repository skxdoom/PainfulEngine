#pragma once
#include "../Assets/Tweaks.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace painful {

class Level;
class TemplateCache;

// One segment of the collision-shape wireframe, in world space.
struct DebugLine {
    float a[3] = {0, 0, 0};
    float b[3] = {0, 0, 0};
    uint32_t abgr = 0xffffffff;
};

// A simulated body that has moved, in the terms the renderer places entities
// in: the level entity it belongs to, its position, and the row-vector 3x3
// ReadRotation produces.
struct BodyPose {
    size_t entity = 0;
    float pos[3] = {0, 0, 0};
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// What the level hands the physics world at load. The original passes exactly
// these: CLevel.lua calls
//   WORLD.Init(Physics.ActiveMeshesMassScale, Physics.DefaultMeshFriction,
//              Physics.DefaultMeshRestitution, Deactivator.Delay,
//              Deactivator.MaxPosDiff)
// right after WORLD.LoadMap. The defaults are CLevel.lua's own class defaults.
struct PhysicsSettings {
    float gravity = 2.f * 9.81f;      // Tweak.GlobalData.Gravity
    float meshFriction = 0.5f;        // o.Physics.DefaultMeshFriction
    float meshRestitution = 0.5f;     // o.Physics.DefaultMeshRestitution
    float activeMeshesMassScale = 1.f;
};

// The physics world: Jolt standing in for Havok.
//
// PainEngine is a Havok game - PhysicsWorld, PhysicsObject, the PO_* natives
// and the ragdolls are all Havok wrappers. Jolt is the roadmap's replacement,
// and this is the bring-up: the static level geometry as one mesh body, a
// stepped simulation, and shape casts for whatever needs to move through the
// world without a body of its own.
//
// Jolt itself is kept entirely inside the implementation, the way SDL is kept
// out of the renderer's headers - nothing else in the engine includes it.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Builds the world from a level: the collidable map geometry becomes one
    // static body, and the level's o.Physics block sets its surface
    // properties. dataRoot is where Tweak.lua is read from.
    void Load(const Level& level, TemplateCache& templates, const std::string& dataRoot);
    void Clear();

    // Steps the simulation. Jolt wants a fixed step, so this accumulates.
    void Update(float dt);

    // Moves a point through the world as a sphere, sliding along whatever it
    // hits, and writes the result back. This is what lets the free camera
    // collide without being a simulated body - the engine has the same idea in
    // PhysicsObject::SetFlying, where the player keeps its shape but not its
    // gravity.
    void SlideSphere(float pos[3], const float delta[3], float radius) const;

    // True when a sphere at this position overlaps anything solid.
    bool SphereOverlaps(const float pos[3], float radius) const;

    // Pushes a sphere out of anything it is inside, and reports how many
    // overlaps it had to resolve. SlideSphere does this before every move:
    // a cast that starts inside geometry hits at zero distance whichever way
    // it goes, which is indistinguishable from being wedged for good.
    int Depenetrate(float pos[3], float radius, int iterations = 4) const;

    // The camera's body in the simulation: a kinematic sphere that follows it.
    //
    // SlideSphere is a query, and a query touches nothing - which is why the
    // camera could press into a barrel and the barrel would not notice. A
    // kinematic body does notice: it pushes loose props out of the way and
    // wakes them, and being kinematic it is not itself pushed back, so the
    // camera keeps flying exactly as it did.
    void SetProbeRadius(float radius);
    float probeRadius() const { return probeRadius_; }
    // Aims the body at a position; Update drives it there. It is deliberately
    // NOT moved here: a kinematic body moves by having a velocity during a
    // simulation step, and the steps are a fixed 1/60 that has nothing to do
    // with how often this is called.
    //
    // push false teleports it instead of sweeping, which is what noclip and a
    // level change want - a sweep across half a level would rake everything in
    // between.
    void MoveProbe(const float pos[3], bool push);

    // Where the simulation has put the props. With activeOnly (the default)
    // only bodies that are awake are reported, so a settled level costs
    // nothing per frame.
    void CollectPoses(std::vector<BodyPose>& out, bool activeOnly = true) const;

    // Wakes every prop, for asking what the simulation would do with a level
    // that has already settled.
    void ActivateProps();

    // The wireframe of every collision shape near a point: the prop bodies
    // whatever their distance, and the static world within the radius, which
    // would otherwise be a few hundred thousand triangles. This is the only
    // way to see what the physics world actually thinks the level is, as
    // opposed to what the renderer draws.
    void CollectDebugLines(const float around[3], float radius,
                           std::vector<DebugLine>& out) const;

    const PhysicsSettings& settings() const { return settings_; }
    const Tweaks& tweaks() const { return tweaks_; }

    bool loaded() const;
    size_t staticTriangles() const;
    size_t bodyCount() const;
    // Placed entities that became physics objects, and the ones whose template
    // asked for one but whose mesh could not be resolved.
    size_t props() const;
    size_t unresolvedProps() const;

private:
    // The placed props: entities whose template chain calls PO_Create.
    void LoadProps(const Level& level, TemplateCache& templates, const std::string& dataRoot);
    // (Re)builds the camera's kinematic body at the current radius.
    void CreateProbe();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    PhysicsSettings settings_;
    Tweaks tweaks_;
    float probeRadius_ = 0.f;
    // Tweak.PlayerMove.MaximalItemPushMass: the engine's own line between what
    // the player walks through and what stops it.
    float maxPushMass_ = 2500.f;
};

} // namespace painful
