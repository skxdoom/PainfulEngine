#include "Camera.h"
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace painful {

void Camera::ViewProj(int width, int height, float far, float* view, float* proj) const {
	const Vec3 forward = Forward();
	const bx::Vec3 eye = {pos[0], pos[1], pos[2]};
	const bx::Vec3 at = {pos[0] + forward[0], pos[1] + forward[1], pos[2] + forward[2]};
	// PainEngine data is right-handed (Maya export). bx defaults to left-handed,
	// which renders the whole world mirrored.
	bx::mtxLookAt(view, eye, at, {0.0f, 1.0f, 0.0f}, bx::Handedness::Right);
	bx::mtxProj(proj, fovDegrees, float(width) / float(height), nearPlane, far,
			bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
	// A clipped camera keeps its projection: the world shader discards the
	// wrong side of y = mirrorY instead. An oblique near plane was tried and
	// its skewed far plane cut the distant reflection off in a ring.
}

} // namespace painful
