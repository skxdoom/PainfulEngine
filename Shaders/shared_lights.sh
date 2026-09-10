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
uniform vec4 u_dynCone[PAINFUL_MAX_DYN]; // x: cos(inner edge), y: tan(outer half-angle), z: baked (world only), w: its subtract strength
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

// The same, with a texel that differs per axis (an atlas that is not square).
float Pcf3x3v(sampler2DShadow s, vec2 uv, float z, vec2 t)
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

// The placed lights' shadows. Per slot: (atlas slot or -1, A, B, fade), the
// depth a point `dist` along its face has in the map being A + B / dist, and
// fade the pick's distance term. A spot has one face down its cone, a point
// light six, rendered a guard band wider than 90 degrees so a lookup at a
// face's edge still has neighbours to filter over. The face table is
// LightShadowAtlas.cpp's, and right = cross(forward, up) on both sides.
uniform vec4 u_dynShadow[PAINFUL_MAX_DYN];
uniform vec4 u_lightShadowInfo; // x: 1/(2 slots), y: v sign, zw: one atlas texel
#ifdef PAINFUL_LIGHTSHADOW_STAGE
SAMPLER2DSHADOW(s_lightShadow, PAINFUL_LIGHTSHADOW_STAGE);

// toPoint is light -> receiver, l the unit vector back to the light.
float LightShadow(int i, vec3 toPoint, vec3 n, vec3 l)
{
	vec4 sh = u_dynShadow[i];
	bool spot = u_dynColor[i].w > 2.5 && u_dynAxis[i].w > -1.0;
	// The face's half-fov cotangent: the cone's for a spot (clamped as the
	// atlas clamps it), the guard band's for a point light - 1/faceSize is
	// three atlas texels across.
	float cot;
	if (spot)
	{
		float c = clamp(u_dynAxis[i].w, 0.2, 0.999);
		cot = c / sqrt(1.0 - c * c);
	}
	else
	{
		cot = 1.0 - 4.0 * (3.0 * u_lightShadowInfo.z);
	}
	// The receiver lifted off its surface by a texel or so at its distance,
	// along the normal and toward the light, BEFORE the face is chosen: a
	// lifted point that stepped off its face read as lit, a seam.
	float texelWorld = 2.0 * length(toPoint) / cot * (3.0 * u_lightShadowInfo.z);
	vec3 p = toPoint + n * (texelWorld * 1.5) + l * (texelWorld * 1.0);
	vec3 F, U;
	int face = 0;
	if (spot)
	{
		F = u_dynAxis[i].xyz;
		vec3 pick = abs(F.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
		vec3 right = normalize(cross(F, pick));
		U = cross(right, F);
	}
	else
	{
		vec3 a = abs(p);
		if (a.x >= a.y && a.x >= a.z)
		{
			float s = p.x > 0.0 ? 1.0 : -1.0;
			face = p.x > 0.0 ? 0 : 1;
			F = vec3(s, 0.0, 0.0);
			U = vec3(0.0, 1.0, 0.0);
		}
		else if (a.y >= a.z)
		{
			float s = p.y > 0.0 ? 1.0 : -1.0;
			face = p.y > 0.0 ? 2 : 3;
			F = vec3(0.0, s, 0.0);
			U = vec3(0.0, 0.0, -s);
		}
		else
		{
			float s = p.z > 0.0 ? 1.0 : -1.0;
			face = p.z > 0.0 ? 4 : 5;
			F = vec3(0.0, 0.0, s);
			U = vec3(0.0, 1.0, 0.0);
		}
	}
	vec3 R = cross(F, U);
	float dist = dot(p, F);
	if (dist <= 0.0001) return 1.0;
	float u = 0.5 + 0.5 * dot(p, R) * cot / dist;
	float v = 0.5 + 0.5 * u_lightShadowInfo.y * dot(p, U) * cot / dist;
	if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) return 1.0;
	float z = sh.y + sh.z / dist;
	if (z > 1.0) return 1.0;
	// Into the atlas: column face % 3, row 2 * slot + face / 3, and the taps
	// kept inside the cell so a neighbour's face is never read.
	vec2 t = u_lightShadowInfo.zw;
	vec2 cellMin = vec2(float(face - (face / 3) * 3) / 3.0, (sh.x * 2.0 + float(face / 3)) * u_lightShadowInfo.x);
	vec2 cellSize = vec2(1.0 / 3.0, u_lightShadowInfo.x);
	vec2 uv = cellMin + vec2(u, v) * cellSize;
	uv = clamp(uv, cellMin + t * 1.5, cellMin + cellSize - t * 1.5);
	return mix(1.0, Pcf3x3v(s_lightShadow, uv, z, t), sh.w);
}
#endif

#ifdef PAINFUL_VM_STAGE
// The view model's own map (Render/ViewModelShadows.h): orthographic down
// the environment directional, fitted to the weapon. Only a view-model draw
// sets u_vmParams.x, and only with a directional to shadow sets y.
uniform vec4 u_vmParams; // x: this is the view model, y: the map is on, w: one texel in uv
uniform mat4 u_vmMtx;
uniform vec4 u_vmLight; // w: one texel in world units
SAMPLER2DSHADOW(s_vmShadow, PAINFUL_VM_STAGE);

float VmShadow(vec3 wpos, vec3 n, vec3 l)
{
	float texel = u_vmLight.w;
	vec3 p = wpos + n * (texel * 1.5) + l * (texel * 1.0);
	vec3 c = mul(u_vmMtx, vec4(p, 1.0)).xyz;
	if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
	return Pcf3x3(s_vmShadow, c.xy, c.z, u_vmParams.w);
}
#endif

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
// shadowMin collects the darkest placed-light shadow term at the pixel, for
// PAINFUL_SHADOWVIEW, and `occluded` what a BAKED light (u_dynCone.z, the
// world's lightmap already holds it) loses to a model's shadow - to be taken
// off the lightmap, in the same units as `diffuse`. Both are only written
// where the caller defined PAINFUL_LIGHTSHADOW_STAGE.
void DynamicLights(vec3 wpos, vec3 n, vec3 eye, vec3 specular,
		inout vec3 diffuse, inout vec3 spec, inout float shadowMin, inout vec3 occluded)
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

#ifdef PAINFUL_LIGHTSHADOW_STAGE
		float shadowTerm = 1.0;
		if (u_dynShadow[i].x >= 0.0)
		{
			shadowTerm = LightShadow(i, -d, n, l);
			shadowMin = min(shadowMin, shadowTerm);
		}
		if (u_dynCone[i].z > 0.5)
		{
			// Baked: adds nothing, takes away what the model occludes.
			occluded += u_dynColor[i].rgb * tint * (gain * att) * ndotl *
					(1.0 - shadowTerm) * u_dynCone[i].w;
			continue;
		}
		att *= shadowTerm;
#endif

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
