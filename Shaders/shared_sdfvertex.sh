// Pf.RendererType 1: a vertex's traced light, read from its history texel
// (Render/SdfVertexLight.h). The includer defines PAINFUL_SDF_VERTEX_STAGE.
#ifndef PAINFUL_SHARED_SDFVERTEX
#define PAINFUL_SHARED_SDFVERTEX

USAMPLER2D(s_sdfVertexLight, PAINFUL_SDF_VERTEX_STAGE);

uniform vec4 u_sdfVertex; // x: the buffer's first history slot, y: 1 to read it, z: the history's width

// rgb: irradiance / pi along the vertex normal, times w; w: 1 where the vertex
// has been traced inside the fields, 0 for the box ambient.
vec4 SdfVertexLight(float index)
{
	if (u_sdfVertex.y < 0.5) return vec4_splat(0.0);
	int slot = int(u_sdfVertex.x + index + 0.5);
	int side = int(u_sdfVertex.z);
	uint lit = texelFetch(s_sdfVertexLight, ivec2(slot - (slot / side) * side, slot / side), 0).x;
	if ((lit >> 30u) == 0u) return vec4_splat(0.0);
	vec3 rgb = vec3(float(lit & 1023u), float((lit >> 10u) & 1023u), float((lit >> 20u) & 1023u)) * (4.0 / 1023.0);
	return vec4(rgb, 1.0);
}

#endif
