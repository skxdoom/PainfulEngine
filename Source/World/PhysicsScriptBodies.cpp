// PhysicsWorld: the bodies the scripts create, and the characters they walk.
// Split out of PhysicsWorld.cpp at its own banner; see PhysicsWorldInternal.h.

#include "PhysicsWorldInternal.h"
#include "../Core/Vectors.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace painful {

// ------------------------------------------------------------ script bodies

// The engine applies its textbook quaternion matrix to ROW vectors, which in
// standard column convention is the rotation by the CONJUGATE - the same
// transpose CollectPoses and LoadProps handle for matrices, expressed on the
// quaternion itself.
static JPH::Quat EngineQuatToJolt(const Quat& q) {
	JPH::Quat j(-q.x, -q.y, -q.z, q.w);
	return j.LengthSq() < 1e-12f ? JPH::Quat::sIdentity() : j.Normalized();
}

static Quat JoltQuatToEngine(const JPH::Quat& j) {
	return Quat(j.GetW(), -j.GetX(), -j.GetY(), -j.GetZ());
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
		const Vec3& pos, const Quat& rot,
		const std::string& dataRoot, int collisionGroup,
		float sphereRadius) {
	if (impl_->worldBody.IsInvalid()) return -1; // no world, nothing to rest on

	MeshPoints mesh;
	if (!packName.empty()) {
		if (!PackPoints(dataRoot + "/Items", packName, packMesh, mesh)) return -1;
	} else {
		if (modelName.empty() || !ModelPoints(dataRoot + "/Models", modelName, mesh))
			return -1;
	}
	if (scale <= 0.f) return -1;

	float radius = mesh.radius() * scale; // world-space
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
			EngineQuatToJolt(rot),
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
	// PO_SetCollisionGroup turns a driven projectile into a dynamic body.
	body.mAllowDynamicOrKinematic = true;
	// The sizer's mass for a mesh body: (0.2 * scale)^3 * 10000 (0x101B3E20,
	// constants 0x102B3B80 and 0x102C8658) - 80 for the player at scale 1,
	// 0.64 for a scale-0.2 crate. Jolt's density gave a small crate hundreds
	// of kilos and made it a wall to the player. PO_SetMass still overrides.
	// The sphere cases keep Jolt's own mass. Physics.md, "The props".
	if (sphereRadius <= 0.f) {
		const float k = 0.2f * scale;
		body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		body.mMassPropertiesOverride.mMass = std::max(0.01f, k * k * k * 10000.f);
	}

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

	// BodyID -> (ragdoll slot, part): a limb is a side too, for the joints
	// ENTITY.EnableCollisionsToRagdoll asked about.
	std::unordered_map<uint32_t, std::pair<int, int>> limbOf;
	for (size_t r = 0; r < impl_->ragdolls.size(); ++r) {
		const JPH::Ragdoll* rd = impl_->ragdolls[r].ragdoll;
		if (rd == nullptr) continue;
		for (size_t b = 0; b < rd->GetBodyCount(); ++b)
			limbOf[rd->GetBodyID(int(b)).GetIndexAndSequenceNumber()] = {int(r), int(b)};
	}

	for (const ScriptContactListener::Pending& p : pending) {
		const auto a = slotOf.find(p.a.GetIndexAndSequenceNumber());
		const auto b = slotOf.find(p.b.GetIndexAndSequenceNumber());
		const auto la = limbOf.find(p.a.GetIndexAndSequenceNumber());
		const auto lb = limbOf.find(p.b.GetIndexAndSequenceNumber());
		// ONE side is enough. Requiring both was wrong and it hid the common
		// case: almost everything a prop hits is the STATIC WORLD - a vase
		// pushed off a balcony lands on the floor, not on another prop - and
		// that collision is exactly the one a destructible breaks on. The world
		// side reports slot -1, which becomes entity 0 in the message, the same
		// stand-in a world hit already uses in the traces.
		if (a == slotOf.end() && b == slotOf.end() && la == limbOf.end() && lb == limbOf.end())
			continue;
		ScriptContact c;
		c.slotA = a == slotOf.end() ? -1 : a->second;
		c.slotB = b == slotOf.end() ? -1 : b->second;
		if (la != limbOf.end()) { c.ragdollA = la->second.first; c.partA = la->second.second; }
		if (lb != limbOf.end()) { c.ragdollB = lb->second.first; c.partB = lb->second.second; }
		c.pawnA = !impl_->pawnProbe.IsInvalid() && p.a == impl_->pawnProbe;
		c.pawnB = !impl_->pawnProbe.IsInvalid() && p.b == impl_->pawnProbe;
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
float PhysicsWorld::ScriptBodyMass(int slot) const {
	return ScriptBodyExists(slot) ? impl_->scriptBodies[slot].mass : 0.f;
}

void PhysicsWorld::ActivateScriptBody(int slot, bool on) {
	if (!ScriptBodyExists(slot)) return;
	const JPH::BodyID id = impl_->scriptBodies[slot].body;
	if (id.IsInvalid()) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	if (on) bodies.ActivateBody(id);
	else bodies.DeactivateBody(id);
}

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
	Vec3 inv; // locked, locked, locked
	switch (mode) {
	case 1: inv[1] = 0.1f; break; // YAxis
	case 5: inv[0] = 0.1f; break; // XAxis
	case 6: inv[2] = 0.1f; break; // ZAxis
	case 4: { // HardTurn
		const float i = std::max(1e-4f, softness * 10.f);
		inv[0] = inv[1] = inv[2] = 1.f / i;
		break;
	}
	default: break; // Disabled: no rotation
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
		Vec3& outOrigin) {
	if (impl_->worldBody.IsInvalid() || object.vertexCount() == 0) return -1;
	// World-space points, and the bounds centre the body is built about.
	MeshPoints mesh;
	for (size_t v = 0; v < object.vertexCount(); ++v) {
		Vec3 p, w;
		object.position(v, p);
		w = object.transform.TransformPoint(p);
		w *= worldScale;
		mesh.Add(w);
	}
	Vec3 origin;
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
	body.mEnhancedInternalEdgeRemoval = true; // see MakeScriptBodyCharacter
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

// AddMesh's "statdest" branch: the intact twin is fixed geometry of its own,
// not part of the world body, so the release can take it out. Exact triangles,
// wound the world's way (see BuildStaticWorld); pieces sit inside it.
int PhysicsWorld::CreateStaticTwinBody(const MapObject& object, float worldScale, int group,
		Vec3& outOrigin) {
	if (impl_->worldBody.IsInvalid() || object.vertexCount() == 0 || object.indices.size() < 3)
		return -1;
	MeshPoints mesh;
	JPH::VertexList vertices;
	for (size_t v = 0; v < object.vertexCount(); ++v) {
		Vec3 p, w;
		object.position(v, p);
		w = object.transform.TransformPoint(p);
		w *= worldScale;
		mesh.Add(w);
	}
	Vec3 origin;
	for (int c = 0; c < 3; ++c) origin[c] = (mesh.lo[c] + mesh.hi[c]) * 0.5f;
	for (JPH::Vec3& p : mesh.points) {
		p -= JPH::Vec3(origin[0], origin[1], origin[2]);
		vertices.push_back(JPH::Float3(p.GetX(), p.GetY(), p.GetZ()));
	}
	JPH::IndexedTriangleList triangles;
	for (size_t t = 0; t + 2 < object.indices.size(); t += 3) {
		const uint32_t a = object.indices[t], b = object.indices[t + 1], c = object.indices[t + 2];
		if (a >= object.vertexCount() || b >= object.vertexCount() || c >= object.vertexCount())
			continue;
		triangles.push_back(JPH::IndexedTriangle(a, c, b, 0));
	}
	if (triangles.empty()) return -1;
	JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
	settings.Sanitize();
	settings.SetEmbedded();
	JPH::ShapeSettings::ShapeResult shape = settings.Create();
	if (shape.HasError()) return -1;

	JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3(origin[0], origin[1], origin[2]),
			JPH::Quat::sIdentity(), JPH::EMotionType::Static,
			Layers::kNonMoving);
	body.mFriction = settings_.meshFriction;
	body.mRestitution = settings_.meshRestitution;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	const JPH::BodyID id = bodies.CreateAndAddBody(body, JPH::EActivation::DontActivate);
	if (id.IsInvalid()) return -1;
	Impl::ScriptBody sb;
	sb.body = id;
	sb.radius = mesh.radius();
	sb.activeMesh = true;
	sb.activeTwin = true;
	sb.activeGroup = group;
	sb.activeRadius = mesh.radius();
	sb.activeLevelScaled = true; // no mass to scale
	impl_->scriptBodies.push_back(sb);
	for (int c = 0; c < 3; ++c) outOrigin[c] = origin[c];
	return int(impl_->scriptBodies.size() - 1);
}

bool PhysicsWorld::IsStaticTwin(int slot) const {
	return ScriptBodyExists(slot) && impl_->scriptBodies[size_t(slot)].activeTwin;
}

void PhysicsWorld::ScaleUnscaledActiveMeshes(float massScale) {
	if (massScale == 1.f) return;
	for (Impl::ScriptBody& sb : impl_->scriptBodies) {
		if (!sb.activeMesh || sb.activeLevelScaled || sb.body.IsInvalid()) continue;
		sb.activeLevelScaled = true;
		// A pinned body is static and has no mass yet; scale on release.
		sb.mass = massScale; // for a pinned body: the factor to apply at release
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

void PhysicsWorld::ActivateActiveMeshGroup(int group, std::vector<int>& twinsOut) {
	twinsOut.clear();
	for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
		Impl::ScriptBody& sb = impl_->scriptBodies[i];
		if (!sb.activeMesh || sb.activeGroup != group || sb.body.IsInvalid()) continue;
		sb.activeEnabled = true;
		if (sb.activeTwin) twinsOut.push_back(int(i));
		else if (sb.activePinned) SetScriptBodyPinned(int(i), false);
	}
}

void PhysicsWorld::EnableActiveMeshGroup(int group, bool enabled) {
	for (Impl::ScriptBody& sb : impl_->scriptBodies)
		if (sb.activeMesh && sb.activeGroup == group) sb.activeEnabled = enabled;
}

void PhysicsWorld::UnpinActiveMeshesNear(const Vec3& centre, float range,
		std::vector<int>& out) {
	out.clear();
	const JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
		const Impl::ScriptBody& sb = impl_->scriptBodies[i];
		if (!sb.activeMesh || !(sb.activePinned || sb.activeTwin) || !sb.activeEnabled ||
				sb.body.IsInvalid() || !sb.inWorld)
			continue;
		const JPH::RVec3 p = bodies.GetPosition(sb.body);
		const float d = std::sqrt(float((p.GetX() - centre[0]) * (p.GetX() - centre[0]) +
				(p.GetY() - centre[1]) * (p.GetY() - centre[1]) +
				(p.GetZ() - centre[2]) * (p.GetZ() - centre[2])));
		if (d < range + sb.activeRadius) out.push_back(int(i));
	}
	for (int slot : out)
		if (!impl_->scriptBodies[size_t(slot)].activeTwin) SetScriptBodyPinned(slot, false);
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

void PhysicsWorld::SetScriptBodyPose(int slot, const Vec3& pos,
		const Quat& rot) {
	if (!ScriptBodyExists(slot)) return;
	// A disabled body is out of the world; Jolt will not move one.
	if (!impl_->scriptBodies[size_t(slot)].inWorld) return;
	impl_->system.GetBodyInterface().SetPositionAndRotation(
			impl_->scriptBodies[slot].body, JPH::RVec3(pos[0], pos[1], pos[2]),
			EngineQuatToJolt(rot), JPH::EActivation::Activate);
	// A teleported character lands at its model origin, like a spawn.
	if (impl_->scriptBodies[size_t(slot)].character >= 0) StandCharacterOnFloor(slot, 100.f);
	// A teleport is not a motion to blend across.
	impl_->scriptBodies[size_t(slot)].hist = false;
}


void PhysicsWorld::SetScriptBodyCollisionGroup(int slot, int collisionGroup) {
	if (!ScriptBodyExists(slot)) return;
	Impl::ScriptBody& sb = impl_->scriptBodies[size_t(slot)];
	if (!sb.inWorld || sb.character >= 0) return;
	// CreateScriptBody's rule: 7 driven and touching nothing, 1 kinematic,
	// 5/8 a missile, anything else an ordinary dynamic body.
	const bool projectile = collisionGroup == 7;
	const bool fixedRigid = collisionGroup == 1;
	const bool missile = collisionGroup == 5 || collisionGroup == 8;
	const JPH::EMotionType motion = (projectile || fixedRigid) ? JPH::EMotionType::Kinematic
			: JPH::EMotionType::Dynamic;
	const JPH::ObjectLayer layer =
		projectile ? Layers::kNoCollide : (missile ? Layers::kMissile : Layers::kMoving);
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	if (bodies.GetMotionType(sb.body) != motion)
		bodies.SetMotionType(sb.body, motion, JPH::EActivation::Activate);
	if (bodies.GetObjectLayer(sb.body) != layer) bodies.SetObjectLayer(sb.body, layer);
	bodies.ActivateBody(sb.body);
}

void PhysicsWorld::SetScriptBodyVelocity(int slot, const Vec3& v) {
	if (!ScriptBodyExists(slot)) return;
	// A disabled body is out of the world, and ACTIVATING ONE CORRUPTS THE
	// SOLVER: Jolt puts it in the active list without a broadphase entry, and
	// the next DestroyBody frees it there, so the following step reads a dead
	// body in JobApplyGravity. Same guard as SetScriptBodyPose.
	// Docs/Reference/Physics.md, "Activation and a body out of the world"
	if (!impl_->scriptBodies[size_t(slot)].inWorld) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	const JPH::BodyID id = impl_->scriptBodies[slot].body;
	bodies.SetLinearVelocity(id, JPH::Vec3(v[0], v[1], v[2]));
	// A body given a velocity is meant to move, and a sleeping one would sit
	// there holding it.
	if (!bodies.IsActive(id)) bodies.ActivateBody(id);
}


void PhysicsWorld::AddScriptBodyImpulse(int slot, const Vec3& at,
		const Vec3& impulse) {
	if (!ScriptBodyExists(slot)) return;
	// Out of the world takes no impulse - and must not be woken for one.
	// See SetScriptBodyVelocity for what activating a removed body does.
	if (!impl_->scriptBodies[size_t(slot)].inWorld) return;
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

bool PhysicsWorld::GetScriptBodyVelocity(int slot, Vec3& out) const {
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
	else bodies.RemoveBody(sb.body);
	sb.inWorld = enabled;
}

void PhysicsWorld::MakeScriptBodyCharacter(int slot, float k, const Vec3& rootOffset) {
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
		// Shifted DOWN 0.7k, so the lowest sphere's bottom (-4.8k) lands on the
		// floor point (-5.5k) - the model's soles - and the top sits under the
		// head (4.8k) instead of 0.7k above it. That is where the original's
		// body evidently is: actors stand on their soles and monks fit the
		// arches their models fit. ASSUMED - the sizer centres the records on
		// the entity, and what moves them in Havok is not recovered.
		// Docs/Reference/MonsterMovement.md, "The body".
		constexpr float kStackShift = -0.7f;
		for (const Ball& ball : kBalls) {
			JPH::SphereShapeSettings* sphere = new JPH::SphereShapeSettings(ball.r * k);
			compound.AddShape(JPH::Vec3(rootOffset[0], (ball.y + kStackShift) * k + rootOffset[1],
					rootOffset[2]),
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
	ch.rootOffsetY = rootOffset[1];

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
			static const float charFriction = DebugFloat("PAINFUL_CHAR_FRICTION", 0.1f);
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
	// The stack's physical bottom: the lowest sphere is shifted 0.7k down in
	// MakeScriptBodyCharacter, so it ends at the floor point (-5.5k), the
	// model's soles. Docs/Reference/MonsterMovement.md, "The body".
	const float bottom = float(p.GetY()) + ch->rootOffsetY - 5.5f * ch->k;
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

void PhysicsWorld::SetCharacterWish(int slot, const Vec3& v) {
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

bool PhysicsWorld::CharacterOnFloor(int slot, Vec3& normal) const {
	const Impl::Character* ch = impl_->CharacterOf(slot);
	if (!ch) return false;
	if (normal) for (int c = 0; c < 3; ++c) normal[c] = ch->floorNormal[c];
	return ch->onFloor;
}

// GetPawnFloorPos (0x10189390) is body.y - 1.1 * bodyScale and GetPawnHeadPos
// (0x10189340) body.y + 0.9 * bodyScale; in the sizer's unit k = 0.2 *
// bodyScale that is -5.5k and +4.5k off the stack's origin.
bool PhysicsWorld::CharacterFloorPos(int slot, Vec3& out) const {
	const Impl::Character* ch = impl_->CharacterOf(slot);
	if (!ch || !ScriptBodyExists(slot)) return false;
	const JPH::RVec3 p =
		impl_->system.GetBodyInterface().GetPosition(impl_->scriptBodies[size_t(slot)].body);
	out[0] = float(p.GetX());
	out[1] = float(p.GetY()) + ch->rootOffsetY - 5.5f * ch->k;
	out[2] = float(p.GetZ());
	return true;
}

bool PhysicsWorld::CharacterHeadPos(int slot, Vec3& out) const {
	const Impl::Character* ch = impl_->CharacterOf(slot);
	if (!ch || !ScriptBodyExists(slot)) return false;
	const JPH::RVec3 p =
		impl_->system.GetBodyInterface().GetPosition(impl_->scriptBodies[size_t(slot)].body);
	out[0] = float(p.GetX());
	out[1] = float(p.GetY()) + ch->rootOffsetY + 4.5f * ch->k;
	out[2] = float(p.GetZ());
	return true;
}

void PhysicsWorld::SetScriptBodyRotation(int slot, const Quat& rot) {
	if (!ScriptBodyExists(slot) || !impl_->scriptBodies[size_t(slot)].inWorld) return;
	impl_->system.GetBodyInterface().SetRotation(impl_->scriptBodies[size_t(slot)].body,
			EngineQuatToJolt(rot),
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
		// airborne 49% of their steps and cost speed on every landing, and
		// holding them at an exact height off a ray that ended there was a
		// 0.027 sawtooth every three frames.
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

void PhysicsWorld::ShoveCharacters(const Vec3& pos, float radius, const Vec3& dir,
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
		if (bodies.GetMotionType(hit.mBodyID2) != JPH::EMotionType::Dynamic) continue;
		const bool character =
			impl_->characterBodies.count(hit.mBodyID2.GetIndexAndSequenceNumber()) != 0;
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
		// Characters only: props meet the pawn's own 80 kg body in the solver.
		if (!character) continue;
		const float share = 0.5f * speed * pusherMass / (pusherMass + mass);
		const JPH::Vec3 v = bodies.GetLinearVelocity(hit.mBodyID2);
		const float have = v.Dot(along);
		if (have < share) bodies.AddLinearVelocity(hit.mBodyID2, along * (share - have));
	}
}

// The contact between a dynamic 80 kg body re-commanded at `speed` and the
// prop it walks into, per frame: dv = M / (M + m) * (speed - have) along the
// walk. The player's shape, nudged a little ahead, finds the props it is
// pressing on. Characters keep ShoveCharacters; the world and pinned bodies
// are not dynamic and block. PlayerMovement.md, "What the player collides with".
void PhysicsWorld::PushProps(const Vec3& centre, const Vec3& dir, float speed,
		float pusherMass) {
	if (!loaded() || speed <= 0.f) return;
	const JPH::Vec3 d(dir[0], 0.f, dir[2]);
	if (d.LengthSq() < 1e-8f) return;
	const JPH::Vec3 along = d.Normalized();
	if (!impl_->playerShape) impl_->playerShape = Impl::MakePlayerShape();
	if (!impl_->playerShape) return;
	JPH::CollideShapeSettings settings;
	settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
	settings.mCollectFacesMode = JPH::ECollectFacesMode::NoFaces;
	settings.mMaxSeparationDistance = 0.06f;
	JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
	// The centre-of-mass transform, as CollideShape wants; `centre` is the origin.
	const JPH::RVec3 at = JPH::RVec3(centre[0] + along.GetX() * 0.04f, centre[1],
			centre[2] + along.GetZ() * 0.04f) +
			JPH::RVec3(impl_->playerShape->GetCenterOfMass());
	impl_->system.GetNarrowPhaseQuery().CollideShape(
			impl_->playerShape.GetPtr(), JPH::Vec3::sOne(), JPH::RMat44::sTranslation(at), settings,
			JPH::RVec3::sZero(), collector, {}, kSweepLayer, {});
	if (collector.mHits.empty()) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	for (const JPH::CollideShapeResult& hit : collector.mHits) {
		const JPH::BodyID id = hit.mBodyID2;
		if (id == impl_->pawnProbe || id == impl_->probe) continue;
		if (impl_->characterBodies.count(id.GetIndexAndSequenceNumber()) != 0) continue;
		if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) continue;
		float mass = pusherMass;
		{
			JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), id);
			if (lock.Succeeded() && lock.GetBody().GetMotionProperties() &&
					lock.GetBody().GetMotionProperties()->GetInverseMass() > 0.f)
				mass = 1.f / lock.GetBody().GetMotionProperties()->GetInverseMass();
		}
		if (mass > maxPushMass_) continue;
		const float have = bodies.GetLinearVelocity(id).Dot(along);
		if (have >= speed) continue;
		const float dv = pusherMass / (pusherMass + mass) * (speed - have);
		bodies.AddLinearVelocity(id, along * dv);
	}
}

void PhysicsWorld::PressGround(const Vec3& feet, float radius, float force) {
	if (!loaded() || force <= 0.f) return;
	// A little below the feet, so a body the sphere rests on (skin 0.02) is
	// inside the query.
	const JPH::SphereShape sphere(radius + 0.06f);
	sphere.SetEmbedded();
	JPH::CollideShapeSettings settings;
	settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
	settings.mCollectFacesMode = JPH::ECollectFacesMode::NoFaces;
	JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
	impl_->system.GetNarrowPhaseQuery().CollideShape(
			&sphere, JPH::Vec3::sOne(),
			JPH::RMat44::sTranslation(JPH::RVec3(feet[0], feet[1], feet[2])), settings,
			JPH::RVec3::sZero(), collector, {}, kSweepLayer, {});
	if (collector.mHits.empty()) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	for (const JPH::CollideShapeResult& hit : collector.mHits) {
		const JPH::BodyID id = hit.mBodyID2;
		if (impl_->characterBodies.count(id.GetIndexAndSequenceNumber()) != 0) continue;
		if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) continue;
		// Only what is UNDER the feet, not a wall the sphere brushes.
		if (float(hit.mContactPointOn2.GetY()) > feet[1] - 0.5f * radius) continue;
		bodies.AddForce(id, JPH::Vec3(0.f, -force, 0.f), JPH::RVec3(hit.mContactPointOn2));
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
bool PhysicsWorld::ScriptBodyBounds(int slot, Vec3& lo, Vec3& hi) const {
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

bool PhysicsWorld::GetScriptBodyPosition(int slot, Vec3& out) const {
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
	sb.hist = false;
	sb.needFinal = false;
}

void PhysicsWorld::CollectScriptPoses(std::vector<ScriptBodyPose>& out,
		bool activeOnly) const {
	out.clear();

	const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
	const float alpha = Alpha();
	for (size_t slot = 0; slot < impl_->scriptBodies.size(); ++slot) {
		Impl::ScriptBody& sb = impl_->scriptBodies[slot];
		const JPH::BodyID id = sb.body;
		if (id.IsInvalid() || !sb.inWorld) continue;
		// Still blending toward a rest pose counts as moving, and a settled
		// body is reported once more so it lands exactly where it stopped.
		const bool blending = sb.hist && (sb.p0 != sb.p1 || sb.q0 != sb.q1);
		if (activeOnly && !bodies.IsActive(id) && !blending && !sb.needFinal) continue;
		if (!blending) sb.needFinal = false;

		JPH::RVec3 position;
		JPH::Quat rotation;
		if (sb.hist) {
			position = sb.p0 + (sb.p1 - sb.p0) * alpha;
			rotation = sb.q0.SLERP(sb.q1, alpha).Normalized();
		} else {
			bodies.GetPositionAndRotation(id, position, rotation);
		}

		ScriptBodyPose pose;
		pose.slot = int(slot);
		for (int c = 0; c < 3; ++c) pose.pos[c] = float(position[c]);
		pose.rot = JoltQuatToEngine(rotation);
		out.push_back(pose);
	}
}

} // namespace painful
