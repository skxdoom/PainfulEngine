#include "Rde.h"

#include <algorithm>
#include <cstdlib>

#include "Pkmdl.h"
#include "Skeleton.h"

namespace painful {

const RagdollLimb* Ragdoll::Find(const std::string& bone) const {
    for (const RagdollLimb& limb : limbs)
        if (limb.bone == bone) return &limb;
    return nullptr;
}

namespace {

// "  Key = 1.5  " -> key and value, both trimmed. Returns false for anything
// that is not an assignment, which includes blank lines and section headers.
bool SplitAssignment(const std::string& line, std::string& key, std::string& value) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    const auto trim = [](std::string s) {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        const size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    key = trim(line.substr(0, eq));
    value = trim(line.substr(eq + 1));
    return !key.empty();
}

} // namespace

bool Ragdoll::Load(const std::string& path, Ragdoll& out) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) {
        out.error = "cannot read file";
        return false;
    }

    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;

        const size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;

        if (line[first] == '[') {
            const size_t close = line.find(']', first);
            if (close == std::string::npos) continue;
            RagdollLimb limb;
            limb.bone = line.substr(first + 1, close - first - 1);
            if (!limb.bone.empty()) out.limbs.push_back(limb);
            continue;
        }

        std::string key, value;
        if (out.limbs.empty() || !SplitAssignment(line, key, value)) continue;
        RagdollLimb& limb = out.limbs.back();
        const float f = float(std::atof(value.c_str()));
        if      (key == "Mass")            limb.mass = f;
        else if (key == "LinearDamping")   limb.linearDamping = f;
        else if (key == "AngularDamping")  limb.angularDamping = f;
        else if (key == "Friction")        limb.friction = f;
        else if (key == "Restitution")     limb.restitution = f;
    }

    if (out.limbs.empty()) out.error = "no limbs";
    return out.error.empty();
}

std::vector<LimbBounds> BuildLimbBounds(const Model& model, const Ragdoll& ragdoll) {
    std::vector<LimbBounds> out;
    if (model.bones.empty() || ragdoll.limbs.empty()) return out;

    // Bone name -> the slot in `out` that collects it, so a vertex can be
    // charged to its limb without searching the list per vertex.
    std::vector<int> limbForBone(model.bones.size(), -1);
    for (size_t b = 0; b < model.bones.size(); ++b) {
        if (!ragdoll.Find(model.bones[b].name)) continue;
        LimbBounds limb;
        limb.bone = int(b);
        limb.name = model.bones[b].name;
        limb.min[0] = limb.min[1] = limb.min[2] = 1e30f;
        limb.max[0] = limb.max[1] = limb.max[2] = -1e30f;
        limbForBone[b] = int(out.size());
        out.push_back(limb);
    }
    if (out.empty()) return out;

    // The bind pose, inverted: model space -> bone space. Exactly the matrices
    // the skinning path builds, used in reverse.
    std::vector<Bone> bones = model.bones;
    BuildHierarchy(bones);
    std::vector<Mat4> bindWorld, inverseBind;
    ComputeBindWorld(bones, bindWorld, inverseBind);

    for (const ModelMesh& mesh : model.meshes) {
        if (!mesh.hasSkin()) continue;
        const size_t vc = mesh.vertexCount();
        for (size_t i = 0; i < vc; ++i) {
            // The bone that drives this vertex most. A vertex split across its
            // influences would smear every box into its neighbours.
            int best = -1;
            float bestWeight = 0.f;
            for (const SkinInfluence& inf : mesh.skin[i]) {
                if (inf.bone >= bones.size()) continue;
                if (inf.weight > bestWeight) {
                    bestWeight = inf.weight;
                    best = int(inf.bone);
                }
            }
            if (best < 0 || limbForBone[size_t(best)] < 0) continue;

            float local[3];
            inverseBind[size_t(best)].TransformPoint(mesh.verts[i * 8 + 0],
                                                     mesh.verts[i * 8 + 1],
                                                     mesh.verts[i * 8 + 2], local);
            LimbBounds& limb = out[size_t(limbForBone[size_t(best)])];
            for (int c = 0; c < 3; ++c) {
                limb.min[c] = std::min(limb.min[c], local[c]);
                limb.max[c] = std::max(limb.max[c], local[c]);
            }
            ++limb.vertices;
        }
    }

    // A limb the ragdoll names but no vertex is weighted to has no box. That is
    // a real answer, not a failure - report it rather than an inside-out one.
    for (LimbBounds& limb : out)
        if (limb.vertices == 0)
            for (int c = 0; c < 3; ++c) limb.min[c] = limb.max[c] = 0.f;

    return out;
}

} // namespace painful
