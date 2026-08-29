#include "PhysicsWorld.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "../Assets/Dat.h"
#include "../Assets/Pkmdl.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "Level.h"
#include "Templates.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <filesystem>
#include <thread>

namespace painful {

namespace {

// Two object layers, which is all a world of static geometry and loose props
// needs: things that never move and things that do. Jolt uses the pairing to
// skip static-against-static entirely.
namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
} // namespace Layers

namespace BroadPhase {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
} // namespace BroadPhase

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        // Static against static is never interesting.
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhase::kCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::kMoving ? BroadPhase::kMoving : BroadPhase::kNonMoving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhase::kMoving ? "moving" : "static";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        return layer == Layers::kMoving || broad == BroadPhase::kMoving;
    }
};

// What stops the camera, as opposed to what it shoves aside.
//
// The engine draws this line by MASS: Tweak.PlayerMove.MaximalItemPushMass is
// 2500, and the player walks through anything lighter rather than being stopped
// by it. Without that the camera's own query treats a barrel as a wall - it
// halts an inch short of one and never presses into it, so the body that does
// the pushing barely moves and the push looks feeble.
//
// It also drops the camera's own body, which sits exactly where the camera is.
class CameraBlockerFilter final : public JPH::BodyFilter {
public:
    CameraBlockerFilter(const JPH::BodyID& ignore, float maxPushMass)
        : ignore_(ignore), maxPushMass_(maxPushMass) {}

    bool ShouldCollide(const JPH::BodyID& id) const override { return id != ignore_; }

    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if (body.GetID() == ignore_) return false;
        // The world, and anything pinned in place, always blocks.
        if (body.GetMotionType() != JPH::EMotionType::Dynamic) return true;
        const JPH::MotionProperties* motion = body.GetMotionProperties();
        if (motion == nullptr) return true;
        const float inverseMass = motion->GetInverseMass();
        if (inverseMass <= 0.f) return true;
        return 1.f / inverseMass > maxPushMass_;
    }

private:
    JPH::BodyID ignore_;
    float maxPushMass_;
};

void TraceToLog(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    LogInfo("jolt: %s", buffer);
}

// Jolt's globals are process wide, so they are set up once and left alone -
// tearing them down between levels would invalidate every shape still held.
struct JoltRuntime {
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = TraceToLog;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltRuntime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

void EnsureJolt() {
    static JoltRuntime runtime;
    (void)runtime;
}

// Room for the props a level places, plus the one static body. Painkiller's
// busiest maps place a few hundred items; this leaves headroom without
// reserving anything expensive.
constexpr JPH::uint kMaxBodies = 8192;
constexpr JPH::uint kMaxBodyPairs = 16384;
constexpr JPH::uint kMaxContactConstraints = 8192;
// Jolt is written for a fixed step. Anything longer than this is a stall - a
// level load or a debugger break - and simulating it in full would fling
// everything across the map.
constexpr float kStep = 1.f / 60.f;
constexpr int kMaxStepsPerFrame = 4;

// A cap on the points handed to the hull builder, for a mesh detailed enough
// to make hull building the slowest part of a level load. It has to be high:
// thinning drops points blindly, and dropping the wrong one shrinks the shape.
// At 128 it was cutting the bottom off barrels, which then settled a unit into
// the air.
constexpr size_t kMaxHullPoints = 2048;

// The points a prop's shape is built from, in its own mesh space.
struct MeshPoints {
    JPH::Array<JPH::Vec3> points;
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};

    void Add(const float p[3]) {
        points.push_back(JPH::Vec3(p[0], p[1], p[2]));
        for (int c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], p[c]);
            hi[c] = std::max(hi[c], p[c]);
        }
    }
    bool empty() const { return points.empty(); }
    float radius() const {
        float r = 0.f;
        for (int c = 0; c < 3; ++c) r = std::max(r, (hi[c] - lo[c]) * 0.5f);
        return r;
    }
};

// Last resort for an absurdly detailed mesh. This samples blindly, so it can
// lose a point that was on the hull - only reach for it well past the point
// where the shape's accuracy stops mattering.
void Thin(MeshPoints& mesh) {
    if (mesh.points.size() <= kMaxHullPoints) return;
    const size_t stride = mesh.points.size() / kMaxHullPoints + 1;
    JPH::Array<JPH::Vec3> kept;
    for (size_t i = 0; i < mesh.points.size(); i += stride) kept.push_back(mesh.points[i]);
    mesh.points = std::move(kept);
}

bool PackPoints(const std::string& itemsRoot, const std::string& packName,
                const std::string& meshName, MeshPoints& out) {
    const std::string path = itemsRoot + "/" + packName;
    if (!FileSystem::Get().Exists(path)) return false;

    DatPack pack;
    if (!DatPack::Load(path, pack)) return false;

    for (const MapObject& o : pack.objects) {
        // o.Mesh selects one object; when it matches nothing, the whole pack
        // is the mesh - the same rule the renderer follows.
        if (!meshName.empty() && o.name != meshName && pack.objects.size() > 1) continue;
        // Raw vertices, WITHOUT the object transform. The renderer uploads pack
        // meshes exactly this way, and a collision shape that does not match
        // what is drawn is worse than none - it was placing barrel hulls
        // several units from their barrels, which is what the hull view showed.
        for (size_t v = 0; v < o.vertexCount(); ++v) {
            float p[3];
            o.position(v, p);
            out.Add(p);
        }
    }
    return !out.empty();
}

bool ModelPoints(const std::string& modelsRoot, const std::string& modelName, MeshPoints& out) {
    const std::string path = modelsRoot + "/" + modelName + ".pkmdl";
    if (!FileSystem::Get().Exists(path)) return false;

    Model model;
    if (!Model::Load(path, model)) return false;
    for (const ModelMesh& mesh : model.meshes) {
        for (size_t v = 0; v + 7 < mesh.verts.size(); v += 8) {
            const float p[3] = {mesh.verts[v], mesh.verts[v + 1], mesh.verts[v + 2]};
            out.Add(p);
        }
    }
    return !out.empty();
}

} // namespace

struct PhysicsWorld::Impl {
    JPH::TempAllocatorImpl temp{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                  std::max(1, static_cast<int>(
                                                  std::thread::hardware_concurrency()) - 1)};
    BroadPhaseLayerInterfaceImpl broadPhaseLayers;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhase;
    ObjectLayerPairFilterImpl objectPairs;
    JPH::PhysicsSystem system;

    JPH::BodyID worldBody;
    size_t worldTriangles = 0;
    float accumulator = 0.f;

    // One per placed entity that became a physics object, so the renderer can
    // be told where the simulation put it.
    struct Prop {
        JPH::BodyID body;
        size_t entity = 0;
    };
    std::vector<Prop> props;
    size_t unresolvedProps = 0;

    // Bodies the scripts created through ENTITY.PO_Create; the slot index is
    // what the script layer holds. Removed slots keep an invalid id so the
    // indices of the others stay stable.
    struct ScriptBody {
        JPH::BodyID body;
        float radius = 0.f;    // world-space mesh radius, for PO_GetMaxSphereRay
    };
    std::vector<ScriptBody> scriptBodies;

    JPH::BodyID probe;
    float probePos[3] = {0, 0, 0};
    bool probePush = false;

    Impl() {
        system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, broadPhaseLayers,
                    objectVsBroadPhase, objectPairs);
    }
};

PhysicsWorld::PhysicsWorld() {
    EnsureJolt();
    impl_ = std::make_unique<Impl>();
}

PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::loaded() const { return !impl_->worldBody.IsInvalid(); }
size_t PhysicsWorld::staticTriangles() const { return impl_->worldTriangles; }
size_t PhysicsWorld::bodyCount() const { return impl_->system.GetNumBodies(); }
size_t PhysicsWorld::props() const { return impl_->props.size(); }
size_t PhysicsWorld::unresolvedProps() const { return impl_->unresolvedProps; }

void PhysicsWorld::Clear() {
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    JPH::BodyIDVector all;
    impl_->system.GetBodies(all);
    for (const JPH::BodyID& id : all) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    impl_->worldBody = JPH::BodyID();
    impl_->probe = JPH::BodyID();
    impl_->worldTriangles = 0;
    impl_->accumulator = 0.f;
    impl_->props.clear();
    impl_->scriptBodies.clear();
    impl_->unresolvedProps = 0;
    settings_ = PhysicsSettings();
}

void PhysicsWorld::SetProbeRadius(float radius) {
    if (radius == probeRadius_) return;
    probeRadius_ = radius;
    CreateProbe();
}

void PhysicsWorld::CreateProbe() {
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    if (!impl_->probe.IsInvalid()) {
        bodies.RemoveBody(impl_->probe);
        bodies.DestroyBody(impl_->probe);
        impl_->probe = JPH::BodyID();
    }
    if (probeRadius_ <= 0.f) return;

    JPH::SphereShapeSettings shape(probeRadius_);
    shape.SetEmbedded();
    JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) return;

    JPH::BodyCreationSettings body(
        result.Get(),
        JPH::RVec3(impl_->probePos[0], impl_->probePos[1], impl_->probePos[2]),
        JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::kMoving);
    // Swept, not stepped. The camera crosses more than this body's own width
    // in a single step at anything above a walk - and with shift held it
    // covers 2 units against a radius of 1.2 - so a discrete body would pass
    // straight through props without ever touching them.
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    impl_->probe = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
}


void PhysicsWorld::MoveProbe(const float pos[3], bool push) {
    float jump = 0.f;
    for (int c = 0; c < 3; ++c) {
        const float d = pos[c] - impl_->probePos[c];
        jump += d * d;
    }
    for (int c = 0; c < 3; ++c) impl_->probePos[c] = pos[c];
    impl_->probePush = push;
    if (impl_->probe.IsInvalid()) return;

    // A jump of this size is not a movement, it is a teleport - a level
    // change, a respawn, or noclip crossing a wall. Sweeping through it would
    // drag everything in the way along. The threshold is generous because the
    // body sweeps rather than steps: a real move, even at shift speed on a
    // stuttering frame, stays well under it, and setting it too low turned
    // fast flight into a teleport that pushed nothing.
    if (!push || jump > 400.f) {
        JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
        bodies.SetPosition(impl_->probe, JPH::RVec3(pos[0], pos[1], pos[2]),
                           JPH::EActivation::DontActivate);
        bodies.SetLinearVelocity(impl_->probe, JPH::Vec3::sZero());
    }
}

void PhysicsWorld::LoadTweaks(const std::string& dataRoot) {
    // Tweak.lua is level independent, so it is read once and kept.
    if (!tweaks_.loaded() && !dataRoot.empty()) {
        if (tweaks_.LoadFromDataRoot(dataRoot))
            LogInfo("physics: %zu tweak values from LScripts/Main/Tweak.lua", tweaks_.size());
        else
            LogWarn("physics: no LScripts/Main/Tweak.lua - using the shipped defaults");
    }
    settings_.gravity = tweaks_.Number("GlobalData.Gravity", settings_.gravity);
    maxPushMass_ = tweaks_.Number("PlayerMove.MaximalItemPushMass", maxPushMass_);
    impl_->system.SetGravity(JPH::Vec3(0.f, -settings_.gravity, 0.f));
}

void PhysicsWorld::Load(const Level& level, TemplateCache& templates,
                        const std::string& dataRoot) {
    Clear();
    LoadTweaks(dataRoot);
    settings_.meshFriction = level.info().meshFriction;

    if (!level.mapLoaded()) return;
    if (!BuildStaticWorld(level.map(), level.info().scale)) return;

    LoadProps(level, templates, dataRoot);
    // Clear() destroyed the camera's body along with everything else.
    CreateProbe();

    // The broad phase is built lazily otherwise, and the first query of the
    // frame would pay for the whole level.
    impl_->system.OptimizeBroadPhase();

    // Let the props settle before the level is ever drawn. They are created
    // awake and most of them have somewhere to fall, and a level that visibly
    // rains its own furniture into place on load is not what the original
    // does. A fixed number of fixed steps, so a screenshot of a given frame is
    // the same picture every time.
    for (int i = 0; i < 90; ++i) impl_->system.Update(kStep, 1, &impl_->temp, &impl_->jobs);

    LogInfo("physics: %zu static triangles, %zu props (%zu unresolved), "
            "gravity %.2f, friction %.2f",
            impl_->worldTriangles, impl_->props.size(), impl_->unresolvedProps,
            settings_.gravity, settings_.meshFriction);
}

bool PhysicsWorld::BuildStaticWorld(const MapMesh& map, float worldScale) {
    // The static world, from the same object set the original hands Havok:
    // MapObject::isCollidable rejects portals, zones, volumetric-light helpers
    // and anything named "noclip", and the original gives those no body either.
    // Triangles are built in RENDERED space - raw mesh coordinates times the
    // level o.Scale - which is the space entity positions and the camera live
    // in.
    JPH::VertexList vertices;
    JPH::IndexedTriangleList triangles;

    for (const MapObject& o : map.objects) {
        if (!o.isCollidable()) continue;
        const JPH::uint32 base = static_cast<JPH::uint32>(vertices.size());
        for (size_t v = 0; v < o.vertexCount(); ++v) {
            float p[3], w[3];
            o.position(v, p);
            // Every shipped map has this at identity, but honouring it costs
            // nothing and avoids a silent wrong answer if one ever does not.
            o.transform.TransformPoint(p[0], p[1], p[2], w);
            vertices.push_back(JPH::Float3(w[0] * worldScale, w[1] * worldScale,
                                           w[2] * worldScale));
        }
        for (size_t t = 0; t + 2 < o.indices.size(); t += 3) {
            const uint32_t a = o.indices[t], b = o.indices[t + 1], c = o.indices[t + 2];
            if (a >= o.vertexCount() || b >= o.vertexCount() || c >= o.vertexCount()) continue;
            // WOUND BACKWARDS ON PURPOSE. Jolt takes counter-clockwise as the
            // front face; the world exporter winds the other way, which is why
            // the renderer draws these meshes with CULL_CCW. Feeding them in
            // as authored gives every floor a downward face, and simulated
            // bodies fall through the level while queries - which can be told
            // to collide with back faces - still hit it.
            triangles.push_back(JPH::IndexedTriangle(base + a, base + c, base + b, 0));
        }
    }

    if (triangles.empty()) {
        LogWarn("physics: level has no collidable geometry");
        return false;
    }

    JPH::MeshShapeSettings meshSettings(std::move(vertices), std::move(triangles));
    // Map geometry has degenerate triangles here and there; Jolt refuses to
    // build a tree around them, so they go before it sees them.
    meshSettings.Sanitize();
    meshSettings.SetEmbedded();

    JPH::ShapeSettings::ShapeResult shape = meshSettings.Create();
    if (shape.HasError()) {
        LogWarn("physics: mesh shape failed: %s", shape.GetError().c_str());
        return false;
    }

    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                   JPH::EMotionType::Static, Layers::kNonMoving);
    body.mFriction = settings_.meshFriction;
    body.mRestitution = settings_.meshRestitution;

    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    impl_->worldBody = bodies.CreateAndAddBody(body, JPH::EActivation::DontActivate);
    impl_->worldTriangles = meshSettings.mIndexedTriangles.size();
    return true;
}

// The shape a prop body gets for a BodyTypes value, scaled into world units.
// FromMesh and its variants (4/5/7/11) become the mesh's convex hull - Jolt
// works out the centre of mass itself, which is the difference the original
// draws between FromMesh and FromMeshNotCentered, and a moving body cannot be
// a triangle mesh. Everything else (Simple / Sphere / Fatter / Default) is a
// sphere around the mesh.
static JPH::ShapeSettings::ShapeResult BuildScaledPropShape(MeshPoints& mesh,
                                                            int bodyType,
                                                            float finalScale) {
    JPH::ShapeSettings::ShapeResult shape;
    if (bodyType == 4 || bodyType == 5 || bodyType == 7 || bodyType == 11) {
        Thin(mesh);
        JPH::ConvexHullShapeSettings hull(mesh.points);
        hull.SetEmbedded();
        shape = hull.Create();
    } else {
        JPH::SphereShapeSettings sphere(std::max(0.05f, mesh.radius()));
        sphere.SetEmbedded();
        shape = sphere.Create();
    }
    if (shape.HasError()) return shape;

    JPH::ScaledShapeSettings scaled(shape.Get(), JPH::Vec3::sReplicate(finalScale));
    scaled.SetEmbedded();
    return scaled.Create();
}

void PhysicsWorld::LoadProps(const Level& level, TemplateCache& templates,
                             const std::string& dataRoot) {
    const std::string itemsRoot = dataRoot + "/Items";
    const std::string modelsRoot = dataRoot + "/Models";
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();

    const std::vector<Entity>& entities = level.entities();
    for (size_t index = 0; index < entities.size(); ++index) {
        const Entity& e = entities[index];

        // Only the entities whose own template chain calls PO_Create. Actors
        // are left out on purpose: CActor:PO_Create runs when a monster is
        // spawned, and spawning is script work that does not exist yet.
        if (e.type == "CActor") continue;
        // A placed instance can carry the call itself, which wins over its
        // template: Cathedral's barrels each declare
        // o.StartCommand = "o:PO_Create(BodyTypes.FromMesh)".
        int bodyType = TemplateCache::BodyTypeInScript(e.props.String("StartCommand"));
        if (bodyType < 0) bodyType = templates.PhysicsBodyType(e.baseObj);
        if (bodyType < 0) continue;
        // Ragdoll bodies need a skeleton driving them.
        if (bodyType == 15) continue;

        const double scale = templates.ResolveNumber(e.props, e.baseObj, "Scale", 1.0);
        const std::string pack = templates.ResolveString(e.props, e.baseObj, "Pack");

        MeshPoints mesh;
        float finalScale = 0.f;
        if (!pack.empty()) {
            const std::string meshName = templates.ResolveString(e.props, e.baseObj, "Mesh");
            if (!PackPoints(itemsRoot, pack, meshName, mesh)) { ++impl_->unresolvedProps; continue; }
            // Pack meshes share the world exporter's units, so o.Scale is a
            // plain multiplier; models are created at Scale * 0.1. Both rules
            // are literal in CItem.lua, and the shape has to match what the
            // renderer draws.
            finalScale = static_cast<float>(scale);
        } else {
            const std::string modelName = templates.ResolveString(e.props, e.baseObj, "Model");
            if (modelName.empty() || !ModelPoints(modelsRoot, modelName, mesh)) {
                ++impl_->unresolvedProps;
                continue;
            }
            finalScale = static_cast<float>(scale) * 0.1f;
        }
        if (finalScale <= 0.f) { ++impl_->unresolvedProps; continue; }

        JPH::ShapeSettings::ShapeResult final = BuildScaledPropShape(mesh, bodyType, finalScale);
        if (final.HasError()) { ++impl_->unresolvedProps; continue; }

        float rot[9];
        ReadRotation(e.props, rot);
        // The engine's 3x3 is row-vector; Jolt is column-vector, and the two
        // are transposes, so engine row j is Jolt column j.
        JPH::Mat44 basis = JPH::Mat44::sIdentity();
        for (int j = 0; j < 3; ++j)
            basis.SetColumn3(j, JPH::Vec3(rot[j * 3 + 0], rot[j * 3 + 1], rot[j * 3 + 2]));

        // CObject:PO_Create reads exactly these off the object, and only sets
        // what the object declares:
        //     if self.Restitution then ENTITY.PO_SetRestitution(...) end
        //     if self._Class == "CItem" then ENTITY.PO_SetFriction(entity, 1) end
        //     if self.Friction then ENTITY.PO_SetFriction(...) end
        // The level's DefaultMeshRestitution is the WORLD mesh's surface, not
        // every prop's - handing it to the props made barrels bounce and
        // topple when they landed.
        const bool pinned = templates.ResolveBool(e.baseObj, "Pinned", false);
        const double mass = templates.ResolveNumber(e.props, e.baseObj, "Mass", 0.0);
        const double friction =
            templates.ResolveNumber(e.props, e.baseObj, "Friction", e.type == "CItem" ? 1.0 : -1.0);
        const bool hasRestitution = e.props.Has("Restitution") ||
                                    templates.ResolveHas(e.baseObj, "Restitution");
        const double restitution = templates.ResolveNumber(e.props, e.baseObj, "Restitution", 0.0);

        JPH::BodyCreationSettings body(
            final.Get(), JPH::RVec3(e.pos[0], e.pos[1], e.pos[2]), basis.GetQuaternion(),
            pinned ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
            pinned ? Layers::kNonMoving : Layers::kMoving);
        if (friction >= 0.0) body.mFriction = static_cast<float>(friction);
        if (hasRestitution) body.mRestitution = static_cast<float>(restitution);
        // Props are small and the level's floors are single triangles, so a
        // discrete step lets a falling barrel pass straight through one.
        if (!pinned) body.mMotionQuality = JPH::EMotionQuality::LinearCast;
        if (mass > 0.0) {
            body.mOverrideMassProperties =
                JPH::EOverrideMassProperties::CalculateInertia;
            body.mMassPropertiesOverride.mMass =
                static_cast<float>(mass * settings_.activeMeshesMassScale);
        }
        // Awake, so a level settles when it loads: props are authored resting
        // on the floor but not exactly on it, and one that hangs in the air
        // until something happens to touch it is not a physics object, it is a
        // decoration. Jolt puts each one to sleep again as soon as it stops,
        // so the cost is a second or two at load and nothing after that.
        const JPH::BodyID id = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
        if (id.IsInvalid()) { ++impl_->unresolvedProps; continue; }

        impl_->props.push_back({id, index});
    }
}

void PhysicsWorld::Update(float dt) {
    if (dt <= 0.f) return;
    impl_->accumulator = std::min(impl_->accumulator + dt, kStep * kMaxStepsPerFrame);

    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    while (impl_->accumulator >= kStep) {
        // The camera's body is aimed at the step that is about to run, not at
        // the frame that just went by. Driving it with the frame time instead
        // makes it move a different distance from the one it was given - at
        // 120 fps, every other frame ran no step at all and the one after it
        // moved twice as far - so it lagged behind the camera or overshot
        // whatever it should have pushed, and the push landed or missed
        // depending on the frame rate.
        if (impl_->probePush && !impl_->probe.IsInvalid()) {
            bodies.MoveKinematic(impl_->probe,
                                 JPH::RVec3(impl_->probePos[0], impl_->probePos[1],
                                            impl_->probePos[2]),
                                 JPH::Quat::sIdentity(), kStep);
        }
        impl_->system.Update(kStep, 1, &impl_->temp, &impl_->jobs);
        impl_->accumulator -= kStep;
    }
}

void PhysicsWorld::ActivateProps() {
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (const Impl::Prop& prop : impl_->props) bodies.ActivateBody(prop.body);
}

void PhysicsWorld::CollectPoses(std::vector<BodyPose>& out, bool activeOnly) const {
    out.clear();
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    for (const Impl::Prop& prop : impl_->props) {
        if (activeOnly && !bodies.IsActive(prop.body)) continue;

        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(prop.body, position, rotation);

        BodyPose pose;
        pose.entity = prop.entity;
        for (int c = 0; c < 3; ++c) pose.pos[c] = static_cast<float>(position[c]);
        // Back the other way: Jolt column j is the engine's row j.
        const JPH::Mat44 basis = JPH::Mat44::sRotation(rotation);
        for (int j = 0; j < 3; ++j) {
            const JPH::Vec3 column = basis.GetColumn3(j);
            for (int c = 0; c < 3; ++c) pose.rot[j * 3 + c] = column[c];
        }
        out.push_back(pose);
    }
}

// ------------------------------------------------------------ script bodies

// The engine applies its textbook quaternion matrix to ROW vectors, which in
// standard column convention is the rotation by the CONJUGATE - the same
// transpose CollectPoses and LoadProps handle for matrices, expressed on the
// quaternion itself.
static JPH::Quat EngineQuatToJolt(const float q[4]) {
    JPH::Quat j(-q[1], -q[2], -q[3], q[0]);
    return j.LengthSq() < 1e-12f ? JPH::Quat::sIdentity() : j.Normalized();
}

static void JoltQuatToEngine(const JPH::Quat& j, float out[4]) {
    out[0] = j.GetW();
    out[1] = -j.GetX();
    out[2] = -j.GetY();
    out[3] = -j.GetZ();
}

void PhysicsWorld::LoadWorldMesh(const MapMesh& map, float worldScale,
                                 const std::string& dataRoot) {
    Clear();
    LoadTweaks(dataRoot);
    if (!BuildStaticWorld(map, worldScale)) return;
    CreateProbe();
    LogInfo("physics: %zu static triangles (script path), gravity %.2f",
            impl_->worldTriangles, settings_.gravity);
}

void PhysicsWorld::SetWorldSurface(float massScale, float friction, float restitution) {
    settings_.activeMeshesMassScale = massScale > 0.f ? massScale : 1.f;
    settings_.meshFriction = friction;
    settings_.meshRestitution = restitution;
    if (impl_->worldBody.IsInvalid()) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    bodies.SetFriction(impl_->worldBody, friction);
    bodies.SetRestitution(impl_->worldBody, restitution);
}

void PhysicsWorld::Settle(int steps) {
    impl_->system.OptimizeBroadPhase();
    for (int i = 0; i < steps; ++i)
        impl_->system.Update(kStep, 1, &impl_->temp, &impl_->jobs);
}

int PhysicsWorld::CreateScriptBody(int bodyType, const std::string& modelName,
                                   const std::string& packName,
                                   const std::string& packMesh, float scale,
                                   const float pos[3], const float rotWXYZ[4],
                                   const std::string& dataRoot) {
    if (impl_->worldBody.IsInvalid()) return -1;   // no world, nothing to rest on

    MeshPoints mesh;
    if (!packName.empty()) {
        if (!PackPoints(dataRoot + "/Items", packName, packMesh, mesh)) return -1;
    } else {
        if (modelName.empty() || !ModelPoints(dataRoot + "/Models", modelName, mesh))
            return -1;
    }
    if (scale <= 0.f) return -1;

    const float radius = mesh.radius() * scale;   // world-space
    JPH::ShapeSettings::ShapeResult shape = BuildScaledPropShape(mesh, bodyType, scale);
    if (shape.HasError()) return -1;

    // The same body configuration LoadProps uses; mass, friction and the
    // rest arrive through the PO_Set* calls CObject:PO_Create makes next.
    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(pos[0], pos[1], pos[2]),
                                   EngineQuatToJolt(rotWXYZ), JPH::EMotionType::Dynamic,
                                   Layers::kMoving);
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;

    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
    if (id.IsInvalid()) return -1;

    impl_->scriptBodies.push_back({id, radius});
    return int(impl_->scriptBodies.size() - 1);
}

bool PhysicsWorld::ScriptBodyExists(int slot) const {
    return slot >= 0 && size_t(slot) < impl_->scriptBodies.size() &&
           !impl_->scriptBodies[slot].body.IsInvalid();
}

void PhysicsWorld::SetScriptBodyMass(int slot, float mass) {
    if (!ScriptBodyExists(slot) || mass <= 0.f) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return;
    lock.GetBody().GetMotionProperties()->ScaleToMass(
        mass * settings_.activeMeshesMassScale);
}

void PhysicsWorld::SetScriptBodyFriction(int slot, float friction) {
    if (!ScriptBodyExists(slot)) return;
    impl_->system.GetBodyInterface().SetFriction(impl_->scriptBodies[slot].body,
                                                 friction);
}

void PhysicsWorld::SetScriptBodyRestitution(int slot, float restitution) {
    if (!ScriptBodyExists(slot)) return;
    impl_->system.GetBodyInterface().SetRestitution(impl_->scriptBodies[slot].body,
                                                    restitution);
}

void PhysicsWorld::SetScriptBodyLinearDamping(int slot, float damping) {
    if (!ScriptBodyExists(slot) || damping < 0.f) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (lock.Succeeded() && lock.GetBody().IsDynamic())
        lock.GetBody().GetMotionProperties()->SetLinearDamping(damping);
}

void PhysicsWorld::SetScriptBodyAngularDamping(int slot, float damping) {
    if (!ScriptBodyExists(slot) || damping < 0.f) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (lock.Succeeded() && lock.GetBody().IsDynamic())
        lock.GetBody().GetMotionProperties()->SetAngularDamping(damping);
}

void PhysicsWorld::SetScriptBodyPose(int slot, const float pos[3],
                                     const float rotWXYZ[4]) {
    if (!ScriptBodyExists(slot)) return;
    impl_->system.GetBodyInterface().SetPositionAndRotation(
        impl_->scriptBodies[slot].body, JPH::RVec3(pos[0], pos[1], pos[2]),
        EngineQuatToJolt(rotWXYZ), JPH::EActivation::Activate);
}

float PhysicsWorld::ScriptBodyRadius(int slot) const {
    return ScriptBodyExists(slot) ? impl_->scriptBodies[slot].radius : 0.f;
}

void PhysicsWorld::RemoveScriptBody(int slot) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    bodies.RemoveBody(impl_->scriptBodies[slot].body);
    bodies.DestroyBody(impl_->scriptBodies[slot].body);
    impl_->scriptBodies[slot].body = JPH::BodyID();
}

void PhysicsWorld::CollectScriptPoses(std::vector<ScriptBodyPose>& out,
                                      bool activeOnly) const {
    out.clear();
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    for (size_t slot = 0; slot < impl_->scriptBodies.size(); ++slot) {
        const JPH::BodyID id = impl_->scriptBodies[slot].body;
        if (id.IsInvalid()) continue;
        if (activeOnly && !bodies.IsActive(id)) continue;

        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(id, position, rotation);

        ScriptBodyPose pose;
        pose.slot = int(slot);
        for (int c = 0; c < 3; ++c) pose.pos[c] = float(position[c]);
        JoltQuatToEngine(rotation, pose.quatWXYZ);
        out.push_back(pose);
    }
}

void PhysicsWorld::CollectDebugLines(const float around[3], float radius,
                                     std::vector<DebugLine>& out) const {
    out.clear();
    if (!loaded()) return;

    const JPH::BodyLockInterfaceNoLock& locks = impl_->system.GetBodyLockInterfaceNoLock();
    const JPH::AABox near(JPH::Vec3(around[0] - radius, around[1] - radius, around[2] - radius),
                          JPH::Vec3(around[0] + radius, around[1] + radius, around[2] + radius));

    auto emit = [&out](const JPH::Float3& a, const JPH::Float3& b, uint32_t abgr) {
        DebugLine line;
        line.a[0] = a.x; line.a[1] = a.y; line.a[2] = a.z;
        line.b[0] = b.x; line.b[1] = b.y; line.b[2] = b.z;
        line.abgr = abgr;
        out.push_back(line);
    };

    auto wireframe = [&](JPH::BodyID id, const JPH::AABox& box, uint32_t abgr) {
        JPH::BodyLockRead lock(locks, id);
        if (!lock.Succeeded()) return;

        // GetTriangles only works on LEAF shapes, and a prop is not one - it is
        // a ScaledShape around a hull, whose GetTrianglesNext returns zero and
        // (with asserts compiled out) says nothing about it. So collect the
        // leaves first, which is what Jolt's own assert message asks for.
        JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
        lock.GetBody().GetTransformedShape().CollectTransformedShapes(box, leaves);

        for (const JPH::TransformedShape& leaf : leaves.mHits) {
            JPH::Shape::GetTrianglesContext context;
            leaf.GetTrianglesStart(context, box, JPH::RVec3::sZero());
            for (;;) {
                JPH::Float3 verts[JPH::Shape::cGetTrianglesMinTrianglesRequested * 3];
                const int count = leaf.GetTrianglesNext(
                    context, JPH::Shape::cGetTrianglesMinTrianglesRequested, verts);
                if (count == 0) break;
                for (int t = 0; t < count; ++t) {
                    emit(verts[t * 3 + 0], verts[t * 3 + 1], abgr);
                    emit(verts[t * 3 + 1], verts[t * 3 + 2], abgr);
                    emit(verts[t * 3 + 2], verts[t * 3 + 0], abgr);
                }
            }
        }
    };

    // The static world in dim blue, so the props read against it. Colours are
    // 0xAABBGGRR, the packing bgfx expects for a Uint8 colour attribute.
    wireframe(impl_->worldBody, near, 0x60ff8040u);

    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    for (const Impl::Prop& prop : impl_->props) {
        // Awake bodies in green, sleeping ones in yellow: whether a prop is
        // simulating at all is the first thing to look at.
        const uint32_t abgr = bodies.IsActive(prop.body) ? 0xff00ff00u : 0xff00ffffu;
        wireframe(prop.body, JPH::AABox::sBiggest(), abgr);
    }
}

bool PhysicsWorld::SphereOverlaps(const float pos[3], float radius) const {
    if (!loaded()) return false;

    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();

    JPH::CollideShapeSettings settings;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    // The camera's own body sits exactly where the camera is, so every query
    // made from there would hit it first.
    const CameraBlockerFilter blockers(impl_->probe, maxPushMass_);
    impl_->system.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sOne(),
        JPH::RMat44::sTranslation(JPH::RVec3(pos[0], pos[1], pos[2])), settings,
        JPH::RVec3::sZero(), collector, {}, {}, blockers);
    return collector.HadHit();
}

int PhysicsWorld::Depenetrate(float pos[3], float radius, int iterations) const {
    if (!loaded()) return 0;

    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const CameraBlockerFilter blockers(impl_->probe, maxPushMass_);

    int resolved = 0;
    for (int pass = 0; pass < iterations; ++pass) {
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mCollectFacesMode = JPH::ECollectFacesMode::NoFaces;

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        impl_->system.GetNarrowPhaseQuery().CollideShape(
            &sphere, JPH::Vec3::sOne(),
            JPH::RMat44::sTranslation(JPH::RVec3(pos[0], pos[1], pos[2])), settings,
            JPH::RVec3::sZero(), collector, {}, {}, blockers);
        if (collector.mHits.empty()) break;

        // ONE overlap per pass - the deepest - and then look again.
        //
        // Applying every hit in a pass is the obvious thing to write and it is
        // badly wrong: a floor is hundreds of triangles, so a sphere resting an
        // inch into one overlaps a dozen of them and gets pushed out a dozen
        // times over. That launched the camera several units into the air on
        // the first frame, far enough that it sailed over anything it was meant
        // to walk into.
        const JPH::CollideShapeResult* deepest = nullptr;
        for (const JPH::CollideShapeResult& hit : collector.mHits)
            if (deepest == nullptr || hit.mPenetrationDepth > deepest->mPenetrationDepth)
                deepest = &hit;
        if (deepest->mPenetrationDepth <= 0.f) break;

        // mPenetrationAxis moves shape 2 out of the collision, so the sphere -
        // shape 1 - goes the other way.
        const JPH::Vec3 out = -deepest->mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
        for (int c = 0; c < 3; ++c) pos[c] += out[c] * deepest->mPenetrationDepth;
        ++resolved;
    }
    return resolved;
}

void PhysicsWorld::SlideSphere(float pos[3], const float delta[3], float radius) const {
    if (!loaded()) {
        for (int c = 0; c < 3; ++c) pos[c] += delta[c];
        return;
    }

    // Get out of anything first. A cast that starts inside geometry reports a
    // hit at zero distance in every direction, which is indistinguishable from
    // being wedged - and being wedged for good is exactly what it looks like.
    Depenetrate(pos, radius);

    JPH::Vec3 at(pos[0], pos[1], pos[2]);
    JPH::Vec3 remaining(delta[0], delta[1], delta[2]);
    if (remaining.IsNearZero()) {
        for (int c = 0; c < 3; ++c) pos[c] = at[c];
        return;
    }

    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const CameraBlockerFilter blockers(impl_->probe, maxPushMass_);

    // Keeps the sphere just clear of the surface so the next cast starts
    // outside it. The original calls the same idea PhantomTolerance.
    constexpr float kSkin = 0.02f;

    // Three planes is enough for a corner; anything past that is a crack, and
    // stopping there is better than squeezing through the world.
    for (int iteration = 0; iteration < 3; ++iteration) {
        if (remaining.IsNearZero()) break;

        JPH::RShapeCast cast(&sphere, JPH::Vec3::sOne(),
                             JPH::RMat44::sTranslation(JPH::RVec3(at)), remaining);
        JPH::ShapeCastSettings settings;
        // A sphere that starts inside geometry has no useful hit to report -
        // let it move out rather than locking the camera in place.
        settings.mReturnDeepestPoint = false;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;

        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        // Without ignoring the camera's own body, the cast starts inside a
        // sphere of its own radius and stops dead at the first step - the
        // camera collides with itself and cannot move at all.
        impl_->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(),
                                                      collector, {}, {}, blockers);
        if (!collector.HadHit()) {
            at += remaining;
            break;
        }

        const float fraction = std::max(0.f, collector.mHit.mFraction);
        const JPH::Vec3 travelled = remaining * fraction;
        // Back off along the travel direction rather than along the normal:
        // the normal can be nearly perpendicular to the motion on a grazing
        // hit, where backing off would barely help.
        const float length = travelled.Length();
        if (length > kSkin) at += travelled * ((length - kSkin) / length);

        // The contact normal points from the surface toward the sphere.
        const JPH::Vec3 normal = -collector.mHit.mPenetrationAxis.Normalized();
        remaining -= travelled;
        remaining -= normal * remaining.Dot(normal);
    }

    for (int c = 0; c < 3; ++c) pos[c] = at[c];
}

} // namespace painful
