#pragma once
#include <string>
#include <vector>

#include "../Core/Common.h"

namespace painful {

struct Model;

// One limb of a ragdoll, as the .rde names it.
struct RagdollLimb {
    std::string bone;           // the section name IS the bone name
    float mass = -1.f;
    float linearDamping = 0.f;
    float angularDamping = 0.f;
    float friction = 0.f;
    float restitution = 0.f;
};

// PainEngine .rde - the ragdoll definition shipped beside a model.
//
// Plain INI: one section per limb, keyed by BONE NAME, and every one of the 220
// shipped files uses exactly five float keys and no others. A ragdoll is a
// COARSE skeleton - evilmonkv2 names 17 of its 63 bones, and the average across
// all 220 is 9.4 - so this is spine, head and limbs, not fingers.
//
// There is NO SHAPE DATA HERE, only mass and material. The limb shapes have to
// be derived from the model, which is what BuildLimbBounds does.
struct Ragdoll {
    std::vector<RagdollLimb> limbs;
    std::string error;

    const RagdollLimb* Find(const std::string& bone) const;
    static bool Load(const std::string& path, Ragdoll& out);
};

// A limb's extent in ITS OWN BONE's space, derived from the vertices that bone
// drives.
//
// Bone space rather than model space on purpose: posing it then costs nothing,
// because the skinning matrices that place it are already computed every frame
// for the draw. A box built in model space would have to be rebuilt per pose.
struct LimbBounds {
    int bone = -1;
    std::string name;
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    size_t vertices = 0;        // how many the bone actually drives

    float extent(int axis) const { return max[axis] - min[axis]; }
    bool valid() const { return vertices > 0; }
};

// One box per limb the ragdoll names, from the vertices weighted to that bone.
//
// A vertex counts towards the bone that influences it MOST. Splitting it across
// every influence would smear each box over its neighbours - the whole point of
// per-limb shapes is that an arm is not the chest.
std::vector<LimbBounds> BuildLimbBounds(const Model& model, const Ragdoll& ragdoll);

} // namespace painful
