#pragma once
#include <string>
#include "../Core/Vectors.h"
#include "../Core/Matrix.h"
#include <vector>


namespace painful {

struct Model;
struct Hke;

// One limb of a ragdoll, as the .rde names it.
struct RagdollLimb {
	std::string bone; // the section name IS the bone name
	float mass = -1.f;
	float linearDamping = 0.f;
	float angularDamping = 0.f;
	float friction = 0.f;
	float restitution = 0.f;
};

// PainEngine .rde - the ragdoll definition shipped beside a model.
//
// Plain INI: one section per limb, keyed by bone name, and every one of the 220
// shipped files uses exactly five float keys and no others. A ragdoll is a
// coarse skeleton - evilmonkv2 names 17 of its 63 bones, and the average across
// all 220 is 9.4 - so this is spine, head and limbs, not fingers.
//
// No shape data here, only mass and material overrides: the shapes are the
// .hke's hulls (BuildLimbHulls). Docs/Reference/Hitboxes.md, "The shapes"
struct Ragdoll {
	std::vector<RagdollLimb> limbs;
	std::string error;

	const RagdollLimb* Find(const std::string& bone) const;
	static bool Load(const std::string& path, Ragdoll& out);
};

// One limb's shape, held relative to its own bone so the posed bone matrices
// place it for nothing. min/max bound it in the shape's frame; `frame` takes
// that frame to bone space (identity for a skin-derived box).
struct LimbBounds {
	int bone = -1;
	std::string name;
	Vec3 min;
	Vec3 max;
	size_t vertices = 0; // how many points made the shape
	Mat4 frame;
	// The .hke hull in the shape's frame; empty for a skin-derived box.
	std::vector<float> hullVerts; // xyz triples
	std::vector<uint32_t> hullTris; // index triples

	float extent(int axis) const { return max[axis] - min[axis]; }
	bool valid() const { return vertices > 0; }
};

// One box per limb the ragdoll names, from the vertices weighted to that bone.
//
// A vertex counts towards the bone that influences it most. Splitting it across
// every influence would smear each box over its neighbours - the whole point of
// per-limb shapes is that an arm is not the chest.
// The fallback, for a model with no usable .hke.
std::vector<LimbBounds> BuildLimbBounds(const Model& model, const Ragdoll& ragdoll);

// One hull per .hke rigid body that names a bone of the model, as the original
// builds them (FUN_101BC620): the body's own geometry, never the skin.
std::vector<LimbBounds> BuildLimbHulls(const Model& model, const Hke& def);

} // namespace painful
