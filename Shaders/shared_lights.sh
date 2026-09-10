#ifndef PAINFUL_SHARED_LIGHTS_SH
#define PAINFUL_SHARED_LIGHTS_SH

// The dynamic lights, evaluated per pixel, shared by the world mesh and the
// models. Include it after defining the two sampler stages:
//
//     #define PAINFUL_PROJ_STAGE     5
//     #define PAINFUL_PROJFALL_STAGE 6
//     #define PAINFUL_SHADOW_STAGE   7
//     #define PAINFUL_DIRSHADOW_STAGE 8
//     #include "shared_lights.sh"
//
// The original ran two entirely different paths - a projected cookie on the
// world (light.shader's tu2_pointpass / tu2_spotpass) and four pre-attenuated
// per-entity constants on models (Entity::ComputeVSLights) - and they did not
// match. One function is the deviation; the arithmetic inside it is the
// world pass's, recovered from the shipped ps_1_1 binaries.
// Docs/Reference/Lighting.md

// PAINFUL_MAX_DYN is a shaderc --define from the top-level CMakeLists, the
// same number the C++ side sizes kMaxDynamicLights from. Not defaulted here on
// purpose: bgfx pairs these arrays with the C++ ones by NAME at runtime, so a
// second value that drifted would be wrong lighting rather than a build error.
// Undefined, it fails on the array sizes below.

uniform vec4 u_dynCount; // x: how many, y: which one carries the projector (-1 none)
uniform vec4 u_dynPos[PAINFUL_MAX_DYN]; // xyz: world position, w: range
uniform vec4 u_dynColor[PAINFUL_MAX_DYN]; // rgb: colour x min(intensity*0.5, 1), w: type
uniform vec4 u_dynAxis[PAINFUL_MAX_DYN]; // xyz: spot axis, w: cos(outer edge), -1 = no cone
uniform vec4 u_dynCone[PAINFUL_MAX_DYN]; // x: cos(inner edge), y: tan(outer half-angle)
uniform vec4 u_dynProjX; // xyz: projector's right vector
uniform vec4 u_dynProjY; // xyz: projector's up vector

SAMPLER2D(s_proj, PAINFUL_PROJ_STAGE);
SAMPLER2D(s_projfall, PAINFUL_PROJFALL_STAGE);

// The flashlight's shadow map: world -> shadow uv and depth, crop included,
// and (on, normal offset, light offset, 1/size) with the offsets in texels.
// Render/ShadowMap.h builds both. Docs/Reference/Lighting.md, "Shadows"
uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;
SAMPLER2DSHADOW(s_shadow, PAINFUL_SHADOW_STAGE);

// Nine hardware-compared taps a texel apart. Each tap is bilinear, so it
// already ramps over one texel; a texel apart the ramps overlap and the edge
// is continuous. Four taps two texels apart were four visible steps.
float Pcf3x3(sampler2DShadow s, vec2 uv, float z, float t)
{
	float sum = 0.0;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
			sum += shadow2D(s, vec3(uv + vec2(float(x), float(y)) * t, z));
	return sum / 9.0;
}

// Outside the map is lit: the cookie is black past the cone rim anyway, and
// the ramp is zero past range.
float ShadowTerm(vec3 p)
{
	vec4 sc = mul(u_shadowMtx, vec4(p, 1.0));
	if (sc.w <= 0.0) return 1.0;
	vec3 c = sc.xyz / sc.w;
	if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
	return Pcf3x3(s_shadow, c.xy, c.z, u_shadowParams.w);
}

// The models' shadows from the environment directional: an orthographic map
// about the camera with ONLY the models in it. Laid wherever the map says,
// baked shade included - the original's blobs and HL2's did the same, and
// the world's own occlusion is not consulted. Docs/Reference/Lighting.md
uniform mat4 u_dirShadowMtx;
uniform vec4 u_dirShadowParams; // x: strength (0 off), y: normal offset, z: light offset (world), w: 1/size
uniform vec4 u_dirShadowDir; // xyz: to the light, w: PAINFUL_SHADOWVIEW
uniform vec4 u_dirShadowFade; // x: edge fade width (uv)
SAMPLER2DSHADOW(s_dirShadow, PAINFUL_DIRSHADOW_STAGE);

float ModelShadow(vec3 wpos, vec3 n)
{
	if (u_dirShadowParams.x <= 0.0) return 1.0;
	vec3 p = wpos + n * u_dirShadowParams.y + u_dirShadowDir.xyz * u_dirShadowParams.z;
	vec3 c = mul(u_dirShadowMtx, vec4(p, 1.0)).xyz;
	if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z < 0.0 || c.z > 1.0)
		return 1.0;
	float models = Pcf3x3(s_dirShadow, c.xy, c.z, u_dirShadowParams.w);
	// The box ends somewhere in view, so the shadows fade out over its last
	// stretch rather than stopping on a line.
	float edge = min(min(c.x, 1.0 - c.x), min(c.y, 1.0 - c.y));
	float keep = clamp(edge / max(u_dirShadowFade.x, 0.0001), 0.0, 1.0);
	return 1.0 - (1.0 - models) * keep;
}

// diffuse and spec accumulate; multiply DIFFUSE by the albedo afterwards, the
// way `mul_x2 r0.rgb, r0, t3` closes both shipped light shaders. specular is
// (exponent, strength, N.L gate width); strength 0 skips it, which is what the
// world mesh passes - its light passes have no specular term at all.
void DynamicLights(vec3 wpos, vec3 n, vec3 eye, vec3 specular,
		inout vec3 diffuse, inout vec3 spec)
{
	for (int i = 0; i < PAINFUL_MAX_DYN; ++i)
	{
		if (float(i) >= u_dynCount.x) break;
		vec3 d = u_dynPos[i].xyz - wpos;
		float range = max(u_dynPos[i].w, 0.001);
		float dist2 = dot(d, d);
		float dist = sqrt(max(dist2, 1e-8));
		vec3 l = d / max(dist, 0.0001);
		float ndotl = dot(n, l);
		if (ndotl <= 0.0) continue;

		// ps_atten_dst2.pso: `add r0.a, 1-t0.a, -t1.a` over two lookups of
		// special/atten, which holds (r/R) squared. So the falloff is
		// quadratic and StartFalloff never reaches it - that is the world
		// pass's curve, and the models now share it.
		float att = 1.0 - dist2 / (range * range);
		vec3 tint = vec3_splat(1.0);
		// The shipped pixel shaders end on `mul_x2 r0.rgb, r0, t3`, and the
		// spot one opens with another `mul_x2` - x2 for a point light, x4 for
		// a spot, over a colour already scaled by intensity*0.5.
		float gain = 2.0;

		// A projector beam is the one shape bounded along its axis rather than
		// by the sphere; everything else stops at `range`.
		bool projector = u_dynColor[i].w > 2.5 && float(i) == u_dynCount.y;
		if (!projector && dist2 >= range * range) continue;

		if (u_dynColor[i].w > 2.5)
		{
			float axial = dot(-l, u_dynAxis[i].xyz);
			if (axial <= 0.0) continue;
			if (projector)
			{
				// A PROJECTOR BEAM IS BOUNDED ALONG ITS AXIS, NOT BY A SPHERE.
				// Its falloff is the axial ramp below, so cutting it at radial
				// `range` stops the light dead on a spherical arc while the
				// ramp is still bright - a hard curved line across a floor,
				// which is exactly what a sphere the beam is wider than draws.
				// Bound it by the same quantity the ramp uses instead, and
				// every edge of the beam becomes a place where a smooth term
				// reaches zero: the ramp at zAxial = range, the cookie at the
				// cone rim, N.L at the terminator.
				float zAxial = max(dist * axial, 0.0001);
				if (zAxial >= range) continue;
				// The cookie rides the $proj matrix Light::UpdateProj builds
				// (0x101d4d90): a look-at down the axis, then a perspective
				// whose HALF-fov is acos(coneAngleCos) - the outer cone
				// exactly. tu2_proj_1.vso emits it as oT0.xy over a shared
				// oT0.w, so the divide is the perspective one.
				float span = max(zAxial * u_dynCone[i].y, 0.0001);
				vec2 uv = vec2(dot(-d, u_dynProjX.xyz), dot(-d, u_dynProjY.xyz));
				uv = uv / (span * 2.0) + vec2_splat(0.5);
				// No bounds test: the cookie is black at its border and the
				// sampler clamps, so outside the cone it reads zero on its own.
				// A discard here would be one more hard edge for nothing.
				// The ramp does NOT ride the projection. tu2_proj_1 emits
				// `dp4 oT1.x, v0, c14` - ONE plane row, so the falloff is
				// linear in distance along the beam, scaled by 1/range. Read
				// at the projected z instead it sits at ~0.97 everywhere,
				// which is inside the texture's fade-out tail, and the beam
				// all but vanishes.
				tint = texture2D(s_proj, uv).rgb;
				att = texture2D(s_projfall, vec2(zAxial / range, 0.5)).r;
				gain = 4.0;
				// The shadow rides the same beam. The receiver is lifted off
				// its surface along the normal and toward the light by a
				// texel or so IN WORLD UNITS: a shadow texel grows with the
				// distance down the beam, so the bias follows it and stays
				// the same fraction of a texel near and far.
				if (u_shadowParams.x > 0.5)
				{
					float texel = 2.0 * zAxial * u_dynCone[i].y * u_shadowParams.w;
					vec3 p = wpos + n * (texel * u_shadowParams.y) +
							l * (texel * u_shadowParams.z);
					att *= ShadowTerm(p);
				}
			}
			else if (u_dynAxis[i].w > -1.0)
			{
				// A spot with no cookie: the cone ramp instead, full inside
				// 0.8 of the angle and out to nothing at the edge.
				if (axial < u_dynAxis[i].w) continue;
				if (axial < u_dynCone[i].x)
					att *= (axial - u_dynAxis[i].w) /
							max(u_dynCone[i].x - u_dynAxis[i].w, 0.0001);
			}
		}

		vec3 energy = u_dynColor[i].rgb * tint * (gain * att);
		diffuse += energy * ndotl;

		if (specular.y > 0.0)
		{
			// Per pixel, from the real eye and the real light vector. The
			// original built ONE half-vector per entity out of an
			// unnormalised (camera - entityPos) + lightDir, which tracks the
			// VIEW far more than the light and reads as a camera-facing wash
			// on anything a lamp is near.
			vec3 h = normalize(l + normalize(eye - wpos));
			// palskin's `lit` gates specular on N.L > 0. That is a step, and
			// per pixel it draws a hard line along the contour, so it is
			// ramped over specular.z of N.L.
			spec += energy * pow(max(dot(n, h), 0.0), specular.x) * specular.y *
					smoothstep(0.0, specular.z, ndotl);
		}
	}
}

#endif // PAINFUL_SHARED_LIGHTS_SH
