// Real L2 spherical harmonics, in world space: the basis cs_sdfprobe projects
// its rays onto. The coefficients are stored with the cosine lobe applied, so
// the sum is irradiance / pi - the ambient a Lambert surface multiplies its
// albedo by.
#ifndef PAINFUL_SHARED_SH
#define PAINFUL_SHARED_SH

float ShBasis(int k, vec3 d)
{
	if (k == 0) return 0.282095;
	if (k == 1) return 0.488603 * d.y;
	if (k == 2) return 0.488603 * d.z;
	if (k == 3) return 0.488603 * d.x;
	if (k == 4) return 1.092548 * d.x * d.y;
	if (k == 5) return 1.092548 * d.y * d.z;
	if (k == 6) return 0.315392 * (3.0 * d.z * d.z - 1.0);
	if (k == 7) return 1.092548 * d.x * d.z;
	return 0.546274 * (d.x * d.x - d.y * d.y);
}

#endif
