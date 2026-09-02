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
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Math/Math.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>
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
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace painful {

namespace {

// Object layers. Static geometry, the things that move, and three special
// cases the pair filter below tells apart.
namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
// ECollisionGroups.Noncolliding (7). A projectile is NOT a rigid body in this
// engine: Stake:OnCreateEntity asks for PO_Create(..., Noncolliding), gives it
// a constant velocity with gravity off, and finds its own hits with
// Stake:Trace. The body exists only to carry a position and a model, so it has
// to touch nothing - a colliding one stops dead against the first thing it
// meets, which is exactly what ours did.
constexpr JPH::ObjectLayer kNoCollide = 2;
// The camera's and the pawn's pusher bodies. They shove props and nothing
// else: a grenade or rocket is spawned at the player's head and must not be
// stopped by the sphere that is standing there, and no trace should ever land
// on them.
constexpr JPH::ObjectLayer kProbe = 3;
// ECollisionGroups.Missile (5) and Particles (8): grenades, rockets, shell
// casings. Simulated against the world, props, monsters and each other, but
// never against the probes.
constexpr JPH::ObjectLayer kMissile = 4;
// A LIVE monster's ragdoll limbs, posed along its animation (Ragdoll::Animate).
// Traces land on them; nothing simulates against them. A monster's own body
// is dynamic and sits inside them, and a limb that could push it would eject
// its owner every step.
constexpr JPH::ObjectLayer kHitbox = 5;
constexpr JPH::ObjectLayer kCount = 6;
} // namespace Layers

namespace BroadPhase {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
} // namespace BroadPhase

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        // A non-colliding body pairs with nothing at all, and so does a live
        // limb - it is there to be traced, not simulated.
        if (a == Layers::kNoCollide || b == Layers::kNoCollide) return false;
        if (a == Layers::kHitbox || b == Layers::kHitbox) return false;
        // A probe only ever shoves the ordinary moving bodies.
        if (a == Layers::kProbe || b == Layers::kProbe)
            return (a == Layers::kMoving) != (b == Layers::kMoving) &&
                   (a == Layers::kProbe) != (b == Layers::kProbe);
        // Missiles pass through each other. BoltGunHeater:AltFire launches
        // ten bombs 0.05 apart with a 0.165 radius each; touching, every one
        // of them counted its neighbours as hits and blew up in the barrel.
        if (a == Layers::kMissile && b == Layers::kMissile) return false;
        // Static against static is never interesting.
        return a != Layers::kNonMoving || b != Layers::kNonMoving;
    }
};

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhase::kCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::kNonMoving ? BroadPhase::kNonMoving : BroadPhase::kMoving;
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
        // Rejected here as well as in the narrow phase, so a projectile costs
        // nothing in the broadphase either.
        if (layer == Layers::kNoCollide || layer == Layers::kHitbox) return false;
        if (layer == Layers::kProbe) return broad == BroadPhase::kMoving;
        return layer != Layers::kNonMoving || broad == BroadPhase::kMoving;
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
//
// A maxPushMass of kSolidProps disables the mass rule entirely, so every body
// blocks. That is what the PLAYER wants: you stand on a barrel, you do not
// walk through it. The pass-through is a free-camera affordance, not a
// gameplay rule - the mass line governs what can be SHOVED, and the shoving
// is done by the kinematic probe body, not by the query.
constexpr float kSolidProps = -1.f;

class CameraBlockerFilter final : public JPH::BodyFilter {
public:
    // `also` is a second body to pass through. A monster sweeps its own shape
    // through a world its own body is standing in, so without this it is
    // wedged inside itself and never moves a millimetre.
    //
    // `pawn` is the player's own pusher, which sits exactly where the player
    // is for the same reason the camera's does - a query that starts inside it
    // reports a hit at zero distance in every direction, and the player cannot
    // move at all.
    CameraBlockerFilter(const JPH::BodyID& ignore, float maxPushMass,
                        const JPH::BodyID& also = JPH::BodyID(),
                        const JPH::BodyID& pawn = JPH::BodyID())
        : ignore_(ignore), also_(also), pawn_(pawn), maxPushMass_(maxPushMass) {}

    bool ShouldCollide(const JPH::BodyID& id) const override {
        return id != ignore_ && id != also_ && id != pawn_;
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override {
        const JPH::BodyID id = body.GetID();
        if (id == ignore_ || id == also_ || id == pawn_) return false;
        if (maxPushMass_ < 0.f) return true;
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
    JPH::BodyID pawn_;
    JPH::BodyID also_;
    float maxPushMass_;
};

// What a body or a sweep can stand on and be stopped by: everything but the
// probes and a live monster's limbs. Traces use SolidLayerFilter instead,
// which does land on limbs.
class SweepLayerFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override {
        return layer != Layers::kNoCollide && layer != Layers::kProbe &&
               layer != Layers::kHitbox;
    }
};
const SweepLayerFilter kSweepLayer;

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

// Records contacts between script bodies for the frame.
//
// Jolt calls this from the physics JOBS, so several threads at once: the list
// is guarded, and nothing is looked up or dispatched here. Turning a BodyID
// back into a script slot and deciding who wants to hear about it happens on
// the game thread, once the step is over.
//
// OnContactAdded only - persisted contacts are a body resting on another and
// would report every frame. The scripts' own MinTime gate exists for the
// remaining chatter.
class ScriptContactListener final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override {
        NoteCharacter(a, b, manifold);
        Pending p;
        p.a = a.GetID();
        p.b = b.GetID();
        const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
        p.point[0] = float(point.GetX());
        p.point[1] = float(point.GetY());
        p.point[2] = float(point.GetZ());
        p.normal[0] = manifold.mWorldSpaceNormal.GetX();
        p.normal[1] = manifold.mWorldSpaceNormal.GetY();
        p.normal[2] = manifold.mWorldSpaceNormal.GetZ();
        // Sampled HERE, mid-step: this callback runs before the contact is
        // solved, so these are the closing velocities. Read after the step they
        // are both near zero, and every impact would measure as a nudge.
        const JPH::Vec3 va = a.GetLinearVelocity();
        const JPH::Vec3 vb = b.GetLinearVelocity();
        for (int k = 0; k < 3; ++k) {
            p.velA[k] = va[k];
            p.velB[k] = vb[k];
        }
        // How hard, along the contact normal. Kept so that a full buffer can
        // drop the GENTLEST contact rather than the newest: the scripts only
        // care about hard ones, and dropping by arrival silently threw away a
        // vase's landing while two dozen other props were settling.
        const JPH::Vec3 rel = va - vb;
        p.strength = std::fabs(rel.Dot(manifold.mWorldSpaceNormal));

        std::lock_guard<std::mutex> guard(lock_);
        if (pending_.size() < kMaxPerStep) {
            pending_.push_back(p);
            return;
        }
        auto weakest = std::min_element(pending_.begin(), pending_.end(),
                                        [](const Pending& l, const Pending& r) {
                                            return l.strength < r.strength;
                                        });
        if (weakest != pending_.end() && weakest->strength < p.strength) *weakest = p;
    }

    struct Pending {
        JPH::BodyID a, b;
        float point[3];
        float normal[3];
        float velA[3];
        float velB[3];
        float strength = 0.f;
    };

    void Take(std::vector<Pending>& out) {
        std::lock_guard<std::mutex> guard(lock_);
        out.swap(pending_);
        pending_.clear();
    }
    void Peek(std::vector<Pending>& out) {
        std::lock_guard<std::mutex> guard(lock_);
        out = pending_;
    }

    // What each CHARACTER body was touching during the step, as the
    // direction it cannot move in. New and persisting contacts both count -
    // a body pressed against a wall reports the wall every step.
    struct CharContact {
        uint32_t body;
        float blocked[3];
    };
    const std::unordered_set<uint32_t>* characters = nullptr;
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b,
                            const JPH::ContactManifold& manifold,
                            JPH::ContactSettings&) override {
        NoteCharacter(a, b, manifold);
    }
    void NoteCharacter(const JPH::Body& a, const JPH::Body& b,
                       const JPH::ContactManifold& manifold) {
        if (!characters) return;
        const bool ca = characters->count(a.GetID().GetIndexAndSequenceNumber()) != 0;
        const bool cb = characters->count(b.GetID().GetIndexAndSequenceNumber()) != 0;
        if (!ca && !cb) return;
        // Only what will not give way: the world, the kinematic pushers, and
        // a dynamic body at least as heavy as the character - a gravestone,
        // a pinned-then-released stone. Another character or a light prop
        // keeps the contact impulse instead: that is how a crowd shuffles and
        // how a monster shoves a barrel aside, and clipping those left a
        // queue standing still. A heavy sleeping body was the gap: the
        // Cemetery's graves became bodies and monsters bounced off them the
        // way they had off walls.
        auto blocks = [this](const JPH::Body& me, const JPH::Body& other) {
            if (!other.IsDynamic()) return true;
            if (characters->count(other.GetID().GetIndexAndSequenceNumber())) return false;
            const JPH::MotionProperties* mine = me.GetMotionProperties();
            const JPH::MotionProperties* theirs = other.GetMotionProperties();
            if (!mine || !theirs) return true;
            const float myInv = mine->GetInverseMass(), theirInv = theirs->GetInverseMass();
            if (theirInv <= 0.f) return true;
            return theirInv <= myInv;   // at least my mass
        };
        const JPH::Vec3 n = manifold.mWorldSpaceNormal;   // from a into b
        std::lock_guard<std::mutex> guard(lock_);
        if (ca && blocks(a, b))
            charContacts_.push_back({a.GetID().GetIndexAndSequenceNumber(),
                                     {n.GetX(), n.GetY(), n.GetZ()}});
        if (cb && blocks(b, a))
            charContacts_.push_back({b.GetID().GetIndexAndSequenceNumber(),
                                     {-n.GetX(), -n.GetY(), -n.GetZ()}});
    }
    void TakeCharacterContacts(std::vector<CharContact>& out) {
        std::lock_guard<std::mutex> guard(lock_);
        out.swap(charContacts_);
        charContacts_.clear();
    }
    std::vector<CharContact> charContacts_;

private:
    // Big enough to hold a whole load-time settle: Settle() runs 90 steps with
    // no drain between them, and a level places over a hundred props.
    static constexpr size_t kMaxPerStep = 4096;
    std::mutex lock_;
    std::vector<Pending> pending_;
};

struct PhysicsWorld::Impl {
    JPH::TempAllocatorImpl temp{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                  std::max(1, static_cast<int>(
                                                  std::thread::hardware_concurrency()) - 1)};
    BroadPhaseLayerInterfaceImpl broadPhaseLayers;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhase;
    ObjectLayerPairFilterImpl objectPairs;
    JPH::PhysicsSystem system;
    ScriptContactListener contacts;

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
        // PO_Enable(false) takes the body OUT OF THE WORLD, not just to sleep -
        // see SetScriptBodyEnabled. Jolt asserts on a double add or remove, so
        // the state has to be tracked rather than inferred.
        bool inWorld = true;
        // EFreedomsOfRotation, as PhysicsObject+0x10 holds it. Decides what
        // SetMass does to the inertia: see SetScriptBodyMass.
        int freedomMode = 1;
        // PO_SetMass's value, 0 until the script sets one. CActor:PO_Create
        // sets the mass BEFORE PO_SetMonsterType, and rebuilding the body as
        // a character must not lose it.
        float mass = 0.f;
        int character = -1;    // index into characters, -1 for a prop
        // Active mesh record (AddMesh's pinned array, 0x34-byte entries):
        // the group byte at WorldMesh+0x7e2, whether the static twin still
        // stands, whether explosions may release it, and the bounds radius
        // the release test adds to the blast range.
        bool activeMesh = false;
        int activeGroup = -1;
        bool activePinned = false;
        bool activeEnabled = true;
        bool activeLevelScaled = false;   // Level_GetActiveMeshesData gave != 1
        float activeRadius = 0.f;
    };
    std::vector<ScriptBody> scriptBodies;
    // A character body's tick state: PhysicsObject+0x34 (wish), +0x4c (the
    // last commanded vector), +0x6c/+0x70 (movement const, floor flag), +0x71
    // (on floor), +0x60 (floor normal), +0x75 bit 3 (flying).
    struct Character {
        int slot = -1;
        float k = 0.f;             // 0.2 * bodyScale: the sizer's unit
        float rootOffsetY = 0.f;   // stack origin above the body position
        float wish[3] = {0, 0, 0};
        float lastWish[3] = {0, 0, 0};
        float influence = 0.5f;
        bool checkFloors = true;
        bool flying = false;
        bool onFloor = false;
        float floorNormal[3] = {0, 1, 0};
    };
    std::vector<Character> characters;
    Character* CharacterOf(int slot) {
        if (slot < 0 || size_t(slot) >= scriptBodies.size()) return nullptr;
        const int c = scriptBodies[size_t(slot)].character;
        if (c < 0 || size_t(c) >= characters.size()) return nullptr;
        return &characters[size_t(c)];
    }
    // Which script bodies are CHARACTERS (PO_SetMonsterType, so kinematic and
    // carried by their own mover). Depenetrate separates two characters
    // horizontally: see the comment there. Keyed on the raw id, since
    // JPH::BodyID has no std::hash.
    std::unordered_set<uint32_t> characterBodies;
    // Pinned active meshes: static twins a moving body may knock loose.
    std::unordered_set<uint32_t> pinnedActiveBodies;
    // Last step's blocking contacts per character body (the wall slide's input).
    std::vector<ScriptContactListener::CharContact> lastTouching;

    // Ragdoll settings are per MODEL and shared between every instance of it;
    // the bone order is the part order, which only the builder knows.
    std::unordered_map<std::string, JPH::Ref<JPH::RagdollSettings>> ragdollSettings;
    std::unordered_map<std::string, std::vector<std::string>> ragdollBones;
    struct RagdollInst {
        JPH::Ref<JPH::Ragdoll> ragdoll;
        std::vector<std::string> bones;
        bool simulated = false;     // dynamic (dead) rather than driven (alive)
    };
    std::vector<RagdollInst> ragdolls;
    // Each instance gets its own collision group so two corpses in a heap
    // collide with each other while neither collides with itself.
    JPH::CollisionGroup::GroupID nextRagdollGroup = 1;

    JPH::BodyID probe;
    JPH::BodyID pawnProbe;
    float pawnProbePos[3] = {0, 0, 0};
    float pawnProbeRadius = 0.f;
    float probePos[3] = {0, 0, 0};
    bool probePush = false;

    Impl() {
        contacts.characters = &characterBodies;
        system.SetContactListener(&contacts);
        system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, broadPhaseLayers,
                    objectVsBroadPhase, objectPairs);
        // Havok's material combine is the geometric mean for both friction and
        // restitution; Jolt's default takes the MAX restitution, which makes a
        // dead prop bounce off a lively floor. Havok is statically linked with
        // no symbols, so this is hkpMaterial's documented default rather than
        // a decompiled fact - see Docs/Reference/Physics.md.
        system.SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&,
                                        const JPH::Body& b, const JPH::SubShapeID&) {
            return std::sqrt(a.GetRestitution() * b.GetRestitution());
        });
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
    impl_->pawnProbe = JPH::BodyID();
    impl_->worldTriangles = 0;
    impl_->accumulator = 0.f;
    impl_->props.clear();
    impl_->scriptBodies.clear();
    impl_->characters.clear();
    impl_->characterBodies.clear();
    impl_->unresolvedProps = 0;
    settings_ = PhysicsSettings();
}

void PhysicsWorld::SetProbeRadius(float radius) {
    if (radius == probeRadius_) return;
    probeRadius_ = radius;
    CreateProbe();
    CreatePawnProbe();
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
        JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::kProbe);
    // Swept, not stepped. The camera crosses more than this body's own width
    // in a single step at anything above a walk - and with shift held it
    // covers 2 units against a radius of 1.2 - so a discrete body would pass
    // straight through props without ever touching them.
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    impl_->probe = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
}


// The player's pusher, rebuilt on the same schedule as the camera's: Clear()
// destroys every body in the system, so anything that is meant to outlive a
// level change has to be made again after one.
void PhysicsWorld::CreatePawnProbe() {
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    if (!impl_->pawnProbe.IsInvalid()) {
        bodies.RemoveBody(impl_->pawnProbe);
        bodies.DestroyBody(impl_->pawnProbe);
        impl_->pawnProbe = JPH::BodyID();
    }
    if (impl_->pawnProbeRadius <= 0.f) return;
    JPH::SphereShapeSettings shape(impl_->pawnProbeRadius);
    shape.SetEmbedded();
    JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) return;
    JPH::BodyCreationSettings body(
        result.Get(),
        JPH::RVec3(impl_->pawnProbePos[0], impl_->pawnProbePos[1], impl_->pawnProbePos[2]),
        JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::kProbe);
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    impl_->pawnProbe = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
}

void PhysicsWorld::SetPawnProbeRadius(float radius) {
    impl_->pawnProbeRadius = radius;
    CreatePawnProbe();
}

// Same contract as MoveProbe: aimed here, driven inside the fixed step, and
// teleported rather than swept across a jump that is really a respawn.
void PhysicsWorld::MovePawnProbe(const float pos[3], bool push) {
    float jump = 0.f;
    for (int c = 0; c < 3; ++c) {
        const float d = pos[c] - impl_->pawnProbePos[c];
        jump += d * d;
    }
    for (int c = 0; c < 3; ++c) impl_->pawnProbePos[c] = pos[c];
    if (impl_->pawnProbe.IsInvalid()) return;
    if (!push || jump > 400.f) {
        JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
        bodies.SetPosition(impl_->pawnProbe, JPH::RVec3(pos[0], pos[1], pos[2]),
                           JPH::EActivation::DontActivate);
        bodies.SetLinearVelocity(impl_->pawnProbe, JPH::Vec3::sZero());
    }
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
    if (!BuildStaticWorld(level.map(), level.info().scale, false)) return;

    LoadProps(level, templates, dataRoot);
    // Clear() destroyed the camera's body along with everything else.
    CreateProbe();
    CreatePawnProbe();

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

bool PhysicsWorld::BuildStaticWorld(const MapMesh& map, float worldScale,
                                    bool promoteActiveMeshes) {
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
        if (promoteActiveMeshes && o.isActiveMesh()) continue;   // CreateActiveMeshBody's
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
    if (bodyType == 2) {
        // BodyTypes.Fatter - what 66 of the 82 monsters that declare a body
        // type use, against 16 on Sphere and one on FromRagdoll. Collapsing it
        // into the sphere branch gave two thirds of the bestiary the wrong
        // shape, and all of them the same one.
        //
        // In the original this is the MULTI-PART case. The sizer (0x101B3E20)
        // builds a compound - FUN_10211640 is a refcounted container of child
        // shapes - and then, for every body type EXCEPT 2, collapses it into a
        // single derived convex shape via FUN_10211040. Fatter is the one that
        // keeps its parts, which is what the name is saying.
        //
        // A capsule is the approximation, not a recovered shape: the child
        // records are 32-byte pairs of vectors, which is the layout of a
        // segment with a radius, but no shape-type constant has been read to
        // confirm it. It is the right SHAPE for a walking character either way
        // - shoulders and legs you can slide along rather than a ball that
        // either blocks or does not.
        //
        // Radius is the SMALLER horizontal half-extent. The larger one is arms:
        // evilmonkv2's widest axis is its outstretched arms, 14.4 model units
        // against a body 2.9 deep, and sizing by that makes a monster wider
        // than it is tall that can never reach a wall.
        const float half[3] = {(mesh.hi[0] - mesh.lo[0]) * 0.5f,
                               (mesh.hi[1] - mesh.lo[1]) * 0.5f,
                               (mesh.hi[2] - mesh.lo[2]) * 0.5f};
        const float radius = std::max(0.05f, std::min(half[0], half[2]));
        // The cylinder is what is left of the height once the two hemispheres
        // have taken their radius; a squat body degenerates to a sphere.
        const float cylinder = std::max(0.f, half[1] - radius);
        JPH::CapsuleShapeSettings capsule(cylinder, radius);
        capsule.SetEmbedded();
        shape = capsule.Create();
        if (shape.HasError()) {
            JPH::SphereShapeSettings sphere(std::max(0.05f, mesh.radius()));
            sphere.SetEmbedded();
            shape = sphere.Create();
        }
    } else if (bodyType == 4 || bodyType == 5 || bodyType == 7 || bodyType == 11) {
        Thin(mesh);
        // A hull needs four points and a real volume. Debris packs are full of
        // pieces that have neither: a barrel's lid measures 2.18 x 0.15 x 2.18
        // and a stave is a plank, and Thin() can leave a nearly coplanar set
        // behind. Ask for a hull only when one can exist, and take the sphere
        // when the hull cannot be built - a wrong shape beats a dead load.
        if (mesh.points.size() >= 4) {
            JPH::ConvexHullShapeSettings hull(mesh.points);
            hull.SetEmbedded();
            shape = hull.Create();
        }
        if (mesh.points.size() < 4 || shape.HasError()) {
            JPH::SphereShapeSettings sphere(std::max(0.05f, mesh.radius()));
            sphere.SetEmbedded();
            shape = sphere.Create();
        }
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
        // The pawn's body is driven the same way and for the same reason.
        if (!impl_->pawnProbe.IsInvalid()) {
            bodies.MoveKinematic(impl_->pawnProbe,
                                 JPH::RVec3(impl_->pawnProbePos[0], impl_->pawnProbePos[1],
                                            impl_->pawnProbePos[2]),
                                 JPH::Quat::sIdentity(), kStep);
        }
        // Monsters are re-commanded per STEP, as PhysicsObject::Tick is run
        // per physics tick: the 0.5 carry-over is a per-tick decay.
        StepCharacters();
        impl_->system.Update(kStep, 1, &impl_->temp, &impl_->jobs);
        impl_->accumulator -= kStep;
        // PAINFUL_CHAR_TRACE: a character whose velocity the step changed by
        // more than 4 units/s, and what it was touching.
        static const bool traceChars = std::getenv("PAINFUL_CHAR_TRACE") != nullptr;
        if (traceChars) {
            for (const Impl::Character& ch : impl_->characters) {
                if (ch.slot < 0) continue;
                const Impl::ScriptBody& sb = impl_->scriptBodies[size_t(ch.slot)];
                if (sb.body.IsInvalid() || !sb.inWorld) continue;
                const JPH::Vec3 v = bodies.GetLinearVelocity(sb.body);
                const JPH::Vec3 cmd(ch.lastWish[0], ch.lastWish[1], ch.lastWish[2]);
                const JPH::Vec3 dv = v - cmd;
                static const float minDv = std::getenv("PAINFUL_CHAR_TRACE_MIN")
                                               ? float(std::atof(std::getenv("PAINFUL_CHAR_TRACE_MIN")))
                                               : 4.f;
                if (JPH::Vec3(dv.GetX(), 0.f, dv.GetZ()).Length() < minDv) continue;
                std::vector<ScriptContactListener::Pending> touching;
                impl_->contacts.Peek(touching);
                std::string partners;
                for (const auto& p : touching) {
                    const JPH::BodyID other = p.a == sb.body ? p.b : (p.b == sb.body ? p.a : JPH::BodyID());
                    if (other.IsInvalid()) continue;
                    char buf[96];
                    int slot = -1;
                    for (size_t i = 0; i < impl_->scriptBodies.size(); ++i)
                        if (impl_->scriptBodies[i].body == other) slot = int(i);
                    snprintf(buf, sizeof buf, " [%s%d n=%.2f,%.2f,%.2f]",
                             other == impl_->worldBody ? "world" : (slot >= 0 ? "slot" : "body"),
                             slot >= 0 ? slot : int(other.GetIndex()), p.normal[0], p.normal[1],
                             p.normal[2]);
                    partners += buf;
                }
                // The slide's own contact list for this body: what was clipped.
                for (const ScriptContactListener::CharContact& c : impl_->lastTouching) {
                    if (c.body != sb.body.GetIndexAndSequenceNumber()) continue;
                    char buf[64];
                    snprintf(buf, sizeof buf, " {clip %.2f,%.2f,%.2f}", c.blocked[0], c.blocked[1],
                             c.blocked[2]);
                    partners += buf;
                }
                const JPH::RVec3 pos = bodies.GetPosition(sb.body);
                LogInfo("CHAR slot %d cmd=(%.2f %.2f %.2f) got=(%.2f %.2f %.2f) at=(%.3f %.3f %.3f)%s",
                        ch.slot, cmd.GetX(), cmd.GetY(), cmd.GetZ(), v.GetX(), v.GetY(), v.GetZ(),
                        float(pos.GetX()), float(pos.GetY()), float(pos.GetZ()),
                        partners.empty() ? " (no contacts recorded)" : partners.c_str());
            }
        }
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
    if (!BuildStaticWorld(map, worldScale, true)) return;
    CreateProbe();
    CreatePawnProbe();
    LogInfo("physics: %zu static triangles (script path), gravity %.2f",
            impl_->worldTriangles, settings_.gravity);
}

void PhysicsWorld::SetWorldSurface(float massScale, float friction, float restitution) {
    settings_.activeMeshesMassScale = massScale > 0.f ? massScale : 1.f;
    settings_.meshFriction = friction;
    settings_.meshRestitution = restitution;
    ScaleUnscaledActiveMeshes(settings_.activeMeshesMassScale);
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
                                   const std::string& dataRoot, int collisionGroup,
                                   float sphereRadius) {
    if (impl_->worldBody.IsInvalid()) return -1;   // no world, nothing to rest on

    MeshPoints mesh;
    if (!packName.empty()) {
        if (!PackPoints(dataRoot + "/Items", packName, packMesh, mesh)) return -1;
    } else {
        if (modelName.empty() || !ModelPoints(dataRoot + "/Models", modelName, mesh))
            return -1;
    }
    if (scale <= 0.f) return -1;

    float radius = mesh.radius() * scale;   // world-space
    JPH::ShapeSettings::ShapeResult shape;
    if (sphereRadius > 0.f) {
        // BodyTypes.Sphere with an explicit scale: the sizer (0x101b3e20,
        // case 1) makes a sphere of radius scale * 0.2 * 5.5 and never looks
        // at the mesh. A grenade asks for 0.15 and gets 0.165.
        radius = sphereRadius;
        JPH::SphereShapeSettings sphere(sphereRadius);
        sphere.SetEmbedded();
        shape = sphere.Create();
    } else {
        shape = BuildScaledPropShape(mesh, bodyType, scale);
    }
    if (shape.HasError()) return -1;

    // The same body configuration LoadProps uses; mass, friction and the
    // rest arrive through the PO_Set* calls CObject:PO_Create makes next.
    // ECollisionGroups.Noncolliding (7) is the projectile case, and it is not a
    // simulated body at all - it is KINEMATIC, moved by the engine along a
    // straight line, exactly as a monster is moved rather than simulated.
    //
    // That is what makes every shot identical. Left dynamic, the solver owns it
    // and it stops for reasons that have nothing to do with the shot: ours lost
    // its velocity inside a single step with gravity off and nothing to collide
    // against. A projectile has no business being integrated.
    const bool projectile = collisionGroup == 7;

    // ECollisionGroups.Fixed (1) is RIGID, NOT SIMULATED.
    //
    // It is what the ambush barriers and the lifts are made with -
    // Slab.CItem's OnCreateEntity is
    //
    //     self:PO_Create(BodyTypes.FromMesh, nil, ECollisionGroups.Fixed)
    //     ENTITY.PO_SetMovedByExplosions(self._Entity, false)
    //
    // and the slab is then driven by script, a little further up its own Y
    // every tick, to rise into the player's path. C5L2_Winda (the lift) does
    // the same with FromMeshNonConvex.
    //
    // Dynamic is wrong for all of them twice over: the solver pushes them out
    // of the floor they are authored inside and they never rise at all, and
    // anything that touches one shoves it off its track. Kinematic is what
    // "fixed" means here - it blocks whatever runs into it, nothing moves it
    // but the script that owns it, and it passes through the static world it
    // is rising out of.
    const bool fixedRigid = collisionGroup == 1;
    // Missile (5) and Particles (8) are simulated like anything else but
    // never against the pusher bodies - see Layers::kMissile.
    const bool missile = collisionGroup == 5 || collisionGroup == 8;
    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(pos[0], pos[1], pos[2]),
                                   EngineQuatToJolt(rotWXYZ),
                                   (projectile || fixedRigid) ? JPH::EMotionType::Kinematic
                                                              : JPH::EMotionType::Dynamic,
                                   projectile ? Layers::kNoCollide
                                              : (missile ? Layers::kMissile : Layers::kMoving));
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    // The contact material every script body is born with, and keeps: the
    // sizer fills its hkpRigidBodyCinfo with restitution 0.9, both dampings 0,
    // and leaves friction at the Havok default of 0.5. PO_SetFriction and
    // PO_SetRestitution never reach the body (Docs/Reference/Physics.md).
    body.mFriction = 0.5f;
    body.mRestitution = 0.9f;
    body.mLinearDamping = 0.f;
    body.mAngularDamping = 0.f;

    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
    if (id.IsInvalid()) return -1;

    impl_->scriptBodies.push_back({id, radius, true, 1});
    return int(impl_->scriptBodies.size() - 1);
}

bool PhysicsWorld::ScriptBodyExists(int slot) const {
    return slot >= 0 && size_t(slot) < impl_->scriptBodies.size() &&
           !impl_->scriptBodies[slot].body.IsInvalid();
}

// Turns the step's raw contacts into script slots, on the game thread.
//
// Both sides must be script bodies. A prop striking the static world is a real
// contact but not this message - COLLISION_WITH_OTHER_ENTITY names two
// entities, and the world is not one.
void PhysicsWorld::CollectScriptContacts(std::vector<ScriptContact>& out) {
    out.clear();
    std::vector<ScriptContactListener::Pending> pending;
    impl_->contacts.Take(pending);
    if (pending.empty()) return;

    // BodyID -> slot, built per call rather than kept: bodies come and go every
    // frame and a stale map would report a contact against whatever took the
    // slot. The body count here is in the hundreds.
    // Keyed on the raw id: JPH::BodyID has no std::hash.
    std::unordered_map<uint32_t, int> slotOf;
    slotOf.reserve(impl_->scriptBodies.size());
    for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
        const auto& sb = impl_->scriptBodies[i];
        if (!sb.body.IsInvalid())
            slotOf[sb.body.GetIndexAndSequenceNumber()] = int(i);
    }

    for (const ScriptContactListener::Pending& p : pending) {
        const auto a = slotOf.find(p.a.GetIndexAndSequenceNumber());
        const auto b = slotOf.find(p.b.GetIndexAndSequenceNumber());
        // ONE side is enough. Requiring both was wrong and it hid the common
        // case: almost everything a prop hits is the STATIC WORLD - a vase
        // pushed off a balcony lands on the floor, not on another prop - and
        // that collision is exactly the one a destructible breaks on. The world
        // side reports slot -1, which becomes entity 0 in the message, the same
        // stand-in a world hit already uses in the traces.
        if (a == slotOf.end() && b == slotOf.end()) continue;
        ScriptContact c;
        c.slotA = a == slotOf.end() ? -1 : a->second;
        c.slotB = b == slotOf.end() ? -1 : b->second;
        for (int k = 0; k < 3; ++k) {
            c.point[k] = p.point[k];
            c.normal[k] = p.normal[k];
            c.velA[k] = p.velA[k];
            c.velB[k] = p.velB[k];
        }
        out.push_back(c);
    }
}

// PhysicsObject::SetMass (0x10189510) branches on the freedom-of-rotation
// mode: AllAxes and FullFree rescale the inertia with the mass, every other
// mode sets the mass alone and leaves the inertia the mode chose.
void PhysicsWorld::SetScriptBodyMass(int slot, float mass) {
    if (!ScriptBodyExists(slot) || mass <= 0.f) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return;
    const float m = mass * settings_.activeMeshesMassScale;
    impl_->scriptBodies[slot].mass = m;
    const int mode = impl_->scriptBodies[slot].freedomMode;
    JPH::MotionProperties* mp = lock.GetBody().GetMotionProperties();
    if (mode == 2 || mode == 3) mp->ScaleToMass(m);
    else mp->SetInverseMass(1.f / m);
}

// PhysicsObject::SetFreedomOfRotation (0x10189a30), as an inertia tensor.
// A locked axis is 3.4e38 there and an inverse inertia of zero here; a free
// single axis has inertia 10; HardTurn is isotropic softness * 10; AllAxes and
// FullFree take the shape's own inertia at the current mass.
void PhysicsWorld::SetScriptBodyFreedomOfRotation(int slot, int mode, float softness) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return;
    impl_->scriptBodies[slot].freedomMode = mode;
    JPH::Body& body = lock.GetBody();
    JPH::MotionProperties* mp = body.GetMotionProperties();

    if (mode == 2 || mode == 3) {
        JPH::MassProperties props = body.GetShape()->GetMassProperties();
        const float invMass = mp->GetInverseMass();
        if (invMass > 0.f) props.ScaleToMass(1.f / invMass);
        mp->SetMassProperties(JPH::EAllowedDOFs::All, props);
        return;
    }
    float inv[3] = {0, 0, 0};                    // locked, locked, locked
    switch (mode) {
    case 1: inv[1] = 0.1f; break;                // YAxis
    case 5: inv[0] = 0.1f; break;                // XAxis
    case 6: inv[2] = 0.1f; break;                // ZAxis
    case 4: {                                    // HardTurn
        const float i = std::max(1e-4f, softness * 10.f);
        inv[0] = inv[1] = inv[2] = 1.f / i;
        break;
    }
    default: break;                              // Disabled: no rotation
    }
    mp->SetInverseInertia(JPH::Vec3(inv[0], inv[1], inv[2]), JPH::Quat::sIdentity());
}

void PhysicsWorld::SetScriptBodyLinearDamping(int slot, float damping) {
    if (!ScriptBodyExists(slot) || damping < 0.f) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (lock.Succeeded() && lock.GetBody().IsDynamic())
        lock.GetBody().GetMotionProperties()->SetLinearDamping(damping);
}

void PhysicsWorld::MakeScriptBodyNonColliding(int slot) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = impl_->scriptBodies[slot].body;
    bodies.SetObjectLayer(id, Layers::kNoCollide);
    if (bodies.GetMotionType(id) != JPH::EMotionType::Kinematic)
        bodies.SetMotionType(id, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
}

void PhysicsWorld::SetScriptBodyPinned(int slot, bool pinned) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = impl_->scriptBodies[slot].body;
    // A monster's body is kinematic and carried by its own mover; pinning must
    // not take that away from it.
    if (bodies.GetMotionType(id) == JPH::EMotionType::Kinematic) return;
    const JPH::EMotionType want =
        pinned ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    if (bodies.GetMotionType(id) == want) return;
    bodies.SetMotionType(id, want, JPH::EActivation::Activate);
    Impl::ScriptBody& sb = impl_->scriptBodies[size_t(slot)];
    if (sb.activeMesh) {
        sb.activePinned = pinned;
        if (pinned) impl_->pinnedActiveBodies.insert(id.GetIndexAndSequenceNumber());
        else impl_->pinnedActiveBodies.erase(id.GetIndexAndSequenceNumber());
        // A body released from static has just been given its mass
        // properties; apply the level factor that was waiting for them.
        if (!pinned && sb.mass > 0.f) {
            JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), id);
            if (lock.Succeeded() && lock.GetBody().IsDynamic())
                lock.GetBody().GetMotionProperties()->ScaleToMass(
                    sb.mass / lock.GetBody().GetMotionProperties()->GetInverseMass());
            sb.mass = 0.f;
        }
    }
    // Released with whatever velocity it had when it was frozen would launch
    // it; a pinned body has been standing still by definition.
    if (!pinned) {
        bodies.SetLinearVelocity(id, JPH::Vec3::sZero());
        bodies.SetAngularVelocity(id, JPH::Vec3::sZero());
    }
}

int PhysicsWorld::CreateActiveMeshBody(const MapObject& object, float worldScale,
                                       float massScale, bool pinned, bool concave, int group,
                                       float outOrigin[3]) {
    if (impl_->worldBody.IsInvalid() || object.vertexCount() == 0) return -1;
    // World-space points, and the bounds centre the body is built about.
    MeshPoints mesh;
    for (size_t v = 0; v < object.vertexCount(); ++v) {
        float p[3], w[3];
        object.position(v, p);
        object.transform.TransformPoint(p[0], p[1], p[2], w);
        for (int c = 0; c < 3; ++c) w[c] *= worldScale;
        mesh.Add(w);
    }
    float origin[3];
    for (int c = 0; c < 3; ++c) origin[c] = (mesh.lo[c] + mesh.hi[c]) * 0.5f;
    for (JPH::Vec3& p : mesh.points) p -= JPH::Vec3(origin[0], origin[1], origin[2]);
    Thin(mesh);
    // A convex hull in both cases. Jolt simulates no concave dynamic body;
    // "concave" (type 8, a MOPP in Havok) is the hull too, flagged as a
    // deviation in Docs/Reference/Physics.md.
    (void)concave;
    // No hull inflation. These objects are authored touching - coffins
    // stacked, column drums on each other - and Jolt's default 0.05 convex
    // radius made every pair overlap by 0.1, which the solver resolved by
    // popping the stack apart at load: 475 of the Cemetery's 548 had moved
    // by frame 60, a column drum by 7 units.
    JPH::ConvexHullShapeSettings hull(mesh.points, 0.005f);
    hull.SetEmbedded();
    JPH::ShapeSettings::ShapeResult shape = hull.Create();
    if (shape.HasError()) {
        JPH::SphereShapeSettings sphere(std::max(0.05f, mesh.radius()));
        sphere.SetEmbedded();
        shape = sphere.Create();
        if (shape.HasError()) return -1;
    }
    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(origin[0], origin[1], origin[2]),
                                   JPH::Quat::sIdentity(),
                                   pinned ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
                                   Layers::kMoving);
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    // A pinned body starts static and is released to dynamic later; Jolt
    // only keeps motion properties on a static body when told so here.
    body.mAllowDynamicOrKinematic = true;
    body.mEnhancedInternalEdgeRemoval = true;   // see MakeScriptBodyCharacter
    // AddMesh: SetFriction(DefaultMeshFriction), SetRestitution(DefaultMeshRestitution).
    body.mFriction = settings_.meshFriction;
    body.mRestitution = settings_.meshRestitution;
    body.mLinearDamping = 0.f;
    body.mAngularDamping = 0.05f;
    // Static bodies get no mass properties; a pinned one is given them on
    // release (SetScriptBodyPinned goes Dynamic and Jolt derives them then).
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    // ASLEEP. AddMesh hands every one to the engine's hard deactivator (a
    // body that moves under 0.3 in 5 s is frozen where it is), and in play
    // they do not stir until something touches them. A sleeping Jolt body is
    // the same thing: it stays put, supported or not, until an awake body,
    // a blast or a release wakes it.
    const JPH::BodyID id = bodies.CreateAndAddBody(body, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) return -1;
    Impl::ScriptBody sb;
    sb.body = id;
    sb.radius = mesh.radius();
    sb.activeMesh = true;
    sb.activeGroup = group;
    sb.activePinned = pinned;
    sb.activeRadius = mesh.radius();
    sb.activeLevelScaled = massScale != 1.f;
    impl_->scriptBodies.push_back(sb);
    const int slot = int(impl_->scriptBodies.size() - 1);
    if (pinned) impl_->pinnedActiveBodies.insert(id.GetIndexAndSequenceNumber());
    // ScaleMass(levelFactor): Havok's shape-derived mass times the level's
    // factor. Jolt derives the same kind of mass from the hull's volume.
    if (!pinned && massScale != 1.f) {
        JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), id);
        if (lock.Succeeded()) lock.GetBody().GetMotionProperties()->ScaleToMass(
            massScale / lock.GetBody().GetMotionProperties()->GetInverseMass());
    }
    for (int c = 0; c < 3; ++c) outOrigin[c] = origin[c];
    return slot;
}

void PhysicsWorld::ScaleUnscaledActiveMeshes(float massScale) {
    if (massScale == 1.f) return;
    for (Impl::ScriptBody& sb : impl_->scriptBodies) {
        if (!sb.activeMesh || sb.activeLevelScaled || sb.body.IsInvalid()) continue;
        sb.activeLevelScaled = true;
        // A pinned body is static and has no mass yet; scale on release.
        sb.mass = massScale;   // for a pinned body: the factor to apply at release
        if (sb.activePinned) continue;
        JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), sb.body);
        if (lock.Succeeded() && lock.GetBody().IsDynamic())
            lock.GetBody().GetMotionProperties()->ScaleToMass(
                massScale / lock.GetBody().GetMotionProperties()->GetInverseMass());
        sb.mass = 0.f;
    }
}

bool PhysicsWorld::IsActiveMesh(int slot) const {
    return ScriptBodyExists(slot) && impl_->scriptBodies[size_t(slot)].activeMesh;
}

void PhysicsWorld::ActivateActiveMeshGroup(int group) {
    for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
        Impl::ScriptBody& sb = impl_->scriptBodies[i];
        if (!sb.activeMesh || sb.activeGroup != group) continue;
        sb.activeEnabled = true;
        if (sb.activePinned) SetScriptBodyPinned(int(i), false);
    }
}

void PhysicsWorld::EnableActiveMeshGroup(int group, bool enabled) {
    for (Impl::ScriptBody& sb : impl_->scriptBodies)
        if (sb.activeMesh && sb.activeGroup == group) sb.activeEnabled = enabled;
}

void PhysicsWorld::UnpinActiveMeshesNear(const float centre[3], float range,
                                         std::vector<int>& out) {
    out.clear();
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
        const Impl::ScriptBody& sb = impl_->scriptBodies[i];
        if (!sb.activeMesh || !sb.activePinned || !sb.activeEnabled || sb.body.IsInvalid())
            continue;
        const JPH::RVec3 p = bodies.GetPosition(sb.body);
        const float d = std::sqrt(float((p.GetX() - centre[0]) * (p.GetX() - centre[0]) +
                                        (p.GetY() - centre[1]) * (p.GetY() - centre[1]) +
                                        (p.GetZ() - centre[2]) * (p.GetZ() - centre[2])));
        if (d < range + sb.activeRadius) out.push_back(int(i));
    }
    for (int slot : out) SetScriptBodyPinned(slot, false);
}

bool PhysicsWorld::IsScriptBodyPinned(int slot) const {
    if (!ScriptBodyExists(slot)) return false;
    return impl_->system.GetBodyInterface().GetMotionType(impl_->scriptBodies[slot].body) ==
           JPH::EMotionType::Static;
}

void PhysicsWorld::SetScriptBodyGravityFactor(int slot, float factor) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(),
                            impl_->scriptBodies[slot].body);
    if (lock.Succeeded() && lock.GetBody().IsDynamic())
        lock.GetBody().GetMotionProperties()->SetGravityFactor(factor);
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
    // A disabled body is out of the world; Jolt will not move one.
    if (!impl_->scriptBodies[size_t(slot)].inWorld) return;
    impl_->system.GetBodyInterface().SetPositionAndRotation(
        impl_->scriptBodies[slot].body, JPH::RVec3(pos[0], pos[1], pos[2]),
        EngineQuatToJolt(rotWXYZ), JPH::EActivation::Activate);
    // A teleported character lands at its model origin, like a spawn.
    if (impl_->scriptBodies[size_t(slot)].character >= 0) StandCharacterOnFloor(slot, 100.f);
}


void PhysicsWorld::SetScriptBodyVelocity(int slot, const float v[3]) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = impl_->scriptBodies[slot].body;
    bodies.SetLinearVelocity(id, JPH::Vec3(v[0], v[1], v[2]));
    // A body given a velocity is meant to move, and a sleeping one would sit
    // there holding it.
    if (!bodies.IsActive(id)) bodies.ActivateBody(id);
}


void PhysicsWorld::AddScriptBodyImpulse(int slot, const float at[3],
                                        const float impulse[3]) {
    if (!ScriptBodyExists(slot)) return;
    const JPH::Vec3 j(impulse[0], impulse[1], impulse[2]);
    if (j.IsNearZero()) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = impl_->scriptBodies[slot].body;
    // Only a dynamic body can be shoved; the world and anything pinned in
    // place take the hit without moving, which is what they are for.
    if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) return;
    // Wake it FIRST. An impulse applied to a sleeping body is dropped, and
    // props settle to sleep the moment a level finishes loading - so every
    // shot at a barrel that had been standing still would do nothing.
    if (!bodies.IsActive(id)) bodies.ActivateBody(id);
    // At a point rather than at the centre, so a shot off to one side spins
    // the thing it hits instead of sliding it flat.
    bodies.AddImpulse(id, j, JPH::RVec3(at[0], at[1], at[2]));
    // PhysicsObject::Hit (0x1018C050) caps the speed a hit leaves a body with
    // at 30 (0x102b3b7c). A character has no spin to take it, so the whole
    // impulse lands as velocity and the cap is what keeps a shotgun blast
    // from launching a monster across the level.
    if (impl_->characterBodies.count(id.GetIndexAndSequenceNumber()) != 0) {
        const JPH::Vec3 v = bodies.GetLinearVelocity(id);
        const float speed = v.Length();
        if (speed > 30.f) bodies.SetLinearVelocity(id, v * (30.f / speed));
    }
}

bool PhysicsWorld::GetScriptBodyVelocity(int slot, float out[3]) const {
    if (!ScriptBodyExists(slot)) return false;
    const JPH::Vec3 v =
        impl_->system.GetBodyInterface().GetLinearVelocity(impl_->scriptBodies[slot].body);
    for (int c = 0; c < 3; ++c) out[c] = v[c];
    return true;
}

// ENTITY.PO_Enable(e, on) - IN or OUT OF THE WORLD, not awake or asleep.
//
// PhysicsObject::Enable (Engine.dll 0x1907d0) does not deactivate anything: on
// false it disables the Havok body and then SWAP-REMOVES the object from the
// engine's active registry - last element into this slot, count down, index
// set to -1 - and on true it re-adds and registers it again. IsEnabled reads a
// flag on the body meaning "in the world" (0x196770).
//
// Sleeping it instead leaves it fully solid, which is what CActor:EnableRagdoll
// disables it FOR. A dead monster's movement capsule stayed standing where it
// died: it blocked the player, it swallowed the pusher, and worst of all the
// corpse collided with its own capsule - the ragdoll spent its whole fall
// being shoved around by the shape it used to walk in.
void PhysicsWorld::SetScriptBodyEnabled(int slot, bool enabled) {
    if (!ScriptBodyExists(slot)) return;
    Impl::ScriptBody& sb = impl_->scriptBodies[size_t(slot)];
    if (sb.inWorld == enabled) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    if (enabled) bodies.AddBody(sb.body, JPH::EActivation::Activate);
    else         bodies.RemoveBody(sb.body);
    sb.inWorld = enabled;
}

void PhysicsWorld::MakeScriptBodyCharacter(int slot, float k, float rootOffsetY) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    Impl::ScriptBody& sb = impl_->scriptBodies[size_t(slot)];
    const JPH::BodyID id = sb.body;
    // Only PO_SetMonsterType reaches here, so this is the character set that
    // Depenetrate separates horizontally instead of ejecting.
    impl_->characterBodies.insert(id.GetIndexAndSequenceNumber());

    if (k > 0.f && k != sb.radius) {
        // THREE STACKED SPHERES - which is what BodyTypes.Fatter is.
        //
        // The sizer's Fatter branch (0x101B3E20 case 2) builds twelve floats
        // that group as three (0, y, 0, r) records and hands them to a
        // three-element constructor. Wide low, wider at the middle, narrow at
        // the head: a "fatter" body, and the reason walking into one feels
        // like a capsule without being one.
        //
        //     y = -2.2k  r = 2.6k
        //     y = +1.0k  r = 3.0k
        //     y = +4.0k  r = 1.5k
        //
        // with k = scale * 0.2. The bottom of the first sphere is -4.8k, which
        // is exactly the other constant the branch computes - the corroboration
        // that these are spheres rather than something else read the same way.
        struct Ball { float y, r; };
        static const Ball kBalls[3] = {{-2.2f, 2.6f}, {1.0f, 3.0f}, {4.0f, 1.5f}};

        JPH::StaticCompoundShapeSettings compound;
        compound.SetEmbedded();
        for (const Ball& ball : kBalls) {
            JPH::SphereShapeSettings* sphere = new JPH::SphereShapeSettings(ball.r * k);
            compound.AddShape(JPH::Vec3(0.f, ball.y * k + rootOffsetY, 0.f),
                              JPH::Quat::sIdentity(), sphere);
        }
        JPH::ShapeSettings::ShapeResult shape = compound.Create();
        if (shape.HasError()) {
            JPH::SphereShapeSettings sphere(k * 3.f);
            sphere.SetEmbedded();
            shape = sphere.Create();
        }
        if (!shape.HasError()) {
            bodies.SetShape(id, shape.Get(), true, JPH::EActivation::Activate);
            sb.radius = k;
        }
    }

    if (sb.character < 0) {
        Impl::Character c;
        c.slot = slot;
        impl_->characters.push_back(c);
        sb.character = int(impl_->characters.size() - 1);
    }
    Impl::Character& ch = impl_->characters[size_t(sb.character)];
    ch.k = sb.radius;
    ch.rootOffsetY = rootOffsetY;

    // DYNAMIC, translation only. CreatePhysicsObject (0x101999F0) ends with
    // SetFreedomOfRotation(1, 1.0): pitch and roll inertia FLT_MAX, yaw 10 -
    // and the scripts set the yaw themselves through SetOrientation, so no
    // rotation is left to the solver here.
    if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic)
        bodies.SetMotionType(id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
    {
        JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), id);
        if (lock.Succeeded()) {
            JPH::Body& body = lock.GetBody();
            JPH::MassProperties props = body.GetShape()->GetMassProperties();
            // The sizer's mass rule for a sphere stack is (0.2 * scale)^3 *
            // 10000 - recovered for the player's four spheres, ASSUMED to hold
            // for the Fatter stack. PO_SetMass overrides it where a template
            // declares s_Physics.Mass.
            const float mass = sb.mass > 0.f ? sb.mass : ch.k * ch.k * ch.k * 10000.f;
            if (props.mMass > 0.f) props.ScaleToMass(std::max(mass, 1.f));
            body.GetMotionProperties()->SetMassProperties(
                JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                    JPH::EAllowedDOFs::TranslationZ,
                props);
            // GUESS: Havok's default material. CActor:PO_Create says friction
            // is deliberately left alone because a higher one stops them
            // climbing stairs, and PO_SetFriction never reaches Havok.
            // 0.1: CreatePhysicsObject writes 0.1 friction and 0.001
            // restitution into every non-player body's wrapper (the field
            // SetFriction writes), and CActor:PO_Create leaves it there
            // because a higher one stops them climbing stairs.
            // PAINFUL_CHAR_FRICTION overrides it for experiments.
            static const float charFriction = std::getenv("PAINFUL_CHAR_FRICTION")
                                                  ? float(std::atof(std::getenv("PAINFUL_CHAR_FRICTION")))
                                                  : 0.1f;
            body.SetFriction(charFriction);
            body.SetRestitution(0.f);
            body.GetMotionProperties()->SetLinearDamping(0.f);
            body.GetMotionProperties()->SetAngularDamping(0.f);
            // Re-commanded every step, so it never has a reason to sleep - and
            // a sleeping body would hold a velocity without moving.
            body.SetAllowSleeping(false);
            // A sphere stack sliding across a triangle mesh catches the seams
            // between triangles without this: ghost contacts with normals
            // tilted against the motion, which took 40% of a zombie's
            // commanded speed on a flat Cemetery path whatever the friction.
            body.SetEnhancedInternalEdgeRemoval(true);
        }
    }
    bodies.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    for (int c = 0; c < 3; ++c) ch.wish[c] = ch.lastWish[c] = 0.f;
    // An actor is authored and spawned at its model ORIGIN, mid-body on most
    // rigs, so the stack starts a sole's height inside the floor.
    StandCharacterOnFloor(slot, 100.f);
}

void PhysicsWorld::StandCharacterOnFloor(int slot, float maxLift, float minLift) {
    Impl::Character* ch = impl_->CharacterOf(slot);
    if (!ch || !ScriptBodyExists(slot) || !impl_->scriptBodies[size_t(slot)].inWorld) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::BodyID id = impl_->scriptBodies[size_t(slot)].body;
    const JPH::RVec3 p = bodies.GetPosition(id);
    const float bottom = float(p.GetY()) + ch->rootOffsetY - 4.8f * ch->k;
    const float top = float(p.GetY()) + ch->rootOffsetY + 5.5f * ch->k;
    // From a unit above the head down to the stack's lowest point: a floor in
    // that span has the body inside it. A downward-facing hit is a ceiling
    // seen from below, not a floor.
    const JPH::RVec3 from(p.GetX(), top + 1.f, p.GetZ());
    const JPH::Vec3 span(0.f, bottom - (top + 1.f), 0.f);
    JPH::IgnoreMultipleBodiesFilter ignore;
    ignore.Reserve(3);
    ignore.IgnoreBody(id);
    if (!impl_->probe.IsInvalid()) ignore.IgnoreBody(impl_->probe);
    if (!impl_->pawnProbe.IsInvalid()) ignore.IgnoreBody(impl_->pawnProbe);
    JPH::RRayCast ray(from, span);
    JPH::RayCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mTreatConvexAsSolid = false;
    // The LOWEST upward-facing hit in the span is the floor the body belongs
    // on; the first hit from above may be a ceiling's underside or the top of
    // a prop the head pokes through.
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, kSweepLayer,
                                                ignore);
    // Only below the middle sphere's centre: a surface higher than that inside
    // the stack cannot be entered by walking - the body would have been
    // stopped by it - so a hit there is a ledge beside the body, not a floor
    // it is inside.
    const float middle = float(p.GetY()) + ch->rootOffsetY + 1.0f * ch->k;
    float floorY = -1e30f;
    bool found = false;
    for (const JPH::RayCastResult& hit : collector.mHits) {
        const JPH::RVec3 at = ray.GetPointOnRay(hit.mFraction);
        if (float(at.GetY()) > middle) continue;
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
        if (!lock.Succeeded()) continue;
        const JPH::Vec3 n =
            lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, at);
        if (n.GetY() <= 0.5f) continue;
        if (!found || float(at.GetY()) < floorY) floorY = float(at.GetY());
        found = true;
    }
    if (!found) return;
    const float lift = std::min(floorY + 0.02f - bottom, maxLift);
    if (lift <= minLift) return;
    bodies.SetPosition(id, JPH::RVec3(p.GetX(), p.GetY() + lift, p.GetZ()),
                       JPH::EActivation::Activate);
    const JPH::Vec3 v = bodies.GetLinearVelocity(id);
    if (v.GetY() < 0.f) bodies.SetLinearVelocity(id, JPH::Vec3(v.GetX(), 0.f, v.GetZ()));
}

bool PhysicsWorld::IsScriptBodyCharacter(int slot) const {
    return ScriptBodyExists(slot) && impl_->scriptBodies[size_t(slot)].character >= 0;
}

void PhysicsWorld::SetCharacterWish(int slot, const float v[3]) {
    if (Impl::Character* ch = impl_->CharacterOf(slot))
        for (int c = 0; c < 3; ++c) ch->wish[c] = v[c];
}

void PhysicsWorld::SetCharacterMovement(int slot, float influence, bool dontCheckFloors) {
    if (Impl::Character* ch = impl_->CharacterOf(slot)) {
        ch->influence = influence;
        ch->checkFloors = !dontCheckFloors;
    }
}

void PhysicsWorld::SetCharacterFlying(int slot, bool flying) {
    if (Impl::Character* ch = impl_->CharacterOf(slot)) ch->flying = flying;
}

bool PhysicsWorld::IsCharacterFlying(int slot) const {
    const Impl::Character* ch = impl_->CharacterOf(slot);
    return ch != nullptr && ch->flying;
}

bool PhysicsWorld::CharacterOnFloor(int slot, float normal[3]) const {
    const Impl::Character* ch = impl_->CharacterOf(slot);
    if (!ch) return false;
    if (normal) for (int c = 0; c < 3; ++c) normal[c] = ch->floorNormal[c];
    return ch->onFloor;
}

// GetPawnFloorPos (0x10189390) is body.y - 1.1 * bodyScale and GetPawnHeadPos
// (0x10189340) body.y + 0.9 * bodyScale; in the sizer's unit k = 0.2 *
// bodyScale that is -5.5k and +4.5k off the stack's origin.
bool PhysicsWorld::CharacterFloorPos(int slot, float out[3]) const {
    const Impl::Character* ch = impl_->CharacterOf(slot);
    if (!ch || !ScriptBodyExists(slot)) return false;
    const JPH::RVec3 p =
        impl_->system.GetBodyInterface().GetPosition(impl_->scriptBodies[size_t(slot)].body);
    out[0] = float(p.GetX());
    out[1] = float(p.GetY()) + ch->rootOffsetY - 5.5f * ch->k;
    out[2] = float(p.GetZ());
    return true;
}

bool PhysicsWorld::CharacterHeadPos(int slot, float out[3]) const {
    const Impl::Character* ch = impl_->CharacterOf(slot);
    if (!ch || !ScriptBodyExists(slot)) return false;
    const JPH::RVec3 p =
        impl_->system.GetBodyInterface().GetPosition(impl_->scriptBodies[size_t(slot)].body);
    out[0] = float(p.GetX());
    out[1] = float(p.GetY()) + ch->rootOffsetY + 4.5f * ch->k;
    out[2] = float(p.GetZ());
    return true;
}

void PhysicsWorld::SetScriptBodyRotation(int slot, const float rotWXYZ[4]) {
    if (!ScriptBodyExists(slot) || !impl_->scriptBodies[size_t(slot)].inWorld) return;
    impl_->system.GetBodyInterface().SetRotation(impl_->scriptBodies[size_t(slot)].body,
                                                 EngineQuatToJolt(rotWXYZ),
                                                 JPH::EActivation::DontActivate);
}

// PhysicsObject::Tick (0x10190570), the monster branch, per physics tick.
// MonsterFloorCheck (0x1018FAA0) is a ray from head height, 4.5k above the
// stack origin, to 1.5 below the floor point 5.5k under it - so "on floor"
// means ground within 1.5 of the soles, and a falling actor (vy < -0.01)
// with none has its wish cleared. Then, unless flying:
//     ext = vel - lastWish;  ext.x,z *= c;  if (ext.y > 0 || no gravity) ext.y *= c
//     lastWish = wish;  vel = wish + ext
// with c = PO_SetMonsterMovementConst's first argument (0.5). Downward
// velocity is kept whole so gravity accumulates; everything else the solver
// added - a shove, a blast, a bounce - decays by c each tick.
void PhysicsWorld::StepCharacters() {
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    // Last step's contacts. The commanded velocity is clipped against them
    // below - what a Havok contact does to a body driven into a wall, minus
    // the penetration it builds up while the command keeps coming. Driven
    // into a pew at 8 units/s every step, a body sank into it and was thrown
    // back out at 4 the moment the command stopped; the AI read that as a
    // shove and re-planned. Measured: Docs/Reference/MonsterMovement.md.
    std::vector<ScriptContactListener::CharContact>& touching = impl_->lastTouching;
    impl_->contacts.TakeCharacterContacts(touching);
    for (Impl::Character& ch : impl_->characters) {
        if (ch.slot < 0 || size_t(ch.slot) >= impl_->scriptBodies.size()) continue;
        const Impl::ScriptBody& sb = impl_->scriptBodies[size_t(ch.slot)];
        if (sb.body.IsInvalid() || !sb.inWorld) continue;
        if (bodies.GetMotionType(sb.body) != JPH::EMotionType::Dynamic) continue;
        // The two-sided-mesh stand-in, a step's worth at a time - and only for
        // a real embedding. Lifting the resting slop too kept the bodies
        // airborne 49% of their steps and cost speed on every landing.
        StandCharacterOnFloor(ch.slot, 0.1f, 0.05f);

        const JPH::RVec3 p = bodies.GetPosition(sb.body);
        const float cx = float(p.GetX());
        const float cy = float(p.GetY()) + ch.rootOffsetY;
        const float cz = float(p.GetZ());
        const JPH::Vec3 vel = bodies.GetLinearVelocity(sb.body);

        ch.onFloor = false;
        if (!ch.checkFloors) {
            ch.onFloor = true;
        } else {
            const JPH::RVec3 from(cx, cy + 4.5f * ch.k, cz);
            const JPH::Vec3 span(0.f, -(4.5f * ch.k + 5.5f * ch.k + 1.5f), 0.f);
            JPH::IgnoreMultipleBodiesFilter ignore;
            ignore.Reserve(3);
            ignore.IgnoreBody(sb.body);
            if (!impl_->probe.IsInvalid()) ignore.IgnoreBody(impl_->probe);
            if (!impl_->pawnProbe.IsInvalid()) ignore.IgnoreBody(impl_->pawnProbe);
            JPH::RRayCast ray(from, span);
            JPH::RayCastSettings settings;
            settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
            settings.mTreatConvexAsSolid = false;
            JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
            impl_->system.GetNarrowPhaseQuery().CastRay(
                ray, settings, collector, {},
                kSweepLayer, ignore);
            if (collector.HadHit()) {
                ch.onFloor = true;
                JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(),
                                       collector.mHit.mBodyID);
                if (lock.Succeeded()) {
                    const JPH::Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(
                        collector.mHit.mSubShapeID2, ray.GetPointOnRay(collector.mHit.mFraction));
                    if (n.LengthSq() > 1e-8f && n == n) {
                        ch.floorNormal[0] = n.GetX();
                        ch.floorNormal[1] = n.GetY();
                        ch.floorNormal[2] = n.GetZ();
                    }
                }
            }
        }
        if (!ch.onFloor && vel.GetY() < -0.01f)
            for (int c = 0; c < 3; ++c) ch.wish[c] = 0.f;

        if (ch.flying) continue;
        const bool gravityOn = bodies.GetGravityFactor(sb.body) > 0.f;
        JPH::Vec3 ext = vel - JPH::Vec3(ch.lastWish[0], ch.lastWish[1], ch.lastWish[2]);
        ext.SetX(ext.GetX() * ch.influence);
        ext.SetZ(ext.GetZ() * ch.influence);
        if (ext.GetY() > 0.f || !gravityOn) ext.SetY(ext.GetY() * ch.influence);
        for (int c = 0; c < 3; ++c) ch.lastWish[c] = ch.wish[c];
        JPH::Vec3 next = JPH::Vec3(ch.wish[0], ch.wish[1], ch.wish[2]) + ext;
        // Slide along whatever it is touching: no component into a contact.
        // Two passes so a corner resolves.
        const uint32_t me = sb.body.GetIndexAndSequenceNumber();
        for (int pass = 0; pass < 2; ++pass)
            for (const ScriptContactListener::CharContact& c : touching) {
                if (c.body != me) continue;
                const JPH::Vec3 n(c.blocked[0], c.blocked[1], c.blocked[2]);
                const float into = next.Dot(n);
                if (into > 0.f) next -= n * into;
            }
        bodies.SetLinearVelocity(sb.body, next);
    }
}

void PhysicsWorld::ShoveCharacters(const float pos[3], float radius, const float dir[3],
                                   float speed, float pusherMass) {
    if (!loaded() || impl_->characters.empty() || speed <= 0.f) return;
    const JPH::Vec3 d(dir[0], 0.f, dir[2]);
    if (d.LengthSq() < 1e-8f) return;
    const JPH::Vec3 along = d.Normalized();
    const JPH::SphereShape sphere(radius + 0.08f);
    sphere.SetEmbedded();
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCollectFacesMode = JPH::ECollectFacesMode::NoFaces;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sOne(),
        JPH::RMat44::sTranslation(JPH::RVec3(pos[0], pos[1], pos[2])), settings,
        JPH::RVec3::sZero(), collector, {},
        kSweepLayer, {});
    if (collector.mHits.empty()) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        if (impl_->characterBodies.count(hit.mBodyID2.GetIndexAndSequenceNumber()) == 0)
            continue;
        if (bodies.GetMotionType(hit.mBodyID2) != JPH::EMotionType::Dynamic) continue;
        float mass = pusherMass;
        {
            JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID2);
            if (lock.Succeeded() && lock.GetBody().GetMotionProperties() &&
                lock.GetBody().GetMotionProperties()->GetInverseMass() > 0.f)
                mass = 1.f / lock.GetBody().GetMotionProperties()->GetInverseMass();
        }
        // GUESS: half the inelastic-collision share. The full share read as
        // too strong against the original in play - a monster is a thing you
        // can push, but slowly, and it slows you. What would settle it is the
        // player body's material and friction in the shape sizer.
        const float share = 0.5f * speed * pusherMass / (pusherMass + mass);
        const JPH::Vec3 v = bodies.GetLinearVelocity(hit.mBodyID2);
        const float have = v.Dot(along);
        if (have < share) bodies.AddLinearVelocity(hit.mBodyID2, along * (share - have));
    }
}

float PhysicsWorld::ScriptBodyRadius(int slot) const {
    return ScriptBodyExists(slot) ? impl_->scriptBodies[slot].radius : 0.f;
}

// Where Jolt actually put the body, in world space.
//
// The only way to settle a placement argument: what we asked for and what the
// solver holds are different questions, and a shape that looks wrong on screen
// could be either. This answers the second one directly.
bool PhysicsWorld::ScriptBodyBounds(int slot, float lo[3], float hi[3]) const {
    if (!ScriptBodyExists(slot)) return false;
    const JPH::AABox box =
        impl_->system.GetBodyInterface().GetTransformedShape(impl_->scriptBodies[slot].body)
            .GetWorldSpaceBounds();
    for (int c = 0; c < 3; ++c) {
        lo[c] = box.mMin[c];
        hi[c] = box.mMax[c];
    }
    return true;
}

bool PhysicsWorld::GetScriptBodyPosition(int slot, float out[3]) const {
    if (!ScriptBodyExists(slot)) return false;
    const JPH::RVec3 p =
        impl_->system.GetBodyInterface().GetPosition(impl_->scriptBodies[slot].body);
    for (int c = 0; c < 3; ++c) out[c] = float(p[c]);
    return true;
}

void PhysicsWorld::RemoveScriptBody(int slot) {
    if (!ScriptBodyExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    Impl::ScriptBody& sb = impl_->scriptBodies[size_t(slot)];
    // A disabled body is already out of the world, and Jolt asserts on the
    // second remove.
    if (sb.inWorld) bodies.RemoveBody(sb.body);
    impl_->characterBodies.erase(sb.body.GetIndexAndSequenceNumber());
    if (Impl::Character* ch = impl_->CharacterOf(slot)) ch->slot = -1;
    sb.character = -1;
    bodies.DestroyBody(sb.body);
    sb.body = JPH::BodyID();
    sb.inWorld = false;
}

void PhysicsWorld::CollectScriptPoses(std::vector<ScriptBodyPose>& out,
                                      bool activeOnly) const {
    out.clear();

    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    for (size_t slot = 0; slot < impl_->scriptBodies.size(); ++slot) {
        const JPH::BodyID id = impl_->scriptBodies[slot].body;
        if (id.IsInvalid() || !impl_->scriptBodies[slot].inWorld) continue;
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

// ---------------------------------------------------------------- ragdolls
//
// Built from the .hke, which is the engine's own ragdoll: a convex hull, a
// mass and a material per limb, and hkRagdollConstraint / hkHingeConstraint
// between them. Havok's two constraint types are Jolt's SwingTwist and Hinge
// almost one for one.
//
// EVERY CONSTRAINT IS BUILT IN WORLD SPACE, from the rest pose the file was
// authored in. The file states its frames either way - body-local matrices for
// most models, a world pivot and axes for others like raven - and Jolt's
// LocalToBodyCOM is relative to the CENTRE OF MASS rather than the body origin
// the file measures from, so converting local frames to Jolt's local space
// would need the COM the shape has not been built with yet. Placing the bodies
// at their authored transforms first and stating everything in world space
// sidesteps both problems, and Jolt converts once at creation.

namespace {

// Angle-axis to a quaternion. The file's ROTATION is `angle x y z`, and a
// primitive's identity is `0 0 0 0` - no rotation about no axis - which is why
// a zero axis has to come back as identity rather than as a NaN.
JPH::Quat AngleAxis(float angle, const float axis[3]) {
    const JPH::Vec3 v(axis[0], axis[1], axis[2]);
    const float len = v.Length();
    if (len < 1e-6f || std::fabs(angle) < 1e-9f) return JPH::Quat::sIdentity();
    return JPH::Quat::sRotation(v / len, angle);
}

// The authored world transform of one .hke body, in WORLD units.
JPH::Mat44 BodyRest(const HkeBody& b, float scale) {
    return JPH::Mat44::sRotationTranslation(
        AngleAxis(b.rotAngle, b.rotAxis),
        JPH::Vec3(b.translation[0], b.translation[1], b.translation[2]) * scale);
}

JPH::Vec3 V3(const float v[3]) { return JPH::Vec3(v[0], v[1], v[2]); }

// One hull, with the primitive's own offset baked in so the shape is
// body-local and Jolt never has to nest a RotatedTranslatedShape for it.
JPH::ShapeSettings::ShapeResult BuildLimbHull(const Hke& def, const HkeBody& b, float scale) {
    const HkeGeometry* g = def.Find(b.geometry);
    JPH::Array<JPH::Vec3> points;
    if (g) {
        const JPH::Mat44 prim = JPH::Mat44::sRotationTranslation(
            AngleAxis(b.primRotAngle, b.primRotAxis),
            JPH::Vec3(b.primTranslation[0], b.primTranslation[1], b.primTranslation[2]));
        points.reserve(g->vertexCount());
        for (size_t v = 0; v < g->vertexCount(); ++v)
            points.push_back(prim * JPH::Vec3(g->verts[v * 3 + 0], g->verts[v * 3 + 1],
                                              g->verts[v * 3 + 2]) * scale);
    }
    if (points.size() < 4) return JPH::ShapeSettings::ShapeResult();   // an error result
    JPH::ConvexHullShapeSettings hull(points, JPH::cDefaultConvexRadius * 0.25f);
    return hull.Create();
}

// TAU is on every constraint in every .hke, and it is 0.1 on all of them.
//
// In Havok tau is the fraction of the remaining position error a constraint
// corrects per step - a RELAXATION factor, not a hard snap. Jolt's equivalent
// default is Baumgarte 0.2, so the original's joints are half as eager as
// Jolt's out of the box, and a hard limit in Jolt is harder still: it resolves
// as completely as the solver iterations allow.
//
// That matters at the moment of activation. The ragdoll's joints SKIP skeleton
// bones - evilmonkv2 constrains root to k_zebra while the rig runs root ->
// k_ogo -> k_zebra - so a pose with any bend in the skipped bone separates the
// two anchors. Measured: 0.000 at the authored rest pose, 0.113 when activated
// from a death animation. A hard constraint eats that in one step and the
// corpse snaps; a soft one absorbs it over several and it slumps.
//
// Only the HINGES can take it: Jolt gives HingeConstraint an mLimitsSpringSettings
// and SwingTwistConstraint nothing equivalent, so the cone-twist joints keep hard
// limits for now. Nine of evilmonkv2's fourteen constraints are hinges.
//
// Converting: a constraint correcting a fraction tau of its error each step of
// length h behaves like a spring of angular frequency tau/h, so at the fixed
// 1/60 step tau 0.1 is 6 rad/s, just under 1 Hz. Critically damped, because an
// overshooting joint is a twitching one.
JPH::SpringSettings LimitSpring(float tau) {
    JPH::SpringSettings spring;
    if (tau <= 0.f) return spring;                 // frequency 0 keeps hard limits
    spring.mMode = JPH::ESpringMode::FrequencyAndDamping;
    spring.mFrequency = (tau * 60.f) / (2.f * JPH::JPH_PI);
    spring.mDamping = 1.f;
    return spring;
}

// The .hke's limits onto Jolt's.
//
// JOLT'S SWING IS SYMMETRIC AND HAVOK'S IS NOT. hkRagdollConstraint carries a
// signed min and max for both the cone and the plane; SwingTwistConstraint has
// one half-angle for each. Taking the larger magnitude keeps the joint from
// binding where the original allowed movement, at the cost of allowing a
// little more the other way. Twist is asymmetric in both and carries over
// exactly.
JPH::Ref<JPH::TwoBodyConstraintSettings> BuildConstraint(const HkeConstraint& c,
                                                         const JPH::Mat44& restA,
                                                         const JPH::Mat44& restB,
                                                         float scale) {
    if (c.kind == HkeConstraint::kHinge) {
        JPH::HingeConstraintSettings* h = new JPH::HingeConstraintSettings();
        h->mSpace = JPH::EConstraintSpace::WorldSpace;
        if (c.worldSpace) {
            const JPH::Vec3 pos = V3(c.worldHingePos) * scale;
            JPH::Vec3 dir = V3(c.worldHingeDir);
            if (dir.LengthSq() < 1e-12f) dir = JPH::Vec3::sAxisY();
            dir = dir.Normalized();
            h->mPoint1 = h->mPoint2 = pos;
            h->mHingeAxis1 = h->mHingeAxis2 = dir;
            h->mNormalAxis1 = h->mNormalAxis2 = dir.GetNormalizedPerpendicular();
        } else {
            h->mPoint1 = restA * (V3(c.hingePosA) * scale);
            h->mPoint2 = restB * (V3(c.hingePosB) * scale);
            h->mHingeAxis1 = (restA.Multiply3x3(V3(c.hingeDirA))).NormalizedOr(JPH::Vec3::sAxisY());
            h->mHingeAxis2 = (restB.Multiply3x3(V3(c.hingeDirB))).NormalizedOr(JPH::Vec3::sAxisY());
            h->mNormalAxis1 =
                (restA.Multiply3x3(V3(c.hingePerpA))).NormalizedOr(h->mHingeAxis1.GetNormalizedPerpendicular());
            h->mNormalAxis2 =
                (restB.Multiply3x3(V3(c.hingePerpB))).NormalizedOr(h->mHingeAxis2.GetNormalizedPerpendicular());
        }
        if (c.limited) {
            // Jolt wants min in [-pi,0] and max in [0,pi]; every shipped value
            // is already inside that, but a clamp costs nothing and an
            // out-of-range limit is an assert in a debug build.
            h->mLimitsMin = std::max(-JPH::JPH_PI, std::min(0.f, c.limitMinAngle));
            h->mLimitsMax = std::min(JPH::JPH_PI, std::max(0.f, c.limitMaxAngle));
            h->mLimitsSpringSettings = LimitSpring(c.tau);
        }
        return h;
    }

    if (c.kind == HkeConstraint::kStiffSpring) {
        JPH::DistanceConstraintSettings* d = new JPH::DistanceConstraintSettings();
        d->mSpace = JPH::EConstraintSpace::WorldSpace;
        d->mPoint1 = restA * (V3(c.localPointA) * scale);
        d->mPoint2 = restB * (V3(c.localPointB) * scale);
        return d;
    }

    JPH::SwingTwistConstraintSettings* s = new JPH::SwingTwistConstraintSettings();
    s->mSpace = JPH::EConstraintSpace::WorldSpace;
    JPH::Vec3 pivot, twist, plane;
    if (c.worldSpace) {
        pivot = V3(c.worldPivot) * scale;
        twist = V3(c.twistAxis);
        plane = V3(c.planeAxis);
    } else {
        // CS_TO_REF_TM is the constraint frame in the REFERENCE body: COL0..2
        // the basis, COL3 the origin. Twist runs along the first column and
        // the plane axis along the second, which is hkRagdollConstraint's own
        // ordering and what the world form states explicitly.
        pivot = restA * (V3(c.csToRef[3]) * scale);
        twist = restA.Multiply3x3(V3(c.csToRef[0]));
        plane = restA.Multiply3x3(V3(c.csToRef[1]));
    }
    twist = twist.NormalizedOr(JPH::Vec3::sAxisX());
    plane = plane.NormalizedOr(twist.GetNormalizedPerpendicular());
    // Jolt asserts the two are perpendicular; re-orthogonalise rather than
    // trust an exported basis to be exact.
    plane = (plane - twist * twist.Dot(plane)).NormalizedOr(twist.GetNormalizedPerpendicular());

    s->mPosition1 = s->mPosition2 = pivot;
    s->mTwistAxis1 = s->mTwistAxis2 = twist;
    s->mPlaneAxis1 = s->mPlaneAxis2 = plane;
    s->mNormalHalfConeAngle = std::max(std::fabs(c.coneMin), std::fabs(c.coneMax));
    s->mPlaneHalfConeAngle = std::max(std::fabs(c.planeMin), std::fabs(c.planeMax));
    s->mTwistMinAngle = std::max(-JPH::JPH_PI, std::min(JPH::JPH_PI, c.twistMin));
    s->mTwistMaxAngle = std::max(-JPH::JPH_PI, std::min(JPH::JPH_PI, c.twistMax));
    if (s->mTwistMinAngle > s->mTwistMaxAngle) std::swap(s->mTwistMinAngle, s->mTwistMaxAngle);
    return s;
}

// Jolt's mToParent is always body1 = PARENT, body2 = child. The .hke names its
// pair in whatever order the exporter happened to write, and it is not always
// parent-first: evilmonkv2 has `Hinge r_l_bark -> r_l_lokiec` (parent first)
// next to `Hinge n_l_kolano -> n_l_biodro` (child first).
//
// Feeding frame 1 from RIGID_BODY_A regardless hands the PARENT the CHILD's
// anchor whenever the file is child-first, and the joint then has nothing
// holding it in the right place. Measured on the two knees, which are mirror
// images of each other and differ only in naming order: 1.9% bone-length drift
// on the parent-first one, 41.4% on the child-first one.
void SwapConstraintFrames(JPH::TwoBodyConstraintSettings* s, HkeConstraint::Kind kind) {
    if (kind == HkeConstraint::kHinge) {
        JPH::HingeConstraintSettings* h = static_cast<JPH::HingeConstraintSettings*>(s);
        std::swap(h->mPoint1, h->mPoint2);
        std::swap(h->mHingeAxis1, h->mHingeAxis2);
        std::swap(h->mNormalAxis1, h->mNormalAxis2);
        // Measured from the other body, the angle runs the other way.
        const float lo = h->mLimitsMin, hi = h->mLimitsMax;
        h->mLimitsMin = -hi;
        h->mLimitsMax = -lo;
    } else if (kind == HkeConstraint::kRagdoll) {
        JPH::SwingTwistConstraintSettings* t = static_cast<JPH::SwingTwistConstraintSettings*>(s);
        std::swap(t->mPosition1, t->mPosition2);
        std::swap(t->mTwistAxis1, t->mTwistAxis2);
        std::swap(t->mPlaneAxis1, t->mPlaneAxis2);
        const float lo = t->mTwistMinAngle, hi = t->mTwistMaxAngle;
        t->mTwistMinAngle = -hi;
        t->mTwistMaxAngle = -lo;
    } else {
        JPH::DistanceConstraintSettings* d = static_cast<JPH::DistanceConstraintSettings*>(s);
        std::swap(d->mPoint1, d->mPoint2);
    }
}

} // namespace

int PhysicsWorld::CreateRagdoll(const std::string& model, const Hke& def, float scale) {
    if (def.bodies.empty()) return -1;

    JPH::Ref<JPH::RagdollSettings>& cached = impl_->ragdollSettings[model];
    std::vector<std::string>& order = impl_->ragdollBones[model];
    if (cached == nullptr) {
        // The parts have to be a TREE in parent-before-child order, and the
        // .hke is a graph: mostly a tree, but with the weapons hanging off
        // nothing at all. Walk it from the root, keep the constraints used as
        // tree edges, and hand Jolt the rest as additional constraints.
        std::vector<int> parent(def.bodies.size(), -1);
        std::vector<int> edge(def.bodies.size(), -1);       // constraint used
        std::vector<int> visitOrder;
        std::vector<bool> seen(def.bodies.size(), false);

        int root = 0;
        for (size_t i = 0; i < def.bodies.size(); ++i)
            if (def.bodies[i].bone == "root" || def.bodies[i].bone == "ROOOT") {
                root = int(i);
                break;
            }

        std::vector<int> open{root};
        seen[size_t(root)] = true;
        while (!open.empty()) {
            const int at = open.front();
            open.erase(open.begin());
            visitOrder.push_back(at);
            for (size_t ci = 0; ci < def.constraints.size(); ++ci) {
                const HkeConstraint& c = def.constraints[ci];
                const std::string& me = def.bodies[size_t(at)].bone;
                std::string otherName;
                if      (c.bodyA == me) otherName = c.bodyB;
                else if (c.bodyB == me) otherName = c.bodyA;
                else continue;
                for (size_t bi = 0; bi < def.bodies.size(); ++bi) {
                    if (def.bodies[bi].bone != otherName || seen[bi]) continue;
                    seen[bi] = true;
                    parent[bi] = at;
                    edge[bi] = int(ci);
                    open.push_back(int(bi));
                    break;
                }
            }
        }
        // The weapons: bodies the walk never reached, appended as free parts.
        for (size_t i = 0; i < def.bodies.size(); ++i)
            if (!seen[i]) visitOrder.push_back(int(i));

        std::vector<int> partOf(def.bodies.size(), -1);
        for (size_t p = 0; p < visitOrder.size(); ++p) partOf[size_t(visitOrder[p])] = int(p);

        JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
        settings->mSkeleton = new JPH::Skeleton();
        settings->mParts.resize(visitOrder.size());
        order.clear();

        for (size_t p = 0; p < visitOrder.size(); ++p) {
            const HkeBody& b = def.bodies[size_t(visitOrder[p])];
            const int par = parent[size_t(visitOrder[p])];
            settings->mSkeleton->AddJoint(b.bone, par >= 0 ? partOf[size_t(par)] : -1);
            order.push_back(b.bone);

            JPH::ShapeSettings::ShapeResult hull = BuildLimbHull(def, b, scale);
            JPH::RagdollSettings::Part& part = settings->mParts[p];
            if (hull.IsValid()) part.SetShape(hull.Get());
            else                part.SetShape(new JPH::SphereShape(0.1f));
            const JPH::Mat44 rest = BodyRest(b, scale);
            part.mPosition = rest.GetTranslation();
            part.mRotation = rest.GetQuaternion().Normalized();
            part.mMotionType = JPH::EMotionType::Dynamic;
            // SWEPT, NOT STEPPED. Limb hulls are small - a raven's whole
            // ragdoll spans about a unit - and a corpse dropped from any
            // height reaches a speed where a stepped body jumps clean through
            // the floor between two steps. Measured: the raven fell 209 units
            // out of Cathedral before this, 4.4 after.
            part.mMotionQuality = JPH::EMotionQuality::LinearCast;
            part.mObjectLayer = Layers::kMoving;
            part.mFriction = b.staticFriction;
            part.mRestitution = b.elasticity;
            part.mLinearDamping = def.linearDrag;
            part.mAngularDamping = def.angularDrag;
            // The .hke mass is the authority; the .rde says -1 everywhere,
            // which is what "take it from here" looks like.
            if (b.mass > 0.f) {
                part.mOverrideMassProperties =
                    JPH::EOverrideMassProperties::CalculateInertia;
                part.mMassPropertiesOverride.mMass = b.mass;
            }
            if (par >= 0 && edge[size_t(visitOrder[p])] >= 0) {
                const HkeConstraint& c = def.constraints[size_t(edge[size_t(visitOrder[p])])];
                // Jolt's mToParent is body1 = PARENT. The file names its pair
                // in either order, so build it in the file's terms and then
                // swap the frames when the file put the child first.
                const bool aIsParent = (c.bodyA == def.bodies[size_t(par)].bone);
                const JPH::Mat44 restPar = BodyRest(def.bodies[size_t(par)], scale);
                JPH::Ref<JPH::TwoBodyConstraintSettings> made =
                    BuildConstraint(c, aIsParent ? restPar : rest, aIsParent ? rest : restPar,
                                    scale);
                if (!aIsParent) SwapConstraintFrames(made, c.kind);
                part.mToParent = made;
            }
        }

        // Whatever the tree walk did not consume - a second constraint between
        // two limbs already joined, which a few rigs have.
        for (size_t ci = 0; ci < def.constraints.size(); ++ci) {
            bool used = false;
            for (size_t i = 0; i < def.bodies.size() && !used; ++i)
                if (edge[i] == int(ci)) used = true;
            if (used) continue;
            const HkeConstraint& c = def.constraints[ci];
            int ia = -1, ib = -1;
            for (size_t i = 0; i < def.bodies.size(); ++i) {
                if (def.bodies[i].bone == c.bodyA) ia = partOf[i];
                if (def.bodies[i].bone == c.bodyB) ib = partOf[i];
            }
            if (ia < 0 || ib < 0 || ia == ib) continue;
            const JPH::Mat44 ra = BodyRest(def.bodies[size_t(visitOrder[size_t(ia)])], scale);
            const JPH::Mat44 rb = BodyRest(def.bodies[size_t(visitOrder[size_t(ib)])], scale);
            settings->mAdditionalConstraints.push_back(
                JPH::RagdollSettings::AdditionalConstraint(ia, ib,
                                                           BuildConstraint(c, ra, rb, scale)));
        }

        settings->Stabilize();
        settings->DisableParentChildCollisions();
        settings->CalculateBodyIndexToConstraintIndex();
        cached = settings;
        LogInfo("ragdoll %s: %zu parts, %zu tree constraints, %zu additional",
                model.c_str(), settings->mParts.size(),
                settings->mParts.size() - 1, settings->mAdditionalConstraints.size());
    }

    // Each instance needs its own collision group so two corpses in a heap
    // still collide with each other while neither collides with itself.
    Impl::RagdollInst inst;
    inst.ragdoll = cached->CreateRagdoll(impl_->nextRagdollGroup++, 0, &impl_->system);
    if (inst.ragdoll == nullptr) return -1;
    inst.bones = order;
    inst.ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);

    for (size_t i = 0; i < impl_->ragdolls.size(); ++i)
        if (impl_->ragdolls[i].ragdoll == nullptr) {
            impl_->ragdolls[i] = inst;
            return int(i);
        }
    impl_->ragdolls.push_back(inst);
    return int(impl_->ragdolls.size() - 1);
}

bool PhysicsWorld::RagdollExists(int slot) const {
    return slot >= 0 && size_t(slot) < impl_->ragdolls.size() &&
           impl_->ragdolls[size_t(slot)].ragdoll != nullptr;
}

void PhysicsWorld::RemoveRagdoll(int slot) {
    if (!RagdollExists(slot)) return;
    Impl::RagdollInst& inst = impl_->ragdolls[size_t(slot)];
    inst.ragdoll->RemoveFromPhysicsSystem();
    inst.ragdoll = nullptr;
    inst.bones.clear();
}

const std::vector<std::string>& PhysicsWorld::RagdollBones(int slot) const {
    static const std::vector<std::string> kNone;
    return RagdollExists(slot) ? impl_->ragdolls[size_t(slot)].bones : kNone;
}

bool PhysicsWorld::RagdollActive(int slot) const {
    return RagdollExists(slot) && impl_->ragdolls[size_t(slot)].simulated;
}

void PhysicsWorld::SetRagdollDamping(int slot, float linear, float angular) {
    if (!RagdollExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs()) {
        JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) continue;
        JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (mp == nullptr) continue;
        if (linear >= 0.f) mp->SetLinearDamping(linear);
        if (angular >= 0.f) mp->SetAngularDamping(angular);
    }
    (void)bodies;
}

void PhysicsWorld::SetRagdollFriction(int slot, float friction) {
    if (!RagdollExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs())
        bodies.SetFriction(id, friction);
}

// The mass the scripts set is the WHOLE ragdoll's, and the .hke distributes it
// across the limbs - a torso is ten times a forearm. Scaling every limb by the
// same factor keeps that distribution while hitting the total.
void PhysicsWorld::SetRagdollMass(int slot, float mass) {
    if (!RagdollExists(slot) || mass <= 0.f) return;
    const JPH::Ragdoll* rd = impl_->ragdolls[size_t(slot)].ragdoll;
    float total = 0.f;
    for (JPH::BodyID id : rd->GetBodyIDs()) {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) continue;
        const JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (mp != nullptr && mp->GetInverseMass() > 0.f) total += 1.f / mp->GetInverseMass();
    }
    if (total <= 0.f) return;
    const float k = mass / total;
    for (JPH::BodyID id : rd->GetBodyIDs()) {
        JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) continue;
        JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (mp == nullptr || mp->GetInverseMass() <= 0.f) continue;
        JPH::MassProperties props = lock.GetBody().GetShape()->GetMassProperties();
        props.ScaleToMass((1.f / mp->GetInverseMass()) * k);
        mp->SetMassProperties(JPH::EAllowedDOFs::All, props);
    }
}

// A whole-corpse tumble about Y, the way PhysicsObject::EffectRotateActor
// (Engine.dll 0x1893e0) spends the accumulated spin: the vector it hands the
// body is (0, spin, 0), an angular velocity.
//
// Setting the same angular velocity on every limb would make each one spin
// about its OWN centre, which is a bag of pinwheels rather than a body. A
// rigid rotation about a shared centre also needs the linear velocity that
// rotation implies at each limb's offset - v = w x r.
void PhysicsWorld::SetRagdollSpin(int slot, float yawRate) {
    if (!RagdollExists(slot)) return;
    const JPH::Ragdoll* rd = impl_->ragdolls[size_t(slot)].ragdoll;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();

    JPH::Vec3 centre = JPH::Vec3::sZero();
    float total = 0.f;
    for (JPH::BodyID id : rd->GetBodyIDs()) {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) continue;
        const JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        const float m = (mp != nullptr && mp->GetInverseMass() > 0.f) ? 1.f / mp->GetInverseMass()
                                                                     : 1.f;
        centre += JPH::Vec3(lock.GetBody().GetCenterOfMassPosition()) * m;
        total += m;
    }
    if (total <= 0.f) return;
    centre /= total;

    const JPH::Vec3 w(0.f, yawRate, 0.f);
    for (JPH::BodyID id : rd->GetBodyIDs()) {
        const JPH::Vec3 r = JPH::Vec3(bodies.GetCenterOfMassPosition(id)) - centre;
        bodies.SetAngularVelocity(id, w);
        bodies.SetLinearVelocity(id, bodies.GetLinearVelocity(id) + w.Cross(r));
    }
}

void PhysicsWorld::ScaleRagdollInertia(int slot, float k) {
    if (!RagdollExists(slot) || k <= 0.f) return;
    for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs()) {
        JPH::BodyLockWrite lock(impl_->system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) continue;
        JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
        if (mp == nullptr || mp->GetInverseMass() <= 0.f) continue;
        JPH::MassProperties props = lock.GetBody().GetShape()->GetMassProperties();
        props.ScaleToMass(1.f / mp->GetInverseMass());
        props.mInertia *= k;
        props.mInertia(3, 3) = 1.f;      // the row Jolt keeps as the affine tail
        mp->SetMassProperties(JPH::EAllowedDOFs::All, props);
    }
}

void PhysicsWorld::AddRagdollImpulse(int slot, const float at[3], const float impulse[3]) {
    if (!RagdollExists(slot)) return;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::RVec3 point(at[0], at[1], at[2]);
    // Whichever limb is nearest the point. The scripts aim at a world
    // position, not at a body, so something has to choose.
    JPH::BodyID best;
    float bestDist = 1e30f;
    for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs()) {
        const float d = (JPH::Vec3(bodies.GetPosition(id)) - JPH::Vec3(point)).LengthSq();
        if (d < bestDist) { bestDist = d; best = id; }
    }
    if (best.IsInvalid()) return;
    bodies.AddImpulse(best, JPH::Vec3(impulse[0], impulse[1], impulse[2]), point);
}

void PhysicsWorld::SetRagdollPose(int slot, const float* boneMatrices, bool kinematic) {
    if (!RagdollExists(slot) || boneMatrices == nullptr) return;
    Impl::RagdollInst& inst = impl_->ragdolls[size_t(slot)];
    const size_t n = inst.bones.size();

    // Our Mat4 is row-major with the basis in its ROWS (row-vector, v*M);
    // Jolt's Mat44 is column-major with the basis in its COLUMNS. Row i of one
    // is column i of the other, so this is a copy rather than a transpose.
    std::vector<JPH::Mat44> mats(n);
    for (size_t i = 0; i < n; ++i) {
        const float* m = boneMatrices + i * 16;
        mats[i] = JPH::Mat44(JPH::Vec4(m[0], m[1], m[2], 0.f),
                             JPH::Vec4(m[4], m[5], m[6], 0.f),
                             JPH::Vec4(m[8], m[9], m[10], 0.f),
                             JPH::Vec4(m[12], m[13], m[14], 1.f));
    }

    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    const JPH::EMotionType want =
        kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    // Alive, the limbs are hitboxes and nothing else; dead, they are a
    // corpse that lies on the floor and bumps into things.
    const JPH::ObjectLayer layer = kinematic ? Layers::kHitbox : Layers::kMoving;
    for (JPH::BodyID id : inst.ragdoll->GetBodyIDs()) {
        if (bodies.GetMotionType(id) != want)
            bodies.SetMotionType(id, want, JPH::EActivation::Activate);
        if (bodies.GetObjectLayer(id) != layer) bodies.SetObjectLayer(id, layer);
    }

    inst.ragdoll->SetPose(JPH::RVec3::sZero(), mats.data());
    inst.simulated = !kinematic;
    if (!kinematic) inst.ragdoll->Activate();
}

bool PhysicsWorld::GetRagdollPose(int slot, float* boneMatrices) const {
    if (!RagdollExists(slot) || boneMatrices == nullptr) return false;
    const Impl::RagdollInst& inst = impl_->ragdolls[size_t(slot)];
    const size_t n = inst.bones.size();
    std::vector<JPH::Mat44> mats(n);
    JPH::RVec3 rootOffset = JPH::RVec3::sZero();
    inst.ragdoll->GetPose(rootOffset, mats.data());
    for (size_t i = 0; i < n; ++i) {
        float* m = boneMatrices + i * 16;
        for (int c = 0; c < 3; ++c) {
            const JPH::Vec3 col = mats[i].GetColumn3(c);
            m[c * 4 + 0] = col.GetX();
            m[c * 4 + 1] = col.GetY();
            m[c * 4 + 2] = col.GetZ();
            m[c * 4 + 3] = 0.f;
        }
        const JPH::Vec3 t = JPH::Vec3(mats[i].GetTranslation()) + JPH::Vec3(rootOffset);
        m[12] = t.GetX(); m[13] = t.GetY(); m[14] = t.GetZ(); m[15] = 1.f;
    }
    return true;
}


void PhysicsWorld::CollectDebugLines(const float around[3], float radius,
                                     std::vector<DebugLine>& out,
                                     bool includeStatic) const {
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
    if (includeStatic) wireframe(impl_->worldBody, near, 0x60ff8040u);

    // Ragdoll limbs: magenta while driven by the animation, cyan once the
    // solver owns them. Which of the two a corpse is in is the first thing to
    // look at when a death goes wrong.
    for (const Impl::RagdollInst& inst : impl_->ragdolls) {
        if (inst.ragdoll == nullptr) continue;
        for (JPH::BodyID id : inst.ragdoll->GetBodyIDs())
            wireframe(id, JPH::AABox::sBiggest(), inst.simulated ? 0xffffff00u : 0xffff00ffu);
    }

    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    for (const Impl::Prop& prop : impl_->props) {
        // Awake bodies in green, sleeping ones in yellow: whether a prop is
        // simulating at all is the first thing to look at.
        const uint32_t abgr = bodies.IsActive(prop.body) ? 0xff00ff00u : 0xff00ffffu;
        wireframe(prop.body, JPH::AABox::sBiggest(), abgr);
    }

    // Script bodies in magenta, and in red when they are not colliding with
    // anything. These are what the scripts make - items, debris, projectiles,
    // a stake that has nailed itself to a wall - and they are where the bugs
    // live, so they get a colour of their own rather than being invisible.
    for (const Impl::ScriptBody& script : impl_->scriptBodies) {
        // A disabled body is out of the world - and the point of the view is to
        // show what is actually there.
        if (script.body.IsInvalid() || !script.inWorld) continue;
        const uint32_t abgr =
            bodies.GetObjectLayer(script.body) == Layers::kNoCollide ? 0xff0000ffu
                                                                     : 0xffff00ffu;
        wireframe(script.body, JPH::AABox::sBiggest(), abgr);
    }
}

// Everything a trace may hit: the world and ordinary bodies, but never a
// Noncolliding one.
//
// The scripts put projectiles in ECollisionGroups.Noncolliding exactly so that
// nothing traces against them. Jolt's default object-layer filter accepts every
// layer, so ours were hittable - and a stake's own forward trace hit the stake.
// Stake:Tick reads the collision group of whatever it hit, sees 7 and returns
// early; with the self-hit always nearest it did that on EVERY frame, so every
// real hit behind it was never reached and shots went through walls.
// What a trace can land on: everything that is solid to the world. The pusher
// bodies are not - a trace that stopped on the pawn's own sphere reported a
// WORLD hit standing exactly where the player is.
class SolidLayerFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override {
        return layer != Layers::kNoCollide && layer != Layers::kProbe;
    }
};

// Shared by every query. Jolt's default filter is `{}`, which accepts every
// layer - and the pawn finds its ground, its steps and its walls with SHAPE
// queries, not rays. Fixing only the ray left a stake that had nailed itself
// to a wall still solid enough to stand on, because nothing the player walks
// with was ever asking about layers.
const SolidLayerFilter kSolidLayer;

bool PhysicsWorld::RayCast(const float from[3], const float to[3], RayHit& out,
                           bool staticOnly, const int* exclude,
                           size_t excludeCount) const {
    out = RayHit{};
    if (!loaded()) return false;

    const JPH::RVec3 start(from[0], from[1], from[2]);
    const JPH::Vec3 span(to[0] - from[0], to[1] - from[1], to[2] - from[2]);
    const float length = span.Length();
    if (length < 1e-6f) return false;

    // Everything the caller has taken out of the intersection solver, plus
    // the camera's own probe body - which sits where the camera is and would
    // otherwise swallow every shot fired from there.
    JPH::IgnoreMultipleBodiesFilter bodies;
    bodies.Reserve(int(excludeCount) + 2);
    if (!impl_->probe.IsInvalid()) bodies.IgnoreBody(impl_->probe);
    // The pawn's pusher sits exactly where the player is, so a shot fired from
    // there would hit it at zero distance in every direction.
    if (!impl_->pawnProbe.IsInvalid()) bodies.IgnoreBody(impl_->pawnProbe);
    for (size_t i = 0; i < excludeCount; ++i) {
        const int slot = exclude[i];
        // A removed slot keeps an invalid id so the others stay stable, and
        // handing one of those to the filter is not survivable.
        if (slot >= 0 && size_t(slot) < impl_->scriptBodies.size() &&
            !impl_->scriptBodies[slot].body.IsInvalid())
            bodies.IgnoreBody(impl_->scriptBodies[slot].body);
    }

    JPH::RRayCast ray(start, span);
    JPH::RayCastSettings settings;
    // The world mesh is authored one-sided and wound the other way round, so
    // a trace that ignored back faces would pass straight through a wall it
    // happens to meet from behind - exactly the case a projectile spawned
    // inside geometry hits.
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    // A ray that STARTS inside a convex shape must pass through it rather
    // than report a hit at zero distance. A projectile is spawned inside the
    // muzzle, overlapping whatever it was fired from; treating that as an
    // immediate hit detonates it on frame one, and the contact it reports is
    // degenerate - there is no surface to take a normal from, so the normal
    // comes back as NaN and the scripts carry it into everything downstream.
    settings.mTreatConvexAsSolid = false;

    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    // LineTraceFixedGeom asks about the world mesh alone, which is the
    // non-moving layer; the actors and props it wants to ignore all live in
    // the moving one.
    const JPH::DefaultObjectLayerFilter staticLayer(impl_->objectPairs,
                                                    Layers::kNonMoving);
    const SolidLayerFilter solidLayer;
    impl_->system.GetNarrowPhaseQuery().CastRay(
        ray, settings, collector, {},
        staticOnly ? static_cast<const JPH::ObjectLayerFilter&>(staticLayer)
                   : solidLayer,
        bodies);
    if (!collector.HadHit()) return false;

    const JPH::RVec3 point = ray.GetPointOnRay(collector.mHit.mFraction);
    out.distance = collector.mHit.mFraction * length;
    for (int c = 0; c < 3; ++c) out.point[c] = float(point[c]);

    // The surface normal at the hit, which the scripts use to orient decals
    // and bounce effects.
    {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(),
                               collector.mHit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            const JPH::Vec3 normal =
                body.GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, point);
            for (int c = 0; c < 3; ++c) out.normal[c] = normal[c];
        }
    }

    // Never hand a degenerate normal back to the scripts. A grazing or
    // zero-length contact can leave it unnormalisable, and one NaN here
    // spreads through every effect position, sound and decal the hit
    // spawns. Facing back down the ray is the honest fallback.
    const float n2 = out.normal[0] * out.normal[0] + out.normal[1] * out.normal[1] +
                     out.normal[2] * out.normal[2];
    if (!(n2 > 1e-8f)) {              // false for NaN as well as for zero
        for (int c = 0; c < 3; ++c) out.normal[c] = -span[c] / length;
    }

    // Which script body, if any. Anything that is not one is the world, and
    // -1 is what makes ENTITY.IsFixedMesh answer true.
    for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
        if (impl_->scriptBodies[i].body == collector.mHit.mBodyID) {
            out.bodySlot = int(i);
            break;
        }
    }
    return true;
}

bool PhysicsWorld::SphereOverlaps(const float pos[3], float radius) const {
    if (!loaded()) return false;

    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();

    JPH::CollideShapeSettings settings;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    // The camera's own body sits exactly where the camera is, so every query
    // made from there would hit it first.
    const CameraBlockerFilter blockers(impl_->probe, maxPushMass_, JPH::BodyID(),
                                      impl_->pawnProbe);
    impl_->system.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sOne(),
        JPH::RMat44::sTranslation(JPH::RVec3(pos[0], pos[1], pos[2])), settings,
        JPH::RVec3::sZero(), collector, {}, kSweepLayer, blockers);
    return collector.HadHit();
}

// How far one character may be separated from another in a single call.
//
// A character overlap is resolved as a PUSH, not an ejection. Two upright
// characters standing on the ground separate SIDEWAYS, and the vertical
// component is the one that cannot be undone: a sphere driven below the floor
// mesh overlaps nothing, so no later pass can recover it. Measured before this
// existed - a monk spawning onto another sent it 0.704 straight down in one
// frame and it fell out of the level for good.
//
// GUESS: the rate is not recovered. It is set so a coincident pair of monks
// (radius 0.35) separates over about a quarter of a second, which is the
// "shoulder them aside gently" the original shows rather than a shove. What
// would settle it is the character separation term in the monster update
// inside Engine.dll. Docs/Reference/MonsterMovement.md
constexpr float kCharacterPushPerStep = 0.05f;

int PhysicsWorld::Depenetrate(float pos[3], float radius, int iterations,
                              bool solidProps, int ignoreSlot,
                              bool collideWithPlayer,
                              bool* separatedFromCharacter) const {
    if (!loaded()) return 0;

    const JPH::BodyID self = ScriptBodyExists(ignoreSlot)
                                 ? impl_->scriptBodies[ignoreSlot].body
                                 : JPH::BodyID();
    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    // The player's pusher is excluded from its OWN queries only. Leaving it out
    // of everyone's is why a monster could not feel the player at all - it
    // walked through them, and the player could not shoulder one aside.
    const CameraBlockerFilter blockers(impl_->probe, solidProps ? kSolidProps : maxPushMass_,
                                      self,
                                      collideWithPlayer ? JPH::BodyID() : impl_->pawnProbe);

    int resolved = 0;
    for (int pass = 0; pass < iterations; ++pass) {
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mCollectFacesMode = JPH::ECollectFacesMode::NoFaces;

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        impl_->system.GetNarrowPhaseQuery().CollideShape(
            &sphere, JPH::Vec3::sOne(),
            JPH::RMat44::sTranslation(JPH::RVec3(pos[0], pos[1], pos[2])), settings,
            JPH::RVec3::sZero(), collector, {}, kSweepLayer, blockers);
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
        JPH::Vec3 out = -deepest->mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
        float depth = deepest->mPenetrationDepth;

        const uint32_t hitId = deepest->mBodyID2.GetIndexAndSequenceNumber();
        const bool character = impl_->characterBodies.count(hitId) != 0 ||
                               deepest->mBodyID2 == impl_->pawnProbe;
        if (character) {
            out = JPH::Vec3(out.GetX(), 0.f, out.GetZ());
            if (out.LengthSq() < 1e-8f) {
                // One character directly above the other - which is exactly how
                // a monk spawning onto another arrives. The axis says nothing
                // about which way to part, so take the horizontal offset
                // between the centres, and failing that a fixed axis signed by
                // body order so the two pick OPPOSITE directions rather than
                // travelling together forever.
                const JPH::RVec3 otherCom =
                    impl_->system.GetBodyInterface().GetCenterOfMassPosition(deepest->mBodyID2);
                JPH::Vec3 away(pos[0] - float(otherCom.GetX()), 0.f,
                               pos[2] - float(otherCom.GetZ()));
                if (away.LengthSq() < 1e-8f)
                    away = JPH::Vec3(self.GetIndex() < deepest->mBodyID2.GetIndex() ? 1.f : -1.f,
                                     0.f, 0.f);
                out = away.Normalized();
            } else {
                out = out.Normalized();
            }
            depth = std::min(depth, kCharacterPushPerStep);
            if (separatedFromCharacter) *separatedFromCharacter = true;
        }

        for (int c = 0; c < 3; ++c) pos[c] += out[c] * depth;
        ++resolved;
        // A character push is rate-limited, so re-running the loop would just
        // spend the budget several times over in one call.
        if (character) break;
    }
    return resolved;
}

void PhysicsWorld::SlideSphere(float pos[3], const float delta[3], float radius,
                               bool solidProps, int ignoreSlot,
                               bool collideWithPlayer,
                               bool* separatedFromCharacter) const {
    const JPH::BodyID self = ScriptBodyExists(ignoreSlot)
                                 ? impl_->scriptBodies[ignoreSlot].body
                                 : JPH::BodyID();
    if (!loaded()) {
        for (int c = 0; c < 3; ++c) pos[c] += delta[c];
        return;
    }

    // Get out of anything first. A cast that starts inside geometry reports a
    // hit at zero distance in every direction, which is indistinguishable from
    // being wedged - and being wedged for good is exactly what it looks like.
    Depenetrate(pos, radius, 4, solidProps, ignoreSlot, collideWithPlayer,
                separatedFromCharacter);

    JPH::Vec3 at(pos[0], pos[1], pos[2]);
    JPH::Vec3 remaining(delta[0], delta[1], delta[2]);
    if (remaining.IsNearZero()) {
        for (int c = 0; c < 3; ++c) pos[c] = at[c];
        return;
    }

    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const CameraBlockerFilter blockers(impl_->probe, solidProps ? kSolidProps : maxPushMass_,
                                      self,
                                      collideWithPlayer ? JPH::BodyID() : impl_->pawnProbe);

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
                                                      collector, {}, kSweepLayer, blockers);
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
