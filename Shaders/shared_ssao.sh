// SSAO's occlusion pass (Render/Ssao.h), shared by the single- and multisampled
// depth: the includer declares s_depth and defines SsaoDepth(ivec2), the depth at
// a full-size pixel. Writes r the occlusion kept, g the view depth.
#ifndef PAINFUL_SHARED_SSAO
#define PAINFUL_SHARED_SSAO

uniform mat4 u_ssaoInvProj;
uniform vec4 u_ssaoScreen; // xy: the scene in pixels, z: 1 when its origin is bottom-left, w: 1 when depth runs -1..1
uniform vec4 u_ssaoParams; // x: radius as a share of the screen's height, y: strength, z: the projection's y scale
// x: intensity, y: sin of the elevation a tap must pass (SSAOAngle), z/w: the height
// over the surface, as shares of the radius, where a tap starts and fully counts (SSAOHeight).
uniform vec4 u_ssaoShape;

#define SSAO_SAMPLES 12
#define SSAO_TURNS 7.0

// The view-space position of a full-size pixel.
vec3 SsaoPosition(ivec2 p)
{
	ivec2 at = clamp(p, ivec2(0, 0), ivec2(u_ssaoScreen.xy) - ivec2(1, 1));
	float d = SsaoDepth(at);
	vec2 uv = (vec2(at) + 0.5) / u_ssaoScreen.xy;
	float ndcY = u_ssaoScreen.z > 0.5 ? uv.y * 2.0 - 1.0 : 1.0 - uv.y * 2.0;
	float ndcZ = u_ssaoScreen.w > 0.5 ? d * 2.0 - 1.0 : d;
	vec4 v = mul(u_ssaoInvProj, vec4(uv.x * 2.0 - 1.0, ndcY, ndcZ, 1.0));
	return v.xyz / v.w;
}

// A 2x2 Bayer index of a pixel's low bits: 0 2 / 3 1.
float SsaoBayer2(vec2 b)
{
	return 2.0 * abs(b.x - b.y) + b.y;
}

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy) * 2;
	vec3 P = SsaoPosition(p);
	float depth = abs(P.z);
	if (SsaoDepth(p) >= 0.99999)
	{
		gl_FragColor = vec4(1.0, depth, 0.0, 1.0);
		return;
	}

	// The surface's normal from its neighbours, each axis from the side whose
	// depth runs on smoothly, so an edge does not bend it.
	vec3 right = SsaoPosition(p + ivec2(1, 0)) - P;
	vec3 left = P - SsaoPosition(p - ivec2(1, 0));
	vec3 up = SsaoPosition(p + ivec2(0, 1)) - P;
	vec3 down = P - SsaoPosition(p - ivec2(0, 1));
	vec3 dx = abs(right.z) < abs(left.z) ? right : left;
	vec3 dy = abs(up.z) < abs(down.z) ? up : down;
	vec3 n = normalize(cross(dx, dy));
	if (dot(n, P) > 0.0) n = -n;

	// The spiral's turn and radial offset from a 4x4 Bayer tile: all 16 turns inside
	// every 4x4 block, which the blur's weights average out exactly. A per-pixel hash
	// left diagonal streaks the blur could not cancel.
	vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
	float tile = (4.0 * SsaoBayer2(mod(cell, 2.0)) + SsaoBayer2(floor(cell * 0.5)) + 0.5) / 16.0;
	float spin = 6.2831853 * tile;
	float jitter = fract(tile * 7.0);

	// Horizon occlusion over the spiral: each tap by its elevation past the bias, its
	// height over the surface and its distance, so the average is scale-free. The
	// radius is a share of the screen, so in the world it grows with the depth.
	float pixels = u_ssaoParams.x * u_ssaoScreen.y;
	float radius = 2.0 * u_ssaoParams.x * depth / u_ssaoParams.z;
	float r2 = radius * radius;
	float sum = 0.0;
	for (int i = 0; i < SSAO_SAMPLES; ++i)
	{
		float alpha = (float(i) + jitter) / float(SSAO_SAMPLES);
		float turn = alpha * SSAO_TURNS * 6.2831853 + spin;
		vec2 offset = vec2(cos(turn), sin(turn)) * (alpha * pixels);
		vec3 v = SsaoPosition(p + ivec2(floor(offset + 0.5))) - P;
		float vv = dot(v, v);
		float h = dot(v, n);
		float elevation = clamp((h / sqrt(vv + 1e-8) - u_ssaoShape.y) / max(1.0 - u_ssaoShape.y, 0.01), 0.0, 1.0);
		float height = smoothstep(u_ssaoShape.z * radius, u_ssaoShape.w * radius, h);
		sum += elevation * height * max(1.0 - vv / r2, 0.0);
	}
	float kept = max(0.0, 1.0 - sum * u_ssaoShape.x / float(SSAO_SAMPLES));
	gl_FragColor = vec4(kept, depth, 0.0, 1.0);
}

#endif
