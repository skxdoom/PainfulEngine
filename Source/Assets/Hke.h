#pragma once
#include <string>
#include <vector>

#include "../Core/Common.h"

namespace painful {

// PainEngine .hke - the RAGDOLL ITSELF, and the file nobody knew shipped.
//
// 324 of them sit LOOSE in Data/Models, in no .pak, which is why extracting the
// archives never produced one. Ragdoll::Init (Engine.dll 0x1019cca0) is a thin
// wrapper; the real constructor (FUN_101c02f0) opens exactly these, checks a
// magic and puts up "Critical error: error in ragdoll hke!!!" when it fails.
//
// It is a Havok world export, and it carries everything the .rde does not: a
// CONVEX HULL per limb, the mass and material, and - the part that was holding
// the whole system up - the JOINT LIMITS, as hkRagdollConstraint (cone/twist/
// plane) and hkHingeConstraint (min/max angle).
//
// That reframes the .rde as an OVERRIDE file: `Mass = -1.0` on all 2076 limbs
// in all 220 files means "take the mass from here".
//
// TWO ENCODINGS. The first byte is 'A' for the text form (192 files) and 'B'
// for a binary one (132, including templar and vamp_v2). Only the text form is
// read here; a 'B' file reports so rather than being guessed at.
//
// UNITS ARE THE MODEL'S OWN, ten times the world's. The file declares
// GRAVITY 0 -98.099998 0 against Tweak.lua's 19.62, matching the *0.1 rule
// models are already built with. Hull vertices, translations and hinge
// positions all live in that space.

// One convex hull, named and referenced by a primitive rather than by any rule
// that could be derived from the bone: the bone is `k_szyja` and its hull is
// `k_szyjShape`, `root` gives `rooShape`, and two of evilmonk's are called
// `pCubeShape1` and `pCubeShape2`. Resolve by the reference, never by name.
struct HkeGeometry {
    std::string name;
    std::vector<float> verts;       // xyz triples
    std::vector<uint32_t> tris;     // index triples

    size_t vertexCount() const { return verts.size() / 3; }
    size_t triangleCount() const { return tris.size() / 3; }
};

// One limb: a rigid body named by its BONE, with the hull carried by its
// primitive.
struct HkeBody {
    std::string bone;               // BEGIN_RIGID_BODY <name> - the bone name
    float elasticity = 0.f;         // ELLASTICITY (sic), the restitution
    float staticFriction = 1.f;
    float dynamicFriction = 1.f;
    // ANGLE-AXIS, not a quaternion: `0.349955 1 0 0` is 20 degrees about +X,
    // and a primitive's identity is `0 0 0 0`, which is no rotation about no
    // axis. Read as a quaternion the first is not normalised and the second is
    // not a rotation at all.
    float rotAngle = 0.f;
    float rotAxis[3] = {0, 0, 0};
    float translation[3] = {0, 0, 0};
    float displacement[3] = {0, 0, 0};
    bool active = true;
    bool collisionsDisabled = false;

    // ...and its primitive, which is where the hull and the mass live.
    float mass = 0.f;
    int collisionMask = 0;
    std::string geometry;           // GEOMETRY <name> -> HkeGeometry::name
    bool convex = true;
    float primRotAngle = 0.f;
    float primRotAxis[3] = {0, 0, 0};
    float primTranslation[3] = {0, 0, 0};
};

// One constraint. The two kinds are Havok's own and each maps onto a Jolt
// constraint almost one-to-one - Ragdoll onto SwingTwistConstraint, Hinge onto
// HingeConstraint. Angles are RADIANS.
struct HkeConstraint {
    // Three kinds, not two. StiffSpring holds two bodies a fixed distance
    // apart and carries no limits at all.
    enum Kind { kHinge, kRagdoll, kStiffSpring };
    Kind kind = kRagdoll;
    std::string name;
    // Hinge names its bodies A/B; Ragdoll names them REFERENCE/ATTACHED. Both
    // land here, reference first.
    std::string bodyA, bodyB;
    bool twoBodied = true;
    bool breakable = false;         // IS_BREAKABLE - the breakables system
    float strength = 1.f;           // and what it takes to break
    float tau = 0.1f;

    // --- Hinge ---
    bool limited = false;           // IS_LIMITED
    float hingePosA[3] = {0, 0, 0}, hingePosB[3] = {0, 0, 0};
    float hingeDirA[3] = {0, 0, 0}, hingeDirB[3] = {0, 0, 0};
    float hingePerpA[3] = {0, 0, 0}, hingePerpB[3] = {0, 0, 0};
    float limitMinAngle = 0.f, limitMaxAngle = 0.f, limitFriction = 0.f;

    // --- Ragdoll (cone-twist) ---
    // Constraint space -> reference / attached body, as four columns: a 3x3
    // basis in COL0..2 and the origin in COL3.
    float csToRef[4][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
    float csToAtt[4][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
    float twistMin = 0.f, twistMax = 0.f;
    float coneMin = 0.f, coneMax = 0.f;
    float planeMin = 0.f, planeMax = 0.f;

    // --- the WORLD-SPACE form of both ---
    //
    // A constraint states its frame one of two ways and the files use both.
    // The body-local form is the matrices and HINGE_*_IN_A/B above; the world
    // form is a pivot and axes in the ragdoll's own space, which is the pose
    // the file was authored in. raven uses it, Alastor the other. Whichever
    // arrived is the one to build from, so record which.
    bool worldSpace = false;
    float worldPivot[3] = {0, 0, 0};    // Ragdoll: WORLD_PIVOT_POINT
    float twistAxis[3] = {0, 0, 0};
    float planeAxis[3] = {0, 0, 0};
    float worldHingePos[3] = {0, 0, 0}; // Hinge: WORLD_HINGE_POS / _DIR
    float worldHingeDir[3] = {0, 0, 0};

    // --- StiffSpring ---
    float localPointA[3] = {0, 0, 0}, localPointB[3] = {0, 0, 0};
    float springLength = 0.f;
    float linearStrength = 0.f, angularStrength = 0.f;
};

// BEGIN_ACTION Spring - a soft spring between two limbs, in 17 of the files.
// Not a constraint: it pulls towards a rest length rather than holding a joint.
struct HkeSpring {
    std::string bodyA, bodyB;
    float pointA[3] = {0, 0, 0}, pointB[3] = {0, 0, 0};
    bool twoBodied = true;
    float restitution = 0.f;
    float restLength = 0.f;
    float damping = 0.f;
    bool onCompression = true, onExtension = true;
};

struct Hke {
    int version = 0;
    float worldScale = 1.f;
    float gravity[3] = {0, 0, 0};
    float linearDrag = 0.f, angularDrag = 0.f;
    float deactivationThreshold = 0.f;
    std::vector<HkeGeometry> geometries;
    std::vector<HkeBody> bodies;
    std::vector<HkeConstraint> constraints;
    std::vector<HkeSpring> springs;

    bool binary = false;            // a 'B' file: recognised, not parsed
    std::string error;
    // Keywords the parser did not know. Empty across the shipped set is the
    // check that the format is fully covered rather than merely accepted.
    std::vector<std::string> unknown;

    const HkeGeometry* Find(const std::string& geometry) const;
    const HkeBody* Body(const std::string& bone) const;

    // Is `bone` connected to `root` through the constraint graph?
    //
    // THIS IS THE WEAPON RULE. A monster's weapon gets a rigid body with NO
    // constraint attaching it to anything - evilmonkv2's unconstrained bodies
    // are exactly axeL and axeR, zombie's is joint1 - so it is a limb you can
    // hit that is not part of the body. Ragdoll::Joint_AreLinked answers this,
    // and Stake, BoltStick and PainHead all ask it before doing damage, to
    // tell "the body" from "some detachable element, e.g. a scythe or a
    // pauldron". A shield is the opposite case and IS constrained.
    bool Linked(const std::string& a, const std::string& b) const;

    static bool Load(const std::string& path, Hke& out);
};

} // namespace painful
