// PhysicsWorld: ragdolls - the constraint graph a .hke describes, and the
// corpses built from it. Split out of PhysicsWorld.cpp at its own banner.

#include "PhysicsWorldInternal.h"
#include "../Core/Vectors.h"
#include "../Core/Matrix.h"
#include <string>
#include <vector>

namespace painful {

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
JPH::Quat AngleAxis(float angle, const Vec3& axis) {
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

JPH::Vec3 V3(const Vec3& v) { return JPH::Vec3(v[0], v[1], v[2]); }

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
	if (points.size() < 4) return JPH::ShapeSettings::ShapeResult(); // an error result
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
	if (tau <= 0.f) return spring; // frequency 0 keeps hard limits
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
		// hkStiffSpringConstraint holds its points SPRING_LENGTH apart, which
		// is the authored distance everywhere but Cat_bridge1, where the
		// planks are authored closer than the rope. Physics.md, "Stiff springs".
		if (c.springLength > 0.f)
			d->mMinDistance = d->mMaxDistance = c.springLength * scale;
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
		std::vector<int> edge(def.bodies.size(), -1); // constraint used
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
				if (c.bodyA == me) otherName = c.bodyB;
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
			else part.SetShape(new JPH::SphereShape(0.1f));
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
			// which is what "take it from here" looks like. MASS 0 is Havok's
			// FIXED body - the wall end of a lamp, chain, door or bridge - so
			// it is kinematic here and never moves. Physics.md, "Fixed bodies".
			if (b.mass > 0.f) {
				part.mOverrideMassProperties =
					JPH::EOverrideMassProperties::CalculateInertia;
				part.mMassPropertiesOverride.mMass = b.mass;
			} else {
				part.mMotionType = JPH::EMotionType::Kinematic;
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
		props.mInertia(3, 3) = 1.f; // the row Jolt keeps as the affine tail
		mp->SetMassProperties(JPH::EAllowedDOFs::All, props);
	}
}

void PhysicsWorld::AddRagdollImpulse(int slot, const Vec3& at, const Vec3& impulse) {
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

void PhysicsWorld::AddRagdollPartImpulse(int slot, int part, const Vec3& at,
		const Vec3& impulse) {
	if (!RagdollExists(slot) || part < 0) return;
	const auto& ids = impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs();
	if (size_t(part) >= ids.size()) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	const JPH::BodyID id = ids[size_t(part)];
	if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) return;
	if (!bodies.IsActive(id)) bodies.ActivateBody(id);
	bodies.AddImpulse(id, JPH::Vec3(impulse[0], impulse[1], impulse[2]),
			JPH::RVec3(at[0], at[1], at[2]));
}

void PhysicsWorld::SetRagdollVelocity(int slot, const Vec3& linear, const Vec3& angular) {
	if (!RagdollExists(slot)) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	const JPH::Vec3 v(linear[0], linear[1], linear[2]);
	const JPH::Vec3 w(angular[0], angular[1], angular[2]);
	for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs()) {
		if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) continue;
		if (!bodies.IsActive(id)) bodies.ActivateBody(id);
		bodies.SetLinearAndAngularVelocity(id, v, w);
	}
}

bool PhysicsWorld::GetRagdollPartVelocity(int slot, int part, Vec3& linear,
		Vec3& angular) const {
	if (!RagdollExists(slot) || part < 0) return false;
	// Jolt keeps one body per skeleton joint, in joint order - the same order
	// as RagdollBones.
	const auto& ids = impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs();
	if (size_t(part) >= ids.size()) return false;
	const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
	const JPH::Vec3 v = bodies.GetLinearVelocity(ids[size_t(part)]);
	const JPH::Vec3 w = bodies.GetAngularVelocity(ids[size_t(part)]);
	linear[0] = v.GetX(); linear[1] = v.GetY(); linear[2] = v.GetZ();
	angular[0] = w.GetX(); angular[1] = w.GetY(); angular[2] = w.GetZ();
	return true;
}

// FUN_101B0DC0's rule, per limb: skip anything at or beyond `range`, or so
// close to the centre (<= 0.0001, the float at 0x102C8C58) that there is no
// direction to push along; otherwise the impulse is (strength / limbCount) *
// (1 - d / range) along (limb - centre). The original measures d to the
// limb's nearest surface point when it is within 3 * range; the centre of
// mass stands in for that here.
void PhysicsWorld::RagdollSelfExplosion(int slot, const Vec3& centre, float strength,
		float range) {
	if (!RagdollExists(slot) || range <= 0.f) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	const auto& ids = impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs();
	if (ids.empty()) return;
	const float perLimb = strength / float(ids.size());
	const JPH::Vec3 c(centre[0], centre[1], centre[2]);
	for (JPH::BodyID id : ids) {
		if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) continue;
		const JPH::Vec3 away = JPH::Vec3(bodies.GetCenterOfMassPosition(id)) - c;
		const float d = away.Length();
		if (d >= range || d <= 0.0001f) continue;
		const JPH::Vec3 impulse = away * (perLimb * (1.f - d / range) / d);
		if (!bodies.IsActive(id)) bodies.ActivateBody(id);
		bodies.AddImpulse(id, impulse);
	}
}

bool PhysicsWorld::GetRagdollPartPosition(int slot, int part, Vec3& out) const {
	if (!RagdollExists(slot) || part < 0) return false;
	const auto& ids = impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs();
	if (size_t(part) >= ids.size()) return false;
	const JPH::RVec3 p = impl_->system.GetBodyInterfaceNoLock().GetPosition(ids[size_t(part)]);
	out[0] = float(p.GetX()); out[1] = float(p.GetY()); out[2] = float(p.GetZ());
	return true;
}

void PhysicsWorld::SetRagdollPartPosition(int slot, int part, const Vec3& pos) {
	if (!RagdollExists(slot) || part < 0) return;
	const auto& ids = impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs();
	if (size_t(part) >= ids.size()) return;
	impl_->system.GetBodyInterface().SetPosition(ids[size_t(part)],
			JPH::RVec3(pos[0], pos[1], pos[2]),
			JPH::EActivation::Activate);
	// A dragged limb is placed, not moved: no blend across it.
	impl_->ragdolls[size_t(slot)].hist.clear();
}

void PhysicsWorld::PinRagdollPart(int slot, int part) {
	if (!RagdollExists(slot) || part < 0) return;
	const auto& ids = impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs();
	if (size_t(part) >= ids.size()) return;
	JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
	const JPH::BodyID id = ids[size_t(part)];
	bodies.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
	if (bodies.GetMotionType(id) != JPH::EMotionType::Kinematic)
		bodies.SetMotionType(id, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
}

void PhysicsWorld::RagdollPartPositions(int slot, std::vector<float>& outXYZ) const {
	outXYZ.clear();
	if (!RagdollExists(slot)) return;
	const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
	for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs()) {
		const JPH::RVec3 p = bodies.GetPosition(id);
		outXYZ.push_back(float(p.GetX()));
		outXYZ.push_back(float(p.GetY()));
		outXYZ.push_back(float(p.GetZ()));
	}
}

void PhysicsWorld::SetRagdollPose(int slot, const float* boneMatrices, bool kinematic) {
	if (!RagdollExists(slot) || boneMatrices == nullptr) return;
	Impl::RagdollInst& inst = impl_->ragdolls[size_t(slot)];
	const size_t n = inst.bones.size();
	// A placed pose (the animation driving a live one, or the death
	// handover) starts the step history afresh.
	inst.hist.clear();

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
	const JPH::Array<JPH::BodyID>& ids = inst.ragdoll->GetBodyIDs();
	const JPH::RagdollSettings* settings = inst.ragdoll->GetRagdollSettings();
	for (size_t i = 0; i < ids.size(); ++i) {
		const JPH::BodyID id = ids[i];
		// A fixed part (mass 0) stays kinematic whatever the rest becomes.
		const bool fixed = settings != nullptr && i < settings->mParts.size() &&
				settings->mParts[i].mMotionType == JPH::EMotionType::Kinematic;
		if (!fixed && bodies.GetMotionType(id) != want)
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
	// Blended between the last two steps once a history exists; the solver's
	// own pose before the first step (and for a driven, live ragdoll).
	if (inst.simulated && inst.hist.size() == n) {
		const float alpha = Alpha();
		for (size_t i = 0; i < n; ++i) {
			const Impl::RagdollInst::PartHist& h = inst.hist[i];
			mats[i] = JPH::Mat44::sRotationTranslation(h.q0.SLERP(h.q1, alpha).Normalized(),
					JPH::Vec3(h.p0 + (h.p1 - h.p0) * alpha));
		}
	} else {
		inst.ragdoll->GetPose(rootOffset, mats.data());
	}
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


void PhysicsWorld::CollectDebugLines(const Vec3& around, float radius,
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

bool PhysicsWorld::RayCast(const Vec3& from, const Vec3& to, RayHit& out,
		bool staticOnly, const int* exclude,
		size_t excludeCount, const int* ignoreRagdolls,
		size_t ignoreRagdollCount, bool includePlayer) const {
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
	if (!impl_->pawnProbe.IsInvalid() && !includePlayer) bodies.IgnoreBody(impl_->pawnProbe);
	for (size_t i = 0; i < excludeCount; ++i) {
		const int slot = exclude[i];
		// A removed slot keeps an invalid id so the others stay stable, and
		// handing one of those to the filter is not survivable.
		if (slot >= 0 && size_t(slot) < impl_->scriptBodies.size() &&
				!impl_->scriptBodies[slot].body.IsInvalid())
			bodies.IgnoreBody(impl_->scriptBodies[slot].body);
	}
	// A corpse out of the solver: every one of its limbs is passed through.
	for (size_t i = 0; i < ignoreRagdollCount; ++i) {
		const int slot = ignoreRagdolls[i];
		if (!RagdollExists(slot)) continue;
		for (JPH::BodyID id : impl_->ragdolls[size_t(slot)].ragdoll->GetBodyIDs())
			bodies.IgnoreBody(id);
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
	// the moving one. Asked directly: the pair filter says static never
	// meets static, so a DefaultObjectLayerFilter on kNonMoving hit nothing.
	struct StaticLayerFilter final : JPH::ObjectLayerFilter {
		bool ShouldCollide(JPH::ObjectLayer layer) const override {
			return layer == Layers::kNonMoving;
		}
	};
	const StaticLayerFilter staticLayer;
	const SolidLayerFilter solidLayer;
	// includePlayer: the pawn's sensor lives in the probe layer, which the
	// solid filter leaves out; the AI's trace wants it in.
	struct WithPlayerLayerFilter final : JPH::ObjectLayerFilter {
		bool ShouldCollide(JPH::ObjectLayer layer) const override {
			return layer != Layers::kNoCollide;
		}
	};
	const WithPlayerLayerFilter withPlayerLayer;
	impl_->system.GetNarrowPhaseQuery().CastRay(
			ray, settings, collector, {},
			staticOnly ? static_cast<const JPH::ObjectLayerFilter&>(staticLayer)
			: (includePlayer ? static_cast<const JPH::ObjectLayerFilter&>(withPlayerLayer)
			: solidLayer),
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
			// Which .mpk object, off the triangle's user data (BuildStaticWorld).
			if (body.GetID() == impl_->worldBody &&
					body.GetShape()->GetSubType() == JPH::EShapeSubType::Mesh) {
				const JPH::MeshShape* mesh = static_cast<const JPH::MeshShape*>(body.GetShape());
				out.worldObject = int(mesh->GetTriangleUserData(collector.mHit.mSubShapeID2));
			}
		}
	}

	// Never hand a degenerate normal back to the scripts. A grazing or
	// zero-length contact can leave it unnormalisable, and one NaN here
	// spreads through every effect position, sound and decal the hit
	// spawns. Facing back down the ray is the honest fallback.
	const float n2 = out.normal[0] * out.normal[0] + out.normal[1] * out.normal[1] +
			out.normal[2] * out.normal[2];
	if (!(n2 > 1e-8f)) { // false for NaN as well as for zero
		for (int c = 0; c < 3; ++c) out.normal[c] = -span[c] / length;
	}

	// The player's own sensor body, when the cast was allowed to see it.
	if (includePlayer && !impl_->pawnProbe.IsInvalid() &&
			collector.mHit.mBodyID == impl_->pawnProbe) {
		out.player = true;
		return true;
	}

	// Which script body, if any. Anything that is not one is the world, and
	// -1 is what makes ENTITY.IsFixedMesh answer true.
	for (size_t i = 0; i < impl_->scriptBodies.size(); ++i) {
		if (impl_->scriptBodies[i].body == collector.mHit.mBodyID) {
			out.bodySlot = int(i);
			return true;
		}
	}

	// A RAGDOLL LIMB IS NOT THE WORLD: a corpse is made of these and none is a
	// script body, so the loop above falls through. The caller turns the pair
	// into the owning entity and a limb handle.
	for (size_t r = 0; r < impl_->ragdolls.size(); ++r) {
		const JPH::Ragdoll* rd = impl_->ragdolls[r].ragdoll;
		if (rd == nullptr) continue;
		for (size_t b = 0; b < rd->GetBodyCount(); ++b) {
			if (rd->GetBodyID(int(b)) == collector.mHit.mBodyID) {
				out.ragdollSlot = int(r);
				out.ragdollPart = int(b);
				return true;
			}
		}
	}
	return true;
}

bool PhysicsWorld::SphereOverlaps(const Vec3& pos, float radius) const {
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

int PhysicsWorld::Depenetrate(Vec3& pos, float radius, int iterations,
		bool solidProps, int ignoreSlot,
		bool collideWithPlayer,
		bool* separatedFromCharacter) const {
	if (!loaded()) return 0;

	const JPH::BodyID self = ScriptBodyExists(ignoreSlot)
			? impl_->scriptBodies[ignoreSlot].body
			: JPH::BodyID();
	// radius <= 0 means the player's four-sphere stack (DepenetratePlayer).
	const JPH::SphereShape sphere(radius > 0.f ? radius : 1.f);
	sphere.SetEmbedded();
	if (radius <= 0.f && !impl_->playerShape) impl_->playerShape = Impl::MakePlayerShape();
	const JPH::Shape* shape = radius > 0.f ? static_cast<const JPH::Shape*>(&sphere)
			: impl_->playerShape.GetPtr();
	if (shape == nullptr) return 0;
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
		// Report near-touching pairs too (a negative depth): a shape left
		// exactly touching casts as a hit at fraction 0 whichever way it
		// goes - a sphere settled onto a ledge's corner could never leave it
		// - so anything nearer than kGap is pushed out to kGap.
		constexpr float kGap = 0.01f;
		settings.mMaxSeparationDistance = kGap;

		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		// CollideShape wants the centre-of-mass transform; `pos` is the origin.
		impl_->system.GetNarrowPhaseQuery().CollideShape(
				shape, JPH::Vec3::sOne(),
				JPH::RMat44::sTranslation(JPH::RVec3(pos[0], pos[1], pos[2]) +
				JPH::RVec3(shape->GetCenterOfMass())),
				settings, JPH::RVec3::sZero(), collector, {}, kSweepLayer, blockers);
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
		if (deepest->mPenetrationDepth <= -kGap) break;

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

		// Out to the gap, not merely out of the overlap (see kGap above).
		if (!character) depth += kGap;
		for (int c = 0; c < 3; ++c) pos[c] += out[c] * depth;
		++resolved;
		// A character push is rate-limited, so re-running the loop would just
		// spend the budget several times over in one call.
		if (character) break;
	}
	return resolved;
}

void PhysicsWorld::SlideSphere(Vec3& pos, const Vec3& delta, float radius,
		bool solidProps, int ignoreSlot,
		bool collideWithPlayer,
		bool* separatedFromCharacter, float* hitNormal,
		int iterations) const {
	const JPH::BodyID self = ScriptBodyExists(ignoreSlot)
			? impl_->scriptBodies[ignoreSlot].body
			: JPH::BodyID();
	if (hitNormal) hitNormal[0] = hitNormal[1] = hitNormal[2] = 0.f;
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

	// radius <= 0 means the player's four-sphere stack (SlidePlayer).
	const JPH::SphereShape sphere(radius > 0.f ? radius : 1.f);
	sphere.SetEmbedded();
	if (radius <= 0.f && !impl_->playerShape) impl_->playerShape = Impl::MakePlayerShape();
	const JPH::Shape* shape = radius > 0.f ? static_cast<const JPH::Shape*>(&sphere)
			: impl_->playerShape.GetPtr();
	if (shape == nullptr) return;
	const CameraBlockerFilter blockers(impl_->probe, solidProps ? kSolidProps : maxPushMass_,
			self,
			collideWithPlayer ? JPH::BodyID() : impl_->pawnProbe);

	// Keeps the sphere just clear of the surface so the next cast starts
	// outside it. The original calls the same idea PhantomTolerance.
	constexpr float kSkin = 0.02f;

	// Three planes is enough for a corner; anything past that is a crack, and
	// stopping there is better than squeezing through the world.
	for (int iteration = 0; iteration < iterations; ++iteration) {
		if (remaining.IsNearZero()) break;

		// `at` is the shape's ORIGIN. A ShapeCast wants the centre of mass,
		// and the four-sphere stack's sits 0.059 above its origin - passing
		// the origin as the COM sank the stack by that much and left the
		// player resting 0.06 high on every floor.
		const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
				shape, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(JPH::RVec3(at)), remaining);
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
		// The contact normal points from the surface toward the sphere.
		const JPH::Vec3 normal = -collector.mHit.mPenetrationAxis.Normalized();
		// Back off along the travel direction rather than along the normal:
		// the normal can be nearly perpendicular to the motion on a grazing
		// hit, where backing off would barely help. (Scaling the back-off by
		// the angle instead ate a whole diagonal move along a wall.) What a
		// diagonal hit leaves touching, Depenetrate's gap takes care of.
		const float length = travelled.Length();
		if (length > kSkin) at += travelled * ((length - kSkin) / length);
		if (hitNormal && iteration == 0) {
			hitNormal[0] = normal.GetX();
			hitNormal[1] = normal.GetY();
			hitNormal[2] = normal.GetZ();
		}
		remaining -= travelled;
		remaining -= normal * remaining.Dot(normal);
	}

	for (int c = 0; c < 3; ++c) pos[c] = at[c];
}

} // namespace painful
