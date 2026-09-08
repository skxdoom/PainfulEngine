// PhysicsWorld: the lifecycle, the static world, the placed props, the fixed
// step and the queries. The script bodies are in PhysicsScriptBodies.cpp and
// the corpses in PhysicsRagdolls.cpp; all three share PhysicsWorldInternal.h.
// The recovered rules are in Docs/Reference/Physics.md.

#include "PhysicsWorldInternal.h"
#include "../Core/Vectors.h"
#include <string>
#include <vector>

namespace painful {

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

void PhysicsWorld::SetProbeEnabled(bool on) {
	if (on == probeEnabled_) return;
	probeEnabled_ = on;
	CreateProbe();
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
	if (probeRadius_ <= 0.f || !probeEnabled_) return;

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
	if (!impl_->playerShape) impl_->playerShape = Impl::MakePlayerShape();
	if (!impl_->playerShape) return;
	// A kinematic SENSOR in the player's own silhouette: contacts are reported
	// (a can, an axe, a landing corpse), nothing is pushed by it. Moving the
	// props is the pawn's slide plus PushProps; a solid kinematic shoved them
	// at infinite mass. PlayerMovement.md, "What the player collides with".
	JPH::BodyCreationSettings body(
			impl_->playerShape,
			JPH::RVec3(impl_->pawnProbePos[0], impl_->pawnProbePos[1], impl_->pawnProbePos[2]),
			JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::kProbe);
	body.mMotionQuality = JPH::EMotionQuality::LinearCast;
	body.mIsSensor = true;
	impl_->pawnProbe = bodies.CreateAndAddBody(body, JPH::EActivation::Activate);
}

void PhysicsWorld::SetPawnProbeRadius(float radius) {
	impl_->pawnProbeRadius = radius;
	CreatePawnProbe();
}

void PhysicsWorld::SetScriptBodyAngularVelocity(int slot, const Vec3& w) {
	if (!ScriptBodyExists(slot) || !impl_->scriptBodies[size_t(slot)].inWorld) return;
	impl_->system.GetBodyInterface().SetAngularVelocity(impl_->scriptBodies[size_t(slot)].body,
			JPH::Vec3(w[0], w[1], w[2]));
}

// Same contract as MoveProbe: aimed here, driven inside the fixed step, and
// teleported rather than swept across a jump that is really a respawn.
void PhysicsWorld::MovePawnProbe(const Vec3& pos, bool push) {
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

void PhysicsWorld::MoveProbe(const Vec3& pos, bool push) {
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

	for (size_t objectIndex = 0; objectIndex < map.objects.size(); ++objectIndex) {
		const MapObject& o = map.objects[objectIndex];
		if (!o.isCollidable()) continue;
		// CreateActiveMeshBody's, and CreateStaticTwinBody's: a destructible's
		// intact twin is its own static body, so its release can remove it.
		// A glass pane goes the same way, so breaking one can take it out.
		if (promoteActiveMeshes && (o.isActiveMesh() || o.isStaticTwin() || o.isGlass()))
			continue;
		const JPH::uint32 base = static_cast<JPH::uint32>(vertices.size());
		for (size_t v = 0; v < o.vertexCount(); ++v) {
			Vec3 p, w;
			o.position(v, p);
			// Every shipped map has this at identity, but honouring it costs
			// nothing and avoids a silent wrong answer if one ever does not.
			w = o.transform.TransformPoint(p);
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
			// The object index rides along as the triangle's user data, so a
			// trace can say which .mpk object it hit (RayHit::worldObject).
			triangles.push_back(JPH::IndexedTriangle(base + a, base + c, base + b, 0,
					JPH::uint32(objectIndex)));
		}
	}

	if (triangles.empty()) {
		LogWarn("physics: level has no collidable geometry");
		return false;
	}

	JPH::MeshShapeSettings meshSettings(std::move(vertices), std::move(triangles));
	meshSettings.mPerTriangleUserData = true;
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

		float rot9[9];
		ReadRotation(e.props, rot9);
		// The engine's 3x3 is row-vector; Jolt is column-vector, and the two
		// are transposes, so engine row j is Jolt column j.
		JPH::Mat44 basis = JPH::Mat44::sIdentity();
		for (int j = 0; j < 3; ++j)
			basis.SetColumn3(j, JPH::Vec3(rot9[j * 3 + 0], rot9[j * 3 + 1], rot9[j * 3 + 2]));

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

// Shifts every body's step history: what was current becomes previous, the
// solver's pose becomes current. The read-backs blend the two by Alpha(), so
// a frame between steps still moves everything. Physics.md, "Interpolated poses".
void PhysicsWorld::RecordStep() {
	const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
	for (Impl::ScriptBody& sb : impl_->scriptBodies) {
		if (sb.body.IsInvalid() || !sb.inWorld) continue;
		JPH::RVec3 p;
		JPH::Quat q;
		bodies.GetPositionAndRotation(sb.body, p, q);
		if (!sb.hist) { sb.p0 = p; sb.q0 = q; sb.hist = true; }
		else { sb.p0 = sb.p1; sb.q0 = sb.q1; }
		sb.p1 = p;
		sb.q1 = q;
		// Settled: the two poses agree, so one more report lands it exactly.
		if (sb.p0 == sb.p1 && sb.q0 == sb.q1) sb.needFinal = true;
	}
	for (Impl::RagdollInst& inst : impl_->ragdolls) {
		if (inst.ragdoll == nullptr) continue;
		const size_t n = inst.ragdoll->GetBodyCount();
		std::vector<JPH::Mat44> mats(n);
		JPH::RVec3 rootOffset = JPH::RVec3::sZero();
		inst.ragdoll->GetPose(rootOffset, mats.data());
		const bool fresh = inst.hist.size() != n;
		if (fresh) inst.hist.resize(n);
		for (size_t i = 0; i < n; ++i) {
			Impl::RagdollInst::PartHist& h = inst.hist[i];
			const JPH::RVec3 p = JPH::RVec3(mats[i].GetTranslation()) + rootOffset;
			const JPH::Quat q = mats[i].GetQuaternion();
			if (fresh) { h.p0 = p; h.q0 = q; }
			else { h.p0 = h.p1; h.q0 = h.q1; }
			h.p1 = p;
			h.q1 = q;
		}
	}
}

// How far the frame is past the last step, 0..1.
float PhysicsWorld::Alpha() const {
	const float a = impl_->accumulator / kStep;
	return a < 0.f ? 0.f : (a > 1.f ? 1.f : a);
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
		RecordStep();
		// PAINFUL_CHAR_TRACE: a character whose velocity the step changed by
		// more than 4 units/s, and what it was touching.
		static const bool traceChars = DebugFlag("PAINFUL_CHAR_TRACE");
		if (traceChars) {
			for (const Impl::Character& ch : impl_->characters) {
				if (ch.slot < 0) continue;
				const Impl::ScriptBody& sb = impl_->scriptBodies[size_t(ch.slot)];
				if (sb.body.IsInvalid() || !sb.inWorld) continue;
				const JPH::Vec3 v = bodies.GetLinearVelocity(sb.body);
				const JPH::Vec3 cmd(ch.lastWish[0], ch.lastWish[1], ch.lastWish[2]);
				const JPH::Vec3 dv = v - cmd;
				static const float minDv = DebugFloat("PAINFUL_CHAR_TRACE_MIN", 4.f);
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
			for (int c = 0; c < 3; ++c) pose.rot9[j * 3 + c] = column[c];
		}
		out.push_back(pose);
	}
}

} // namespace painful
