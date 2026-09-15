// Pf.RendererType 1: a vertex's traced light, read from its history texel, and
// its sheen fit (Render/SdfVertexLight.h). The includer defines
// PAINFUL_SDF_VERTEX_STAGE, and PAINFUL_SDF_SHEEN_STAGE_R/G/B to read the fit.
#ifndef PAINFUL_SHARED_SDFVERTEX
#define PAINFUL_SHARED_SDFVERTEX

USAMPLER2D(s_sdfVertexLight, PAINFUL_SDF_VERTEX_STAGE);

uniform vec4 u_sdfVertex; // x: the buffer's first history slot, y: 1 to read it, z: the history's width

ivec2 SdfVertexTexel(float index)
{
	int slot = int(u_sdfVertex.x + index + 0.5);
	int side = int(u_sdfVertex.z);
	return ivec2(slot - (slot / side) * side, slot / side);
}

// rgb: irradiance / pi along the vertex normal, times w; w: 1 where the vertex
// has been traced inside the fields, 0 for the box ambient.
vec4 SdfVertexLight(float index)
{
	if (u_sdfVertex.y < 0.5) return vec4_splat(0.0);
	uint lit = texelFetch(s_sdfVertexLight, SdfVertexTexel(index), 0).x;
	if ((lit >> 30u) == 0u) return vec4_splat(0.0);
	vec3 rgb = vec3(float(lit & 1023u), float((lit >> 10u) & 1023u), float((lit >> 20u) & 1023u)) * (4.0 / 1023.0);
	return vec4(rgb, 1.0);
}

#ifdef PAINFUL_SDF_SHEEN_STAGE_R
// The sheen fit, a colour channel a texture: the light arriving from direction d
// is dot(texel, vec4(1.0, d)), as the vertex's latest trace wrote it.
SAMPLER2D(s_sdfSheenR, PAINFUL_SDF_SHEEN_STAGE_R);
SAMPLER2D(s_sdfSheenG, PAINFUL_SDF_SHEEN_STAGE_G);
SAMPLER2D(s_sdfSheenB, PAINFUL_SDF_SHEEN_STAGE_B);

// Channel 0 red, 1 green, 2 blue; zero where nothing is read.
vec4 SdfVertexSheen(int channel, float index)
{
	if (u_sdfVertex.y < 0.5) return vec4_splat(0.0);
	ivec2 at = SdfVertexTexel(index);
	if (channel == 0) return texelFetch(s_sdfSheenR, at, 0);
	if (channel == 1) return texelFetch(s_sdfSheenG, at, 0);
	return texelFetch(s_sdfSheenB, at, 0);
}
#endif

#endif
