#include "Camera.h"
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace painful {

void Camera::ViewProj(int width, int height, float far, float* view, float* proj) const {
	const Vec3 forward = Forward();
	const bx::Vec3 eye = {pos[0], pos[1], pos[2]};
	const bx::Vec3 at = {pos[0] + forward[0], pos[1] + forward[1], pos[2] + forward[2]};
	// Roll spins the up hint about the view axis (Rodrigues, forward already
	// unit): a mirrored or cube-map camera passes its own up in and keeps it.
	Vec3 u = up;
	if (roll != 0.f) {
		const float c = std::cos(roll), s = std::sin(roll);
		const Vec3 f = forward;
		const Vec3 cross{f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2],
				f[0] * u[1] - f[1] * u[0]};
		const float d = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
		for (int i = 0; i < 3; ++i) u[i] = u[i] * c + cross[i] * s + f[i] * d * (1.f - c);
	}
	// PainEngine data is right-handed (Maya export). bx defaults to left-handed,
	// which renders the whole world mirrored.
	bx::mtxLookAt(view, eye, at, {u[0], u[1], u[2]}, bx::Handedness::Right);
	bx::mtxProj(proj, fovDegrees, float(width) / float(height), nearPlane, far,
			bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
	// A clipped camera keeps its projection: the world shader discards the
	// wrong side of y = mirrorY instead. An oblique near plane was tried and
	// its skewed far plane cut the distant reflection off in a ring.
}

} // namespace painful
