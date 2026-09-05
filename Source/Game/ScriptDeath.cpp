// ScriptEngine: dying - MDL.EnableRagdoll, the ragdoll pose and what it drops.

#include "ScriptEngineInternal.h"

#include <set>
#include <utility>

namespace painful {

namespace {

// What EffectRotateActor does with the spin ScriptEntity accumulated: below
// kSpinKick it has a one-in-eight chance of being multiplied UP to it, and it
// is then clamped to +/-50 rad/s (0x42480000 / 0xc2480000, against
// _DAT_102af16c = 10).
constexpr float kSpinKick = 10.f;
constexpr float kSpinClamp = 50.f;

// A bone matrix composed with the entity transform is not a rotation, and a
// rigid body needs one. Two things are wrong with it: it carries the entity's
// SCALE, and a rig with mirrored left/right bones carries a NEGATIVE
// DETERMINANT with it. Normalising the rows fixes the first and leaves the
// second - a left-handed frame, from which Jolt reads a quaternion that is not
// a rotation at all, and the solver answers by throwing the corpse across the
// level. Measured on evilmonkv2, whose r_l_* and r_p_* bones are mirrors:
// the ragdoll flew to y=11 while its own head lay on the floor.
//
// So build a proper right-handed basis instead: normalise the first row, make
// the second perpendicular to it, and take the third as their cross product.
// Positions are already in world units and are left alone.
void MakeRigid(Mat4& m) {
    float x[3] = {m.m[0], m.m[1], m.m[2]};
    float y[3] = {m.m[4], m.m[5], m.m[6]};

    const auto norm = [](float v[3]) {
        const float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        if (len < 1e-8f) return false;
        for (int c = 0; c < 3; ++c) v[c] /= len;
        return true;
    };
    if (!norm(x)) { x[0] = 1.f; x[1] = x[2] = 0.f; }
    const float d = x[0]*y[0] + x[1]*y[1] + x[2]*y[2];
    for (int c = 0; c < 3; ++c) y[c] -= d * x[c];
    if (!norm(y)) {
        // The first two rows were parallel; any perpendicular will do.
        const float alt[3] = {0.f, 1.f, 0.f};
        const float d2 = x[1];
        for (int c = 0; c < 3; ++c) y[c] = alt[c] - d2 * x[c];
        if (!norm(y)) { y[0] = 0.f; y[1] = 0.f; y[2] = 1.f; }
    }
    const float z[3] = {x[1]*y[2] - x[2]*y[1],
                        x[2]*y[0] - x[0]*y[2],
                        x[0]*y[1] - x[1]*y[0]};
    for (int c = 0; c < 3; ++c) {
        m.m[0 + c] = x[c];
        m.m[4 + c] = y[c];
        m.m[8 + c] = z[c];
    }
    m.m[3] = m.m[7] = m.m[11] = 0.f;
    m.m[15] = 1.f;
}

} // namespace

// ---------------------------------------------------------------- death
//
// MDL.EnableRagdoll(e, on, collisionGroup) is what dying IS. CActor:OnDamage
// calls EnableRagdoll(true, true) the moment health reaches zero, having first
// stopped the actor and disabled its physics object, and from then on the
// solver owns the pose: CActor sets _CurAnimLength to 99999 so the animation
// clock stops, because there is no longer an animation to run.
//
// The engine's shape is Ragdoll::Activate(bone matrices, group) - the SAME
// matrices the renderer poses with, handed to the simulation as a starting
// state. So the corpse begins exactly where the monster was standing, in the
// exact frame of whatever it was doing, and falls from there.
// Where each ragdoll body sits RELATIVE TO THE BONE that drives it, once per
// model.
//
// A limb body is at the limb's centre and the bone is at its end, so the two
// are a limb-length apart. Posing bodies straight onto bone matrices puts
// every constraint anchor that far from where it belongs, and the solver's
// answer is to throw the corpse across the level - measured on evilmonkv2 and
// zombie, both of which flew to y > 15 while their heads lay on the floor. The
// nun happened to survive it, which is exactly the kind of near-miss that
// makes this look like a per-model problem rather than a missing transform.
//
// Off = Rest * inverse(Bind), both in MODEL units, so a posed bone matrix M
// puts its body at Off * M.
const std::vector<Mat4>& ScriptEngine::RagdollOffsets(const std::string& model,
                                                      const std::vector<std::string>& parts,
                                                      const Hke& def,
                                                      const SkeletonCache::Entry& skel) {
    auto it = ragdollOffsets_.find(model);
    if (it != ragdollOffsets_.end() && it->second.size() == parts.size()) return it->second;

    std::vector<Mat4>& out = ragdollOffsets_[model];
    out.assign(parts.size(), Mat4());
    for (size_t p = 0; p < parts.size(); ++p) {
        const HkeBody* body = def.Body(parts[p]);
        if (body == nullptr) continue;
        int bone = -1;
        for (size_t b = 0; b < skel.bones.size(); ++b)
            if (skel.bones[b].name == parts[p]) { bone = int(b); break; }
        if (bone < 0) continue;
        Mat4 rest;
        body->RestMatrix(rest.m);
        out[p] = Mat4::Mul(rest, Mat4::InvertAffine(skel.bindWorld[size_t(bone)]));
    }

    // WHICH LIMBS ARE NOT PART OF THE BODY - the weapons.
    //
    // The .hke gives a monster's weapon a rigid body with no constraint
    // attaching it to anything: evilmonkv2's axeL and axeR, zombie's joint1,
    // 213 such bodies across 99 models. On activation they are free bodies and
    // they fall away, which is the original dropping whatever the monster was
    // carrying. So they must NOT be re-anchored to the parent chain the way a
    // real limb is: an axe that has left the hand has no bone length left to
    // preserve, and pinning it to the wrist welds the mesh back on while the
    // collision shape sails off on its own.
    std::vector<char>& freeParts = ragdollFree_[model];
    freeParts.assign(parts.size(), 0);
    std::string root;
    for (const std::string& name : parts)
        if (name == "root" || name == "ROOOT") { root = name; break; }
    if (root.empty() && !parts.empty()) root = parts.front();
    for (size_t p = 0; p < parts.size(); ++p)
        freeParts[p] = def.Linked(parts[p], root) ? 0 : 1;
    return out;
}

bool ScriptEngine::EnableRagdoll(Entity& e, bool enable, const std::vector<Mat4>* seedPose) {
    if (!physics_) return false;

    if (!enable) {
        if (e.ragdollSlot < 0) return false;
        physics_->RemoveRagdoll(e.ragdollSlot);
        e.ragdollSlot = -1;
        e.ragdollPose.clear();
        return true;
    }
    if (e.ragdollSlot >= 0) return true;            // already one

    const Hke* def = RagdollDef(e.source);
    if (def == nullptr) return false;
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (skel == nullptr) return false;
    const std::vector<Mat4>* bones =
        (seedPose && seedPose->size() == skel->bones.size()) ? seedPose : PosedBones(e);
    if (bones == nullptr) return false;

    const int slot = physics_->CreateRagdoll(e.source, *def, e.scale);
    if (slot < 0) return false;

    // Seed it from the pose the actor died in. A part whose bone the rig does
    // not have keeps its authored place rather than collapsing to the origin -
    // the .hke sweep says that never happens, but a ragdoll that silently
    // folds into a point is not a failure worth discovering in a screenshot.
    const std::vector<std::string>& parts = physics_->RagdollBones(slot);
    const std::vector<Mat4>& offsets = RagdollOffsets(e.source, parts, *def, *skel);
    std::vector<float> pose(parts.size() * 16, 0.f);
    float rot[9];
    EngineQuatToRot9(e.rotWXYZ, rot);
    Mat4 world;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) world.m[r * 4 + c] = e.scale * rot[r * 3 + c];
        world.m[r * 4 + 3] = 0.f;
    }
    for (int c = 0; c < 3; ++c) world.m[12 + c] = e.pos[c];
    world.m[15] = 1.f;

    for (size_t p = 0; p < parts.size(); ++p) {
        Mat4 m;                                     // identity
        for (size_t b = 0; b < skel->bones.size(); ++b)
            if (skel->bones[b].name == parts[p]) {
                m = Mat4::Mul(Mat4::Mul(offsets[p], (*bones)[b]), world);
                MakeRigid(m);
                break;
            }
        for (int i = 0; i < 16; ++i) pose[p * 16 + i] = m.m[i];
    }
    physics_->SetRagdollPose(slot, pose.data(), /*kinematic=*/false);
    if (e.ragdollLinearDamping >= 0.f || e.ragdollAngularDamping >= 0.f)
        physics_->SetRagdollDamping(slot, e.ragdollLinearDamping, e.ragdollAngularDamping);

    // Does the solver hold what it was handed? Straight back out again, with
    // no step in between, so a seed that does not round-trip is separated from
    // a simulation that drifts.
    if (getenv("PAINFUL_RAGDOLL_DEBUG")) {
        std::vector<float> back(parts.size() * 16, 0.f);
        if (physics_->GetRagdollPose(slot, back.data())) {
            float worst = 0.f;
            std::string worstName;
            for (size_t p = 0; p < parts.size(); ++p) {
                float d2 = 0.f;
                for (int c = 0; c < 3; ++c) {
                    const float k = back[p * 16 + 12 + c] - pose[p * 16 + 12 + c];
                    d2 += k * k;
                }
                if (std::sqrt(d2) > worst) { worst = std::sqrt(d2); worstName = parts[p]; }
            }
            LogInfo("rdseed %s round-trip worst %.4f (%s)", e.source.c_str(), worst,
                    worstName.c_str());

            // ARE THE CONSTRAINTS ALREADY TORN? Each one joins an anchor in one
            // body to an anchor in the other, and the two coincide only if the
            // bodies are in the relative configuration the file was authored
            // in. Placing them from an ANIMATION pose does not guarantee that -
            // and every unit of gap here is error the solver has to eat on the
            // first step, which is what a corpse snapping instead of slumping
            // looks like.
            const auto partIndex = [&](const std::string& name) {
                for (size_t i = 0; i < parts.size(); ++i)
                    if (parts[i] == name) return int(i);
                return -1;
            };
            const auto anchorWorld = [&](int p, const float local[3], float out[3]) {
                const float* m = &pose[size_t(p) * 16];
                for (int c = 0; c < 3; ++c)
                    out[c] = m[12 + c] + e.scale * (local[0] * m[0 + c] + local[1] * m[4 + c] +
                                                    local[2] * m[8 + c]);
            };
            float worstGap = 0.f;
            std::string worstPair;
            for (const HkeConstraint& c : def->constraints) {
                if (c.worldSpace) continue;         // stated in world terms, no pair to compare
                if (c.kind == HkeConstraint::kStiffSpring) continue;   // holds a distance, not a point
                const int pa = partIndex(c.bodyA), pb = partIndex(c.bodyB);
                if (pa < 0 || pb < 0) continue;
                const float* la = (c.kind == HkeConstraint::kHinge) ? c.hingePosA : c.csToRef[3];
                const float* lb = (c.kind == HkeConstraint::kHinge) ? c.hingePosB : c.csToAtt[3];
                float wa[3], wb[3];
                anchorWorld(pa, la, wa);
                anchorWorld(pb, lb, wb);
                float d2 = 0.f;
                for (int k = 0; k < 3; ++k) d2 += (wa[k] - wb[k]) * (wa[k] - wb[k]);
                if (std::sqrt(d2) > worstGap) {
                    worstGap = std::sqrt(d2);
                    worstPair = c.bodyA + "->" + c.bodyB;
                }
            }
            LogInfo("rdseed %s worst anchor gap at activation %.3f (%s)", e.source.c_str(),
                    worstGap, worstPair.c_str());
        }
    }
    e.ragdollSlot = slot;

    // SPEND WHAT THE KILLING SHOT PUT ON IT. Ragdoll::Activate does this first
    // thing, by calling PhysicsObject::EffectRotateActor - the spin the pellets
    // accumulated while the monster was still alive becomes the corpse's
    // angular velocity the instant it becomes a corpse. Without it a body that
    // was shot apart at point blank slumps exactly like one that died of old
    // age, which is the whole difference the impact makes.
    //
    // EffectRotateActor's rule, verbatim: under 10 rad/s there is a one-in-
    // eight chance of being multiplied up to it, and the result is clamped to
    // +/-50.
    float spin = e.deathSpin;
    if (std::fabs(spin) < kSpinKick && (std::rand() & 7) == 0) spin *= kSpinKick;
    spin = std::max(-kSpinClamp, std::min(kSpinClamp, spin));
    if (spin != 0.f) physics_->SetRagdollSpin(slot, spin);
    // ...and the linear half, from the PO_Hit that arrived before there was a
    // ragdoll to give it to.
    if (e.hasDeathImpulse)
        physics_->AddRagdollImpulse(slot, e.deathImpulseAt, e.deathImpulse);
    e.deathSpin = 0.f;
    e.hasDeathImpulse = false;

    // The pose it died in, until the solver's first read-back replaces it -
    // so a joint query in the same tick as the death (CreateGib asks for
    // "root" straight away) answers with the corpse, not with identity.
    e.ragdollPose = *bones;
    LogInfo("ragdoll on: %s (%s, %zu parts)", e.name.c_str(), e.source.c_str(), parts.size());
    return true;
}

// Read the solver back into the pose the renderer draws, once per frame.
//
// THE RAGDOLL ONLY NAMES A DOZEN BONES OF SIXTY. Everything it does not drive
// - hands, hair, the fingers - has to follow its nearest driven ancestor, or
// the corpse keeps its arms and loses everything hanging off them. Walking the
// hierarchy in order and composing each undriven bone from its BIND-LOCAL
// transform against its parent's new matrix does that: the undriven parts stay
// rigidly attached exactly as they were posed.
void ScriptEngine::TickRagdolls() {
    if (!physics_) return;
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (e.ragdollSlot < 0) continue;
        const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
        if (skel == nullptr) { e.ragdollSlot = -1; continue; }

        const std::vector<std::string>& parts = physics_->RagdollBones(e.ragdollSlot);
        const Hke* def = RagdollDef(e.source);
        if (def == nullptr) { e.ragdollSlot = -1; continue; }
        const std::vector<Mat4>& offsets = RagdollOffsets(e.source, parts, *def, *skel);
        std::vector<float> got(parts.size() * 16, 0.f);
        if (!physics_->GetRagdollPose(e.ragdollSlot, got.data())) continue;

        // PAINFUL_RAGDOLL_DEBUG: the part positions in WORLD space, straight
        // out of the solver. Everything the scripts can see goes through the
        // entity transform, so a ragdoll falling rigidly and a ragdoll whose
        // pose is not being updated at all look identical from Lua.
        static const bool kDebug = getenv("PAINFUL_RAGDOLL_DEBUG") != nullptr;
        if (kDebug) {
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (size_t p = 0; p < parts.size(); ++p)
                for (int c = 0; c < 3; ++c) {
                    const float v = got[p * 16 + 12 + c];
                    lo[c] = std::min(lo[c], v);
                    hi[c] = std::max(hi[c], v);
                }
            LogInfo("rdbg %s parts=%zu world extent %.2f x %.2f x %.2f  lo.y %.2f  p0 %.2f %.2f %.2f",
                    e.name.c_str(), parts.size(), hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2],
                    lo[1], got[12], got[13], got[14]);
        }

        // THE ENTITY FOLLOWS ITS OWN CORPSE. The solver moves the body across
        // the floor; leaving the entity where it died would leave every
        // distance test, sound and effect anchored to a spot the corpse is no
        // longer at, and would cull it from the wrong place.
        int rootPart = -1;
        for (size_t p = 0; p < parts.size(); ++p)
            if (parts[p] == "root" || parts[p] == "ROOOT") { rootPart = int(p); break; }
        if (rootPart < 0 && !parts.empty()) rootPart = 0;
        if (rootPart >= 0)
            for (int c = 0; c < 3; ++c) e.pos[c] = got[size_t(rootPart) * 16 + 12 + c];

        // Model space is what the renderer wants, so undo the entity's own
        // transform - rebuilt here from the position just updated.
        float rot[9];
        EngineQuatToRot9(e.rotWXYZ, rot);
        Mat4 world;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) world.m[r * 4 + c] = e.scale * rot[r * 3 + c];
            world.m[r * 4 + 3] = 0.f;
        }
        for (int c = 0; c < 3; ++c) world.m[12 + c] = e.pos[c];
        world.m[15] = 1.f;
        const Mat4 toModel = Mat4::InvertAffine(world);

        if (e.ragdollPose.size() != skel->bones.size())
            e.ragdollPose.assign(skel->bones.size(), Mat4());

        std::vector<bool> driven(skel->bones.size(), false);
        // Bones whose body is not held by any constraint - see ragdollFree_.
        std::vector<bool> loose(skel->bones.size(), false);
        const std::vector<char>& freeParts = ragdollFree_[e.source];
        for (size_t p = 0; p < parts.size(); ++p)
            for (size_t b = 0; b < skel->bones.size(); ++b)
                if (skel->bones[b].name == parts[p]) {
                    Mat4 w;
                    for (int i = 0; i < 16; ++i) w.m[i] = got[p * 16 + i];
                    // MAKE IT RIGID BEFORE UNDOING THE OFFSET, not after.
                    //
                    // E carries the entity's scale, so W * E^-1 comes back
                    // with a basis 1/scale long - about 8x. Composing Off^-1
                    // onto that multiplies ITS translation by the same 8x, and
                    // the offset is a limb length, so every bone lands metres
                    // from its own body: measured, the root bone 1.3 below its
                    // pelvis and the hips 2.5 ABOVE the neck.
                    Mat4 partModel = Mat4::Mul(w, toModel);
                    MakeRigid(partModel);
                    e.ragdollPose[b] = Mat4::Mul(Mat4::InvertAffine(offsets[p]), partModel);
                    MakeRigid(e.ragdollPose[b]);
                    driven[b] = true;
                    // A limb the constraint graph does not hold is a dropped
                    // weapon, not a bone: it keeps whatever its own body did.
                    if (p < freeParts.size() && freeParts[p]) loose[b] = true;
                    break;
                }

        // Bones the ragdoll does not drive have to follow the ones it does,
        // and they can sit on EITHER SIDE of it in the hierarchy.
        //
        // Below is the obvious case - hands, fingers, hair - and they chain
        // down from their parent. ABOVE is the one that bites: the zombie's
        // ragdoll starts at k_sub_root, so its `root` bone is an ANCESTOR of
        // every driven bone and has no driven parent to inherit from. Left on
        // its bind transform it stays at standing height above an entity now
        // lying on the floor, which measured as a root bone 1.5 units above
        // its own corpse.
        //
        // So resolve upward first, from any bone to its parent, walking from
        // the leaves; then downward for everything still unresolved. A bone's
        // rest transform relative to its parent is bindWorld[b] *
        // inverse(bindWorld[parent]), and it inverts cleanly either way.
        const auto restLocal = [&](size_t b, size_t par) {
            return Mat4::Mul(skel->bindWorld[b], Mat4::InvertAffine(skel->bindWorld[par]));
        };
        for (size_t i = skel->bones.size(); i-- > 0;) {
            if (!driven[i] || loose[i]) continue;   // a dropped weapon drags nothing
            const int par = skel->bones[i].parent;
            if (par < 0 || size_t(par) >= i || driven[size_t(par)]) continue;
            e.ragdollPose[size_t(par)] =
                Mat4::Mul(Mat4::InvertAffine(restLocal(i, size_t(par))), e.ragdollPose[i]);
            driven[size_t(par)] = true;
        }
        // ONE DOWNWARD PASS FIXES EVERY POSITION FROM ITS PARENT.
        //
        // BONE LENGTHS ARE FIXED; ONLY ROTATIONS ARE NOT. Taking each bone's
        // position from its own body lets the solver's residual constraint
        // error - a few millimetres per joint, which is normal and which Jolt
        // never drives to zero - land in the skin. The mesh is weighted across
        // neighbouring bones, so two origins drifting apart stretch every
        // vertex between them and a forearm ends up visibly longer than it was
        // modelled. Measured before this: 13% on k_zebra -> k_szyja.
        //
        // It has to be EVERY bone, not just the driven ones. k_szyja's parent
        // is k_ramiona, which the ragdoll does not drive, so a pass that only
        // relates driven bones to driven parents skips exactly the joints that
        // stretch most.
        //
        // So: keep the rotation each body computed, take the position from the
        // parent chain using the bone's own bind offset, and give undriven
        // bones their whole transform from the chain. BuildHierarchy leaves
        // parents before children, so one pass in order is enough. The topmost
        // resolved bone keeps the position its body reported, and the rest of
        // the skeleton hangs off it at exactly its modelled proportions.
        for (size_t b = 0; b < skel->bones.size(); ++b) {
            // A dropped weapon keeps its own body's place. It is no longer
            // attached to anything, so there is no bone length to preserve and
            // re-anchoring it to the wrist is what welded the axe back on
            // while its collision shape sailed away on its own.
            if (loose[b]) continue;
            // A DRIVEN BONE KEEPS ITS BODY'S POSITION. This pass used to
            // override it from the parent chain to stop the skin stretching -
            // but the stretch was a bug in the hinge anchors (see
            // BuildConstraint), not something inherent. With those correct the
            // constraints hold their joints to 1-4% of a bone length, so
            // re-anchoring buys nothing and costs everything: it is what made
            // the collision hulls visibly lag the mesh they belong to.
            //
            // Undriven bones still chain, because nothing else places them.
            if (driven[b]) continue;
            const int par = skel->bones[b].parent;
            if (par < 0 || size_t(par) >= b) {
                if (!driven[b]) e.ragdollPose[b] = skel->bindWorld[b];
                continue;
            }
            const Mat4 rest = restLocal(b, size_t(par));
            const Mat4& parent = e.ragdollPose[size_t(par)];
            if (!driven[b]) e.ragdollPose[b] = Mat4::Mul(rest, parent);
            Mat4& me = e.ragdollPose[b];
            for (int c = 0; c < 3; ++c)
                me.m[12 + c] = parent.m[12 + c] +
                               rest.m[12] * parent.m[0 + c] +
                               rest.m[13] * parent.m[4 + c] +
                               rest.m[14] * parent.m[8 + c];
            driven[b] = true;
        }

        // AND PUSH IT TO THE RENDERER HERE, because nothing else will.
        //
        // TickAnimations is what normally hands a pose to the renderer, and it
        // returns early for any entity with no animation running:
        //
        //     if (e.animIndex < 0 || e.animScale <= 0.f) continue;
        //
        // A dead actor is exactly that. CActor:Stop() is the line before
        // EnableRagdoll, and CActor sets _CurAnimLength to 99999 so the clock
        // never advances again. So the bodies simulate underneath a mesh still
        // frozen in the frame it died on - which is the one thing a ragdoll
        // must not look like, and it looks like nothing is happening at all.
        // PAINFUL_RAGDOLL_DEBUG: how far each body is from the bone it drives.
        //
        // The bone-length pass takes a bone's ROTATION from its body but its
        // POSITION from the parent chain, so the two are allowed to disagree by
        // whatever the solver left unresolved. A small residual is normal and
        // is exactly what that pass exists to keep out of the skin; a large one
        // means the constraints are not actually holding and the ragdoll only
        // looks connected because the skeleton is forcing it to.
        if (kDebug) {
            float worst = 0.f, sum = 0.f;
            size_t counted = 0;
            std::string worstName;
            for (size_t p = 0; p < parts.size(); ++p) {
                int bone = -1;
                for (size_t b = 0; b < skel->bones.size(); ++b)
                    if (skel->bones[b].name == parts[p]) { bone = int(b); break; }
                if (bone < 0) continue;
                // Where the bone says the body should be: Off * M * E.
                const Mat4 want = Mat4::Mul(Mat4::Mul(offsets[p], e.ragdollPose[size_t(bone)]),
                                            world);
                float d2 = 0.f;
                for (int c = 0; c < 3; ++c) {
                    const float k = want.m[12 + c] - got[p * 16 + 12 + c];
                    d2 += k * k;
                }
                const float d = std::sqrt(d2);
                sum += d;
                ++counted;
                if (d > worst) { worst = d; worstName = parts[p]; }
            }
            if (counted)
                LogInfo("rdsync %s  mean %.3f  worst %.3f (%s)", e.name.c_str(),
                        sum / float(counted), worst, worstName.c_str());
        }

        SyncPose(e);
        if (renderer_ && e.rendererInstance >= 0) {
            BoneWorldToSkinning(skel->inverseBind, e.ragdollPose, skinScratch_);
            renderer_->SetScriptSkinning(e.rendererInstance, skinScratch_.data(),
                                         skinScratch_.size());
        }

    }
}

int ScriptEngine::L_MDL_EnableRagdoll(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    self->EnableRagdoll(*e, lua_toboolean(L, 2) != 0);
    return 0;
}

// MDL.IsRagdoll(e) - does this actor HAVE a ragdoll. CActor:EnableRagdoll
// guards both directions on it, so answering wrongly either does the work
// twice or refuses to do it at all.
int ScriptEngine::L_MDL_IsRagdoll(lua_State* L) {
    const Entity* e = From(L)->Find(HandleArg(L, 1));
    lua_pushboolean(L, e != nullptr && e->ragdollSlot >= 0);
    return 1;
}

int ScriptEngine::L_MDL_IsRagdollActive(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    lua_pushboolean(L, e != nullptr && e->ragdollSlot >= 0 && self->physics_ &&
                           self->physics_->RagdollActive(e->ragdollSlot));
    return 1;
}

int ScriptEngine::L_ENTITY_RemoveRagdoll(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (e) self->EnableRagdoll(*e, false);
    return 0;
}

// Both dampings are remembered on the entity and applied to a ragdoll made
// later: Cat_bridge1:OnCreateEntity sets them before EnableRagdoll.
int ScriptEngine::L_MDL_SetRagdollLinearDamping(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (e == nullptr) return 0;
    e->ragdollLinearDamping = float(luaL_optnumber(L, 2, 0));
    if (e->ragdollSlot >= 0 && self->physics_)
        self->physics_->SetRagdollDamping(e->ragdollSlot, e->ragdollLinearDamping, -1.f);
    return 0;
}

int ScriptEngine::L_MDL_SetRagdollAngularDamping(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (e == nullptr) return 0;
    e->ragdollAngularDamping = float(luaL_optnumber(L, 2, 0));
    if (e->ragdollSlot >= 0 && self->physics_)
        self->physics_->SetRagdollDamping(e->ragdollSlot, -1.f, e->ragdollAngularDamping);
    return 0;
}

int ScriptEngine::L_MDL_SetRagdollFriction(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (e && e->ragdollSlot >= 0 && self->physics_)
        self->physics_->SetRagdollFriction(e->ragdollSlot, float(luaL_optnumber(L, 2, 1)));
    return 0;
}

// The ragdoll definition of one model, parsed once and kept.
//
// Cached on a miss too. A model with no .hke, or with only the binary form, is
// a settled answer - retrying the open every time a stake asks about a joint
// would be a file operation per shot.
const Hke* ScriptEngine::RagdollDef(const std::string& model) {
    if (model.empty()) return nullptr;
    const auto it = ragdolls_.find(model);
    if (it != ragdolls_.end()) return it->second.bodies.empty() ? nullptr : &it->second;

    Hke& slot = ragdolls_[model];
    if (!Hke::Load(dataRoot_ + "/Models/" + model + ".hke", slot)) {
        // Say so once: a file that exists but will not parse is a different
        // thing from a model with no ragdoll.
        if (!slot.error.empty() && slot.error.find("cannot read") == std::string::npos)
            LogInfo("ragdoll: %s: %s", model.c_str(), slot.error.c_str());

        // A gib whose .hke will not parse (none shipped, now that the binary
        // form decodes): the live ragdoll cut where the gib MESH is cut - a
        // constraint survives when a gib mesh spans it. Physics.md, "Gibs".
        const size_t n = model.size();
        if (n > 4 && model.compare(n - 4, 4, "_gib") == 0) {
            const Hke* base = RagdollDef(model.substr(0, n - 4));   // may rehash
            Model gibMesh;
            if (base != nullptr && !base->bodies.empty() &&
                Model::Load(dataRoot_ + "/Models/" + model + ".pkmdl", gibMesh) &&
                !gibMesh.bones.empty()) {
                // Each bone's ragdoll part: itself, or its nearest ancestor
                // that is a body. Bones are in preorder, so parents resolve
                // first.
                std::vector<int> partOf(gibMesh.bones.size(), -1);
                for (size_t b = 0; b < gibMesh.bones.size(); ++b) {
                    for (size_t p = 0; p < base->bodies.size(); ++p)
                        if (base->bodies[p].bone == gibMesh.bones[b].name) { partOf[b] = int(p); break; }
                    if (partOf[b] < 0 && gibMesh.bones[b].parent >= 0)
                        partOf[b] = partOf[size_t(gibMesh.bones[b].parent)];
                }
                // Which pairs of parts share a mesh. A bone counts for a mesh
                // when it carries at least 1% of the mesh's weight; the
                // exporter leaves traces of 0.1% on neighbouring bones that
                // are not a cut line.
                std::set<std::pair<int, int>> joined;
                std::vector<double> share;
                for (const ModelMesh& mesh : gibMesh.meshes) {
                    if (!mesh.hasSkin()) continue;
                    share.assign(gibMesh.bones.size(), 0.0);
                    for (const std::vector<SkinInfluence>& v : mesh.skin)
                        for (const SkinInfluence& inf : v)
                            if (inf.bone < share.size()) share[inf.bone] += inf.weight;
                    std::vector<int> parts;
                    for (size_t b = 0; b < share.size(); ++b)
                        if (partOf[b] >= 0 && share[b] >= 0.01 * double(mesh.vertexCount()))
                            parts.push_back(partOf[b]);
                    for (int a : parts)
                        for (int c : parts)
                            if (a != c) joined.insert({a, c});
                }
                Hke stand = *base;
                stand.constraints.erase(
                    std::remove_if(stand.constraints.begin(), stand.constraints.end(),
                                   [&](const HkeConstraint& c) {
                                       int ia = -1, ib = -1;
                                       for (size_t p = 0; p < base->bodies.size(); ++p) {
                                           if (base->bodies[p].bone == c.bodyA) ia = int(p);
                                           if (base->bodies[p].bone == c.bodyB) ib = int(p);
                                       }
                                       return ia < 0 || ib < 0 || joined.count({ia, ib}) == 0;
                                   }),
                    stand.constraints.end());
                Hke& gibSlot = ragdolls_[model];
                gibSlot = std::move(stand);
                LogInfo("ragdoll: %s stands in for %s (no usable .hke) - %zu bodies, %zu of %zu constraints kept by the mesh cuts",
                        model.substr(0, n - 4).c_str(), model.c_str(), gibSlot.bodies.size(),
                        gibSlot.constraints.size(), base->constraints.size());
                return &gibSlot;
            }
        }
        return nullptr;
    }
    LogInfo("ragdoll: %s -> %zu bodies, %zu constraints", model.c_str(),
            slot.bodies.size(), slot.constraints.size());
    return &slot;
}

std::string ScriptEngine::JointName(Entity& e, int joint) {
    if (e.type != kModel) return std::string();
    const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
    if (!skel || joint < 0 || size_t(joint) >= skel->bones.size()) return std::string();
    return skel->bones[size_t(joint)].name;
}

// MDL.JointsLinked(e, a, b) - are these two joints connected through the
// RAGDOLL, as opposed to through the skeleton?
//
// THE SKELETON WOULD ALWAYS SAY YES. It is a tree, so every bone reaches the
// root by definition - evilmonkv2's axeL runs axeL -> dlo_lewa_root ->
// r_l_lokiec -> r_l_bark -> ... -> root. The ragdoll is a different graph: the
// .hke gives a weapon a rigid body with NO constraint attaching it to
// anything, so axeL is linked to nothing at all.
//
// That is what the question is for. Stake, BoltStick and PainHead ask it
// before doing damage and, when the answer is no, take that body out of the
// traces and shoot again - passing through "some detachable element, e.g. a
// scythe or a pauldron", in their own words.
bool ScriptEngine::JointsLinked(Entity& e, int a, int b) {
    // A joint EnableJoint has switched off is out of the ragdoll, so it is
    // linked to nothing - which is exactly what the scripts want after a
    // monster has thrown the weapon that bone carried.
    for (int d : e.disabledJoints)
        if (d == a || d == b) return false;
    if (const Hke* def = RagdollDef(e.source))
        return def->Linked(JointName(e, a), JointName(e, b));

    // NO DECODED RAGDOLL: answer as the SKELETON does, yes. False here is not
    // a neutral default but the DETACHABLE-ELEMENT answer, which made the 19
    // monsters with a binary .hke immune to stake, bolt and PainHead alike.
    // Docs/Reference/Physics.md, "STAND-IN: JointsLinked with a binary .hke"
    const auto it = ragdolls_.find(e.source);        // RagdollDef may rehash
    return a >= 0 && b >= 0 && it != ragdolls_.end() && it->second.binary;
}

int ScriptEngine::L_MDL_JointsLinked(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    const int a = int(luaL_optnumber(L, 2, -1));
    const int b = int(luaL_optnumber(L, 3, -1));
    lua_pushboolean(L, e && self->JointsLinked(*e, a, b));
    return 1;
}

// MDL.EnableJoint(e, joint, on) - take one limb out of the ragdoll, or put it
// back. Called on exactly four bones in the whole game (axeL, axeR, joint21,
// br1), always from CustomOnGib and only once the monster has already thrown
// that weapon: the mesh is hidden and the body it drove stops being part of
// the ragdoll.
int ScriptEngine::L_MDL_EnableJoint(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const int joint = int(luaL_optnumber(L, 2, -1));
    if (joint < 0) return 0;
    const bool on = lua_toboolean(L, 3) != 0;
    auto& v = e->disabledJoints;
    const auto at = std::find(v.begin(), v.end(), joint);
    if (on) { if (at != v.end()) v.erase(at); }
    else    { if (at == v.end()) v.push_back(joint); }
    return 0;
}

// PHYSICS.RemoveHavokBodyFromIS(he, on) - take ONE BODY out of the traces.
//
// The finest grain in the whole intersection-solver family: the entity pair
// switches a whole actor, the ragdoll pair switches all of its limbs, and this
// switches a single limb. The stake needs exactly that - it has just hit a
// weapon, wants to know what is BEHIND it, and cannot afford to make the rest
// of the monster invisible to do so.
//
// The argument reads backwards and does in the engine too: `true` REMOVES.
int ScriptEngine::L_PHYSICS_RemoveHavokBodyFromIS(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!lua_isnumber(L, 1)) return 0;
    const int handle = int(lua_tonumber(L, 1));
    int entity = 0, joint = -1;
    if (!self->LimbFromHandle(handle, entity, joint)) return 0;
    const bool remove = lua_toboolean(L, 2) != 0;
    auto& v = self->suppressedLimbs_;
    const auto at = std::find(v.begin(), v.end(), handle);
    if (remove) { if (at == v.end()) v.push_back(handle); }
    else        { if (at != v.end()) v.erase(at); }
    return 0;
}

// PHYSICS.IsHavokBodyInWorld(body) -> is that body still in the world?
//
// 0x101297d0 takes one handle and pushes a bool. Small, and load-bearing:
// PainHead:Tick nils its body handle when this answers false -
//
//     if not PHYSICS.IsHavokBodyInWorld(he) then he = nil end
//
// - because a body can be gone by the time the hit is resolved (the thing
// gibbed, and Ragdoll.Remove took it). Unimplemented, the call returned nil,
// `not nil` is true, and `he` was cleared on EVERY hit. Every
// WORLD.HitPhysicObject below that line then got nothing, which is why the alt
// fire moved debris - spawned fresh and hit through another path - but never
// shoved an intact prop.
//
// Both kinds of handle a trace can report have to answer here: a script body
// slot, and the encoded limb handle a hit on a monster's bone reports.
int ScriptEngine::L_PHYSICS_IsHavokBodyInWorld(lua_State* L) {
    ScriptEngine* self = From(L);
    bool live = false;
    if (lua_isnumber(L, 1)) {
        const int handle = int(lua_tonumber(L, 1));
        int entity = 0, joint = -1;
        if (self->LimbFromHandle(handle, entity, joint))
            live = self->Find(entity) != nullptr;
        else if (handle >= 0 && self->physics_)
            live = self->physics_->ScriptBodyExists(handle);
    }
    lua_pushboolean(L, live ? 1 : 0);
    return 1;
}

// The limb boxes of one model, derived once and kept.
//
// Deriving them means loading the model again for its SKIN WEIGHTS, which the
// skeleton cache does not keep - it holds bones and bind matrices only. That is
// once per model type for the life of the process, against a box set that never
// changes: the boxes live in bone space, so animation moves them for free.
const std::vector<LimbBounds>* ScriptEngine::Hitboxes(const std::string& model) {
    if (model.empty()) return nullptr;
    const auto it = hitboxes_.find(model);
    if (it != hitboxes_.end()) return &it->second;

    // Cached even when it comes back empty: a model with no .rde is a settled
    // answer, not something to retry every frame.
    std::vector<LimbBounds>& slot = hitboxes_[model];
    const std::string base = dataRoot_ + "/Models/" + model;

    Ragdoll ragdoll;
    if (!Ragdoll::Load(base + ".rde", ragdoll)) return &slot;
    Model loaded;
    if (!Model::Load(base + ".pkmdl", loaded)) return &slot;
    slot = BuildLimbBounds(loaded, ragdoll);
    LogInfo("hitboxes: %s -> %zu limbs", model.c_str(), slot.size());
    return &slot;
}

// Which ragdoll part a skeleton joint drives, or -1 for a bone the .hke does
// not name. The parts are the .hke's bodies in tree order, not the model's
// bones, so this is a name match.
int ScriptEngine::RagdollPartOfJoint(Entity& e, int joint) {
    if (e.ragdollSlot < 0 || !physics_) return -1;
    const std::string name = JointName(e, joint);
    if (name.empty()) return -1;
    const std::vector<std::string>& parts = physics_->RagdollBones(e.ragdollSlot);
    for (size_t p = 0; p < parts.size(); ++p)
        if (parts[p] == name) return int(p);
    return -1;
}

// ---------------------------------------------------------------- gibs
//
// MDL.MakeGib(e, group, velocityJoint) is how a monster comes apart.
// World::GibModel (0x10060D90) with Model::SetupGib (0x101E1A40):
//
//   1. the gib is a NEW entity of the model "<name>_gib", at the source's
//      position, rotation and scale (CreateEntity type 4);
//   2. SetupGib poses both skeletons and copies the source's bone matrices
//      into the gib's BY BONE NAME, then Ragdoll::Animate puts the gib's
//      limbs there - so the pieces start exactly where the body was;
//   3. Ragdoll::Activate hands them to the solver, and SetVelocities gives
//      every limb the source's linear and angular velocity - the body's, or
//      when the source is already an active ragdoll and a joint is named,
//      that joint's (CItem passes `gibGetVelFromJoint`);
//   4. a gib model with no ragdoll is removed again and nothing is returned,
//      which is the `if gib then` every caller wraps this in.
//
// The bursting apart is NOT here: CActor:CreateGib turns explosions off on
// the gib, and two ticks later turns them back on and calls
// RagdollSelfExplosion with the template's GibExplosionStrength * 0.2..0.25
// and GibExplosionRange. The scripts then release the source entity and
// re-key EntityToObject to the gib, so every later hit lands on the pieces.
int ScriptEngine::MakeGib(Entity& src, int group, const char* velocityJoint) {
    if (src.type != kModel || src.source.empty() || !physics_) return 0;
    const std::string gibModel = src.source + "_gib";
    const SkeletonCache::Entry* gibSkel = skeletons_.Get(gibModel);
    if (gibSkel == nullptr || gibSkel->bones.empty() || RagdollDef(gibModel) == nullptr) {
        LogInfo("gib: %s has no usable %s ragdoll", src.source.c_str(), gibModel.c_str());
        return 0;
    }

    // What the source was doing, read BEFORE anything is created.
    float lin[3] = {0, 0, 0}, ang[3] = {0, 0, 0};
    if (src.physicsBody >= 0) {
        physics_->GetScriptBodyVelocity(src.physicsBody, lin);
    } else if (velocityJoint != nullptr && *velocityJoint != '\0' && src.ragdollSlot >= 0 &&
               physics_->RagdollActive(src.ragdollSlot)) {
        const int part = RagdollPartOfJoint(src, JointIndexByName(src, velocityJoint));
        if (part >= 0) physics_->GetRagdollPartVelocity(src.ragdollSlot, part, lin, ang);
    }

    // The pose, matched by name. A gib bone the source rig lacks keeps its
    // bind place - SetupGib leaves it at whatever the gib computed for itself.
    std::vector<Mat4> seed = gibSkel->bindWorld;
    if (const std::vector<Mat4>* srcBones = PosedBones(src)) {
        if (const SkeletonCache::Entry* srcSkel = skeletons_.Get(src.source)) {
            for (size_t g = 0; g < gibSkel->bones.size(); ++g)
                for (size_t s = 0; s < srcSkel->bones.size() && s < srcBones->size(); ++s)
                    if (srcSkel->bones[s].name == gibSkel->bones[g].name) {
                        seed[g] = (*srcBones)[s];
                        break;
                    }
        }
    }

    Entity gib;
    gib.type = kModel;
    gib.source = gibModel;
    gib.name = src.name;
    gib.scale = src.scale;
    for (int c = 0; c < 3; ++c) gib.pos[c] = src.pos[c];
    for (int c = 0; c < 4; ++c) gib.rotWXYZ[c] = src.rotWXYZ[c];
    gib.visible = true;
    gib.inWorld = true;                 // GibModel calls World::AddEntity itself
    gib.collisionGroup = group;
    const int handle = nextHandle_++;
    Entity& live = entities_.emplace(handle, gib).first->second;
    ++created_;
    CreateRendererInstance(live);

    if (!EnableRagdoll(live, true, &seed) || live.ragdollSlot < 0) {
        ReleaseEntity(handle);
        return 0;
    }
    physics_->SetRagdollVelocity(live.ragdollSlot, lin, ang);
    LogInfo("gib: %s -> %s handle %d, v=(%.2f %.2f %.2f)", src.name.c_str(), gibModel.c_str(),
            handle, lin[0], lin[1], lin[2]);
    return handle;
}

int ScriptEngine::L_MDL_MakeGib(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const int group = int(luaL_optnumber(L, 2, 0));
    const char* joint = lua_isstring(L, 3) ? lua_tostring(L, 3) : "";
    const int gib = self->MakeGib(*e, group, joint);
    if (gib <= 0) return 0;             // nothing pushed: `if gib then` fails
    lua_pushnumber(L, gib);
    return 1;
}

// MDL.SetRagdollMovedByExplosions(e, on) - 0x1012B2D0, GetBool(2, false).
int ScriptEngine::L_MDL_SetRagdollMovedByExplosions(lua_State* L) {
    if (Entity* e = From(L)->Find(HandleArg(L, 1)))
        e->ragdollMovedByExplosions = lua_toboolean(L, 2) != 0;
    return 0;
}

// MDL.RagdollSelfExplosion(e, x,y,z, strength, range) - 0x1012EBF0. The
// flag byte gates it in FUN_101B0DC0: an inactive ragdoll, or one not moved
// by explosions, takes nothing.
int ScriptEngine::L_MDL_RagdollSelfExplosion(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (!e || e->ragdollSlot < 0 || !self->physics_ || !e->ragdollMovedByExplosions ||
        !self->physics_->RagdollActive(e->ragdollSlot))
        return 0;
    const float centre[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                             float(luaL_optnumber(L, 4, 0))};
    self->physics_->RagdollSelfExplosion(e->ragdollSlot, centre, float(luaL_optnumber(L, 5, 0)),
                                         float(luaL_optnumber(L, 6, 0)));
    return 0;
}

// MDL.ApplyVelocitiesToAllJoints(e, vx,vy,vz, wx,wy,wz) - 0x1012D0D0 into
// Ragdoll::SetVelocities. The demon-mode gib uses it in place of the burst.
int ScriptEngine::L_MDL_ApplyVelocitiesToAllJoints(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* e = self->Find(HandleArg(L, 1));
    if (!e || e->ragdollSlot < 0 || !self->physics_) return 0;
    const float lin[3] = {float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
                          float(luaL_optnumber(L, 4, 0))};
    const float ang[3] = {float(luaL_optnumber(L, 5, 0)), float(luaL_optnumber(L, 6, 0)),
                          float(luaL_optnumber(L, 7, 0))};
    self->physics_->SetRagdollVelocity(e->ragdollSlot, lin, ang);
    return 0;
}


}  // namespace painful
