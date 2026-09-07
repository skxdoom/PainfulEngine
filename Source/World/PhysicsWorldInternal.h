#pragma once

// Shared by the PhysicsWorld translation units.
//
// PhysicsWorld is one class across three files: PhysicsWorld.cpp holds the
// lifecycle, the static world, the placed props, the step and the queries;
// PhysicsScriptBodies.cpp the bodies the scripts create and the characters
// they walk; PhysicsRagdolls.cpp the corpses. The Jolt layer filters, the
// contact listener and Impl itself are here because all three need them.
//
// Everything in here was file-local before the split. It is a named namespace
// now rather than an anonymous one, which a header cannot have without giving
// every unit its own copy. Docs/Reference/Physics.md
#include <Jolt/Jolt.h>
#include "../Core/Vectors.h"

#include "PhysicsWorld.h"
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
#include "../Core/Check.h"
#include "../Core/Debug.h"
#include "../Core/CrashReport.h"
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
#include <string>
#include <vector>

namespace painful {

// Was an anonymous namespace inside PhysicsWorld.cpp.
namespace physics_detail {

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
		// Missiles too: group 5 is disabled against the player body (23) and
		// the actors (4) in the Havok filter (colgroup3.log), so nothing that
		// walks stands on a grenade. Physics.md, "Collision groups".
		return layer != Layers::kNoCollide && layer != Layers::kProbe &&
				layer != Layers::kHitbox && layer != Layers::kMissile;
	}
};
const SweepLayerFilter kSweepLayer;

inline void TraceToLog(const char* format, ...) {
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	LogInfo("jolt: %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
// Jolt's own answer to misusing its API, which lands at the CALL rather than
// where the corrupted state finally faults. Returning false skips the
// breakpoint, so a run reports every assert instead of stopping at the first.
inline bool AssertToLog(const char* expr, const char* message, const char* file, JPH::uint line) {
	LogWarn("jolt assert: %s:%u  %s%s%s", file, line, expr,
			message != nullptr ? " - " : "", message != nullptr ? message : "");
	// The assert names the rule; the stack names who broke it, which is the
	// half that says what to fix.
	LogStackHere("jolt assert");
	return false;
}
#endif

// Jolt's globals are process wide, so they are set up once and left alone -
// tearing them down between levels would invalidate every shape still held.
struct JoltRuntime {
	JoltRuntime() {
		JPH::RegisterDefaultAllocator();
		JPH::Trace = TraceToLog;
#ifdef JPH_ENABLE_ASSERTS
		JPH::AssertFailed = AssertToLog;
#endif
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
	}
	~JoltRuntime() {
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}
};

inline void EnsureJolt() {
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
	Vec3 lo{1e30f, 1e30f, 1e30f};
	Vec3 hi{-1e30f, -1e30f, -1e30f};

	void Add(const Vec3& p) {
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
inline void Thin(MeshPoints& mesh) {
	if (mesh.points.size() <= kMaxHullPoints) return;
	const size_t stride = mesh.points.size() / kMaxHullPoints + 1;
	JPH::Array<JPH::Vec3> kept;
	for (size_t i = 0; i < mesh.points.size(); i += stride) kept.push_back(mesh.points[i]);
	mesh.points = std::move(kept);
}

inline bool PackPoints(const std::string& itemsRoot, const std::string& packName,
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
			Vec3 p;
			o.position(v, p);
			out.Add(p);
		}
	}
	return !out.empty();
}

inline bool ModelPoints(const std::string& modelsRoot, const std::string& modelName, MeshPoints& out) {
	const std::string path = modelsRoot + "/" + modelName + ".pkmdl";
	if (!FileSystem::Get().Exists(path)) return false;

	Model model;
	if (!Model::Load(path, model)) return false;
	for (const ModelMesh& mesh : model.meshes) {
		for (size_t v = 0; v + 7 < mesh.verts.size(); v += 8) {
			const Vec3 p{mesh.verts[v], mesh.verts[v + 1], mesh.verts[v + 2]};
			out.Add(p);
		}
	}
	return !out.empty();
}



// The shape a prop body gets for a BodyTypes value, scaled into world units.
// FromMesh and its variants (4/5/7/11) become the mesh's convex hull - Jolt
// works out the centre of mass itself, which is the difference the original
// draws between FromMesh and FromMeshNotCentered, and a moving body cannot be
// a triangle mesh. Everything else (Simple / Sphere / Fatter / Default) is a
// sphere around the mesh.
inline JPH::ShapeSettings::ShapeResult BuildScaledPropShape(MeshPoints& mesh,
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
		const Vec3 half{(mesh.hi[0] - mesh.lo[0]) * 0.5f,
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
} // namespace physics_detail

using namespace physics_detail;


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
		Vec3 point;
		Vec3 normal;
		Vec3 velA;
		Vec3 velB;
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
		Vec3 blocked;
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
			return theirInv <= myInv; // at least my mass
		};
		const JPH::Vec3 n = manifold.mWorldSpaceNormal; // from a into b
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
		float radius = 0.f; // world-space mesh radius, for PO_GetMaxSphereRay
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
		int character = -1; // index into characters, -1 for a prop
		// The pose at the last two steps, for the render-side interpolation
		// (RecordStep / CollectScriptPoses). Docs/Reference/Physics.md,
		// "Interpolated poses".
		JPH::RVec3 p0 = JPH::RVec3::sZero(), p1 = JPH::RVec3::sZero();
		JPH::Quat q0 = JPH::Quat::sIdentity(), q1 = JPH::Quat::sIdentity();
		bool hist = false;
		bool needFinal = false; // report the rest pose once after settling
		// Active mesh record (AddMesh's pinned array, 0x34-byte entries):
		// the group byte at WorldMesh+0x7e2, whether the static twin still
		// stands, whether explosions may release it, and the bounds radius
		// the release test adds to the blast range.
		bool activeMesh = false;
		int activeGroup = -1;
		bool activePinned = false;
		bool activeTwin = false; // a "statdest" intact twin: static, host-released
		bool activeEnabled = true;
		bool activeLevelScaled = false; // Level_GetActiveMeshesData gave != 1
		float activeRadius = 0.f;
	};
	std::vector<ScriptBody> scriptBodies;
	// A character body's tick state: PhysicsObject+0x34 (wish), +0x4c (the
	// last commanded vector), +0x6c/+0x70 (movement const, floor flag), +0x71
	// (on floor), +0x60 (floor normal), +0x75 bit 3 (flying).
	struct Character {
		int slot = -1;
		float k = 0.f; // 0.2 * bodyScale: the sizer's unit
		float rootOffsetY = 0.f; // stack origin above the body position
		Vec3 wish;
		Vec3 lastWish;
		float influence = 0.5f;
		bool checkFloors = true;
		bool flying = false;
		bool onFloor = false;
		Vec3 floorNormal{0, 1, 0};
	};
	std::vector<Character> characters;
	Character* CharacterOf(int slot) {
		if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < scriptBodies.size(),
				"physics: script body slot %d of %zu", slot, scriptBodies.size()))
			return nullptr;
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
		bool simulated = false; // dynamic (dead) rather than driven (alive)
		// Per part, the pose at the last two steps (RecordStep).
		struct PartHist {
			JPH::RVec3 p0, p1;
			JPH::Quat q0, q1;
		};
		std::vector<PartHist> hist;
	};
	std::vector<RagdollInst> ragdolls;
	// Each instance gets its own collision group so two corpses in a heap
	// collide with each other while neither collides with itself.
	JPH::CollisionGroup::GroupID nextRagdollGroup = 1;

	JPH::BodyID probe;
	JPH::BodyID pawnProbe;
	Vec3 pawnProbePos;
	float pawnProbeRadius = 0.f;
	// BodyTypes.Player at bodyScale 1 (the sizer, 0x101B3E20): four spheres
	// stacked on the body's axis, centres -0.63/-0.10/+0.50/+0.90, radii
	// 0.33/0.40/0.40/0.20. The pawn slides with it, the sensor wears it, and
	// the AI's traces hit it. PlayerMovement.md, "The pawn".
	JPH::Ref<JPH::Shape> playerShape;
	static JPH::Ref<JPH::Shape> MakePlayerShape() {
		JPH::StaticCompoundShapeSettings compound;
		const float centres[4] = {-0.63f, -0.10f, 0.50f, 0.90f};
		const float radii[4] = {0.33f, 0.40f, 0.40f, 0.20f};
		for (int i = 0; i < 4; ++i)
			compound.AddShape(JPH::Vec3(0.f, centres[i], 0.f), JPH::Quat::sIdentity(),
					new JPH::SphereShape(radii[i]));
		JPH::ShapeSettings::ShapeResult result = compound.Create();
		return result.HasError() ? JPH::Ref<JPH::Shape>() : result.Get();
	}
	Vec3 probePos;
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

} // namespace painful
