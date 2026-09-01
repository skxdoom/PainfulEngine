#pragma once
#include "../Assets/Tweaks.h"
#include "../Assets/Hke.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace painful {

class Level;
class TemplateCache;
struct MapMesh;

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

// A script body that has moved, in the terms the script layer holds
// transforms in: position plus an engine-order (w,x,y,z) quaternion.
// One contact between two script bodies, as the listener recorded it during
// the step. The engine reports these to the scripts as
// COLLISION_WITH_OTHER_ENTITY; see ScriptEngine::TickCollisions.
struct ScriptContact {
    int slotA = -1, slotB = -1;
    float point[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};   // pointing from A toward B
    // The two velocities AS THE CONTACT WAS RECORDED, mid-step and before the
    // solver has spent the impact. By the time the scripts run, the bodies have
    // already stopped - and the impact speed is the whole question they ask.
    float velA[3] = {0, 0, 0};
    float velB[3] = {0, 0, 0};
};

struct ScriptBodyPose {
    int slot = -1;
    float pos[3] = {0, 0, 0};
    float quatWXYZ[4] = {1, 0, 0, 0};
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

    // --- the script-driven path (the WORLD.*/ENTITY.PO_* natives) ---
    // The natives create bodies one by one as the scripts ask, instead of
    // Load()'s template sweep; the property setters mirror the PO_Set*
    // surface, which CObject:PO_Create calls right after creation.

    // The static world from a map mesh, in the same construction Load uses.
    // WORLD.Init arrives right after and refines the surface via
    // SetWorldSurface.
    void LoadWorldMesh(const MapMesh& map, float worldScale, const std::string& dataRoot);
    // WORLD.Init(massScale, friction, restitution, ...) - the world surface.
    void SetWorldSurface(float massScale, float friction, float restitution);
    // Fixed settle steps after a level load, so furniture is in place before
    // the first frame (Load does the same internally).
    void Settle(int steps);

    // One dynamic body for a script entity. Exactly one of modelName /
    // packName is set; the shape comes from the same mesh the renderer
    // draws. Returns a body slot, or -1 when the mesh cannot be resolved.
    int CreateScriptBody(int bodyType, const std::string& modelName,
                         const std::string& packName, const std::string& packMesh,
                         float scale, const float pos[3], const float rotWXYZ[4],
                         const std::string& dataRoot,
                         // ECollisionGroups from Definitions.lua. 7 is
                         // Noncolliding - a projectile, which must touch
                         // nothing - and 0 means the script named none.
                         int collisionGroup = 0);
    bool ScriptBodyExists(int slot) const;
    // The contacts recorded during the last step, then cleared. Only pairs where
    // BOTH sides are script bodies: a prop hitting the static world is not a
    // COLLISION_WITH_OTHER_ENTITY, which is an entity-to-entity message.
    void CollectScriptContacts(std::vector<ScriptContact>& out);
    void SetScriptBodyMass(int slot, float mass);
    void SetScriptBodyFriction(int slot, float friction);
    void SetScriptBodyRestitution(int slot, float restitution);
    void SetScriptBodyLinearDamping(int slot, float damping);
    void SetScriptBodyAngularDamping(int slot, float damping);
    // ENTITY.PO_EnableGravity. PhysicsObject::EnableGravity (0x1018c4e0) sets
    // the body's OWN gravity - the world vector when on, zero when off - which
    // is Jolt's gravity factor. A projectile flies straight by turning it off.
    void SetScriptBodyGravityFactor(int slot, float factor);
    // Takes a body out of the solver: kinematic, and in the layer that pairs
    // with nothing. ENTITY.RemoveFromIntersectionSolver is the scripts' way of
    // saying a thing is driven rather than simulated - the rocket asks for it
    // explicitly, having been created in the Particles group.
    void MakeScriptBodyNonColliding(int slot);
    // Teleports the body where the scripts put the entity.
    void SetScriptBodyPose(int slot, const float pos[3], const float rotWXYZ[4]);
    // PO_Enable on a prop: wakes or sleeps the body.
    void SetScriptBodyEnabled(int slot, bool enabled);

    // Takes a body out of the dynamic simulation: it still blocks everything
    // that sweeps against it, but nothing can push it and it cannot tumble.
    //
    // This is what a MONSTER is. ENTITY.PO_SetMonsterType sets a flag on the
    // physics object (Engine.dll 0x101313C0, bit 2 at PhysicsObject+0x74) and
    // the engine then moves it from the vector PO_Move stores rather than by
    // simulating it. A monster left dynamic is a barrel with legs - the player
    // bowls it over and it ends up inside the level.
    // The radius also gets corrected here, because the shape a prop wants is
    // not the shape a character wants: CreateScriptBody sizes a sphere by the
    // LARGEST half-extent, which for a T-posed humanoid is its arm span. Pass
    // <= 0 to keep the body's existing radius.
    // The monster body: three stacked spheres, which is what BodyTypes.Fatter
    // builds. k is the sizer's own working scalar (scale * 0.2) and
    // rootOffsetY the negated ROOOT height - both from the engine's own rule,
    // see MonsterBodyScale.
    void SetScriptBodyKinematic(int slot, float k, float rootOffsetY = 0.f);
    // ENTITY.SetVelocity / GetVelocity. Setting one wakes the body: a
    // projectile is created, given its velocity and expected to fly.
    void SetScriptBodyVelocity(int slot, const float v[3]);
    bool GetScriptBodyVelocity(int slot, float out[3]) const;
    // ENTITY.PO_Hit / WORLD.HitPhysicObject: an impulse at a world point, so
    // a shot shoves what it lands on and spins it about the point it struck.
    void AddScriptBodyImpulse(int slot, const float at[3], const float impulse[3]);
    // The world-space mesh radius, which is what PO_GetMaxSphereRay reports.
    float ScriptBodyRadius(int slot) const;
    // Where Jolt actually put the body, world space. Settles placement.
    bool ScriptBodyBounds(int slot, float lo[3], float hi[3]) const;
    void RemoveScriptBody(int slot);
    // Where the simulation has put the script bodies (slot in .slot).
    void CollectScriptPoses(std::vector<ScriptBodyPose>& out, bool activeOnly = true) const;


    // --- ragdolls (the .hke) ---
    //
    // Settings are built once per MODEL and shared; each call instances one.
    // scale converts the file's units to the world's - the .hke is authored in
    // the model's own space, ten times the world's, which is the same *0.1 the
    // renderer applies.
    //
    // Returns a slot, or -1 when the definition has no usable bodies.
    int CreateRagdoll(const std::string& model, const Hke& def, float scale);
    void RemoveRagdoll(int slot);
    bool RagdollExists(int slot) const;

    // The BONES the parts correspond to, in part order. The caller fills one
    // world matrix per entry for SetRagdollPose, which is the only way it can
    // know the order: the parts are the .hke's bodies, topologically sorted,
    // not the model's bones.
    const std::vector<std::string>& RagdollBones(int slot) const;

    // Poses every part from world-space bone matrices - row-major, row-vector,
    // 16 floats each, the form ScriptEngine already builds for the draw.
    //
    // kinematic is the LIVE monster: Ragdoll::Animate in the original, bodies
    // driven along the animation so a shot has something to hit. Dropping it
    // is death - Ragdoll::Activate - after which the solver owns the pose and
    // GetRagdollPose is what the renderer should draw.
    void SetRagdollPose(int slot, const float* boneMatrices, bool kinematic);
    bool GetRagdollPose(int slot, float* boneMatrices) const;
    // Is this ragdoll being simulated rather than driven? MDL.IsRagdollActive.
    bool RagdollActive(int slot) const;
    // MDL.SetRagdollLinearDamping / AngularDamping / Friction, and the mass
    // CActor:EnableRagdoll pushes in right after activating one. The .hke
    // supplies all of these; the scripts override them per monster.
    void SetRagdollDamping(int slot, float linear, float angular);
    void SetRagdollFriction(int slot, float friction);
    void SetRagdollMass(int slot, float mass);
    // A rigid spin of the whole corpse about Y through its centre of mass:
    // each limb gets the angular velocity AND the linear velocity that
    // rotating about the centre implies, or the limbs just spin in place.
    void SetRagdollSpin(int slot, float yawRate);
    // ENTITY.PO_ScaleInertiaTensor - s_Physics.InertiaTensorMultiplier, 0.1 on
    // every monster that declares one, applied 15 ticks after death.
    void ScaleRagdollInertia(int slot, float k);
    // ENTITY.PO_Hit on a corpse, and RagdollSelfExplosion: an impulse at a
    // world point, applied to whichever limb is nearest it.
    void AddRagdollImpulse(int slot, const float at[3], const float impulse[3]);

    // Steps the simulation. Jolt wants a fixed step, so this accumulates.
    void Update(float dt);

    // Moves a point through the world as a sphere, sliding along whatever it
    // hits, and writes the result back. This is what lets the free camera
    // collide without being a simulated body - the engine has the same idea in
    // PhysicsObject::SetFlying, where the player keeps its shape but not its
    // gravity.
    //
    // solidProps=true makes every body block, which is what the PLAYER
    // needs - props are things you stand on. Left false, bodies lighter than
    // Tweak.PlayerMove.MaximalItemPushMass are passed straight through; that
    // is a free-camera affordance so it can press into a barrel and have the
    // probe body shove it, and it is why the player used to walk through
    // barrels while still standing on the heavier, pinned coffins.
    // ignoreSlot passes one script body straight through, for a body that is
    // sweeping ITSELF through the world (see CameraBlockerFilter).
    void SlideSphere(float pos[3], const float delta[3], float radius,
                     bool solidProps = false, int ignoreSlot = -1,
                     bool collideWithPlayer = false,
                     bool* separatedFromCharacter = nullptr) const;

    // True when a sphere at this position overlaps anything solid.
    bool SphereOverlaps(const float pos[3], float radius) const;

    // What a line trace found. bodySlot is the script body that was hit, or
    // -1 for the static world - which is what tells WORLD.LineTrace's callers
    // apart, since ENTITY.IsFixedMesh branches on exactly that.
    struct RayHit {
        float distance = 0.f;
        float point[3] = {0, 0, 0};
        float normal[3] = {0, 0, 0};
        int bodySlot = -1;
    };

    // WORLD.LineTrace and friends. staticOnly restricts it to the world mesh,
    // which is LineTraceFixedGeom. `exclude` lists script body slots to pass
    // straight through: the scripts keep that set themselves through
    // ENTITY.Add/RemoveFromIntersectionSolver, so a projectile does not hit
    // the thing that fired it.
    bool RayCast(const float from[3], const float to[3], RayHit& out,
                 bool staticOnly = false, const int* exclude = nullptr,
                 size_t excludeCount = 0) const;

    // Pushes a sphere out of anything it is inside, and reports how many
    // overlaps it had to resolve. SlideSphere does this before every move:
    // a cast that starts inside geometry hits at zero distance whichever way
    // it goes, which is indistinguishable from being wedged for good.
    int Depenetrate(float pos[3], float radius, int iterations = 4,
                    bool solidProps = false, int ignoreSlot = -1,
                    bool collideWithPlayer = false,
                    bool* separatedFromCharacter = nullptr) const;

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

    // The PLAYER's own pusher body, distinct from the camera's.
    //
    // SlideSphere is a query and a query touches nothing, so the pawn walks
    // through corpses and loose props without either noticing. The camera has
    // had a kinematic body for exactly this reason since the free-camera work;
    // the player needs its own because the camera's is deliberately three
    // times fatter (1.2 against the player's 0.4) and only exists while the
    // free camera is flying.
    //
    // Radius comes from the recovered player shape: EngineGame::CreatePlayer
    // asks for BodyTypes.Player at bodyScale 1.0 and the sizer builds four
    // spheres of which the widest is 0.4.
    void SetPawnProbeRadius(float radius);
    void MovePawnProbe(const float pos[3], bool push);

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
    // Script bodies - items, debris, anything the scripts made with PO_Create -
    // are drawn too, in their own colour: they are the ones that go wrong, and
    // they were the ones this never showed.
    //
    // includeStatic false leaves the level out, which is the view you want when
    // the question is about a prop and the world is just in the way.
    void CollectDebugLines(const float around[3], float radius,
                           std::vector<DebugLine>& out,
                           bool includeStatic = true) const;

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
    void CreatePawnProbe();
    // The collidable map geometry as one static body; shared by Load and
    // LoadWorldMesh.
    bool BuildStaticWorld(const MapMesh& map, float worldScale);
    // Tweak.lua and gravity; level independent, read once.
    void LoadTweaks(const std::string& dataRoot);

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
