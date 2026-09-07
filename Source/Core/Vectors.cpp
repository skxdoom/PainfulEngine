#include "Vectors.h"

#include <cmath>

namespace painful {

// Verbatim from FUN_1011bea0, which is qz * qy * qx written out.
Quat Quat::FromEuler(float ax, float ay, float az) {
	const float cx = std::cos(ax * 0.5f), sx = std::sin(ax * 0.5f);
	const float cy = std::cos(ay * 0.5f), sy = std::sin(ay * 0.5f);
	const float cz = std::cos(az * 0.5f), sz = std::sin(az * 0.5f);
	return Quat(cz * cy * cx + sx * sz * sy,
			cz * cy * sx - sz * sy * cx,
			sy * cz * cx + sz * sx * cy,
			sz * cy * cx - sy * sx * cz);
}

Quat operator*(const Quat& a, const Quat& b) {
	return Quat(a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
}

Vec3 Quat::Rotate(const Vec3& v) const {
	const Quat vq(0.f, v.x, v.y, v.z);
	const Quat r = (Conjugate() * vq) * *this;
	return Vec3(r.x, r.y, r.z);
}

float Quat::Length() const { return std::sqrt(w * w + x * x + y * y + z * z); }

Quat Quat::Normalized() const {
	const float len = Length();
	if (len <= 1e-8f) return Quat();
	return Quat(w / len, x / len, y / len, z / len);
}

Quat Nlerp(const Quat& a, const Quat& b, float u) {
	const Quat to = Dot(a, b) < 0.f ? -b : b;
	const Quat mix(a.w + (to.w - a.w) * u, a.x + (to.x - a.x) * u,
			a.y + (to.y - a.y) * u, a.z + (to.z - a.z) * u);
	return mix.Length() > 1e-8f ? mix.Normalized() : a;
}

} // namespace painful
