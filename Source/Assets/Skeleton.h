#pragma once
#include "Ani.h"
#include "Pkmdl.h"

namespace painful {

// Fill in Bone::parent from the preorder ordering + child counts.
void BuildHierarchy(std::vector<Bone>& bones);

// Bind-pose world matrices and their inverses, in bone order.
void ComputeBindWorld(const std::vector<Bone>& bones,
                      std::vector<Mat4>& bindWorld,
                      std::vector<Mat4>& inverseBind);

// Per-bone skinning matrices for one keyframe of an animation:
//     skin[b] = inverse(bindWorld[b]) * animWorld[b]
// Bones without a matching track fall back to their bind pose.
void ComputeSkinningMatrices(const std::vector<Bone>& bones,
                             const std::vector<Mat4>& inverseBind,
                             const Animation& anim,
                             int keyIndex,
                             std::vector<Mat4>& outSkin);

// Deform one mesh with the given skinning matrices. Positions are written as
// xyz triples, one per vertex.
void SkinMesh(const ModelMesh& mesh,
              const std::vector<Mat4>& skin,
              std::vector<float>& outPositions);

} // namespace painful
