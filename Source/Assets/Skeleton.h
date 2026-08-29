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

// --- playback -------------------------------------------------------------
//
// The pair below is the per-frame path, split so the expensive half happens
// once. Matching bone names to tracks is a string lookup per bone; doing it
// inside the per-frame call would hash every bone name of every animated
// model every frame.

// One track pointer per bone, null where the animation does not drive it.
// Recompute only when the animation changes.
void ResolveAnimTracks(const std::vector<Bone>& bones, const Animation& anim,
                       std::vector<const AnimTrack*>& outTracks);

// Skinning matrices at a playback TIME rather than a key index.
//
// The key is chosen by floor - the last one at or before `time` - with no
// interpolation between keys. The .ani carries a fixed frameTime, so the keys
// are evenly spaced and this is what the data was authored for; if it ever
// visibly steps, interpolate here and nowhere else.
void ComputeSkinningMatricesAtTime(const std::vector<Bone>& bones,
                                   const std::vector<Mat4>& inverseBind,
                                   const std::vector<const AnimTrack*>& tracks,
                                   float time,
                                   std::vector<Mat4>& outSkin);

// Deform one mesh into the renderer's 8-float vertex layout (pos3, normal3,
// uv2), which is what a GPU buffer wants - SkinMesh alone writes positions.
// Normals are carried by the same matrices without their translation, so
// lighting follows the pose instead of staying stuck in the bind pose.
void SkinMeshVertices(const ModelMesh& mesh,
                      const std::vector<Mat4>& skin,
                      std::vector<float>& outVerts);

} // namespace painful
