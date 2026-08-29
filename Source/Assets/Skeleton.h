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

// An extra rotation laid on one bone on top of whatever the animation says -
// MDL.ApplyJointRotation, which is how a monster's head follows the player and
// how a gun's barrel pitches. Euler angles in RADIANS (the shipped scripts
// clamp a barrel to math.pi/3), engine order, applied in the bone's OWN space
// so the bone turns where it is instead of swinging about its parent.
struct JointOverride {
    int bone = -1;
    float euler[3] = {0, 0, 0};
};

// Where every bone IS at a playback time: bone-local to MODEL space, before
// the bind pose is divided out. This is what a joint query wants - "where is
// the hand" is this matrix, not a skinning matrix, which only makes sense
// applied to a vertex that started in the bind pose.
//
// Keys are interpolated. A .ani holds a whole parent-relative matrix per key
// at 25-30 keys a second, which visibly steps if the nearest one is held, so
// the rotation is recovered and blended through a quaternion; see BlendPose.
//
// Bones the animation does not drive keep their bind pose, which is what
// leaves an unanimated arm attached to the shoulder instead of at the origin.
void ComputeBoneWorldAtTime(const std::vector<Bone>& bones,
                            const std::vector<const AnimTrack*>& tracks,
                            float time,
                            std::vector<Mat4>& outWorld,
                            const JointOverride* overrides = nullptr,
                            size_t overrideCount = 0);

// skin[b] = inverseBind[b] * boneWorld[b]. Split from the above because the
// renderer wants this and the joint natives want the bone world matrices, and
// both must come from ONE pose or a muzzle flash drifts off the barrel it is
// drawn on.
void BoneWorldToSkinning(const std::vector<Mat4>& inverseBind,
                         const std::vector<Mat4>& boneWorld,
                         std::vector<Mat4>& outSkin);

// The two above in one step, for a caller that only draws.
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
