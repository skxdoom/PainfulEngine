#include "TextureFilter.h"

#include "../Core/Log.h"

#include <bgfx/bgfx.h>

namespace painful {

namespace {

TextureFilter g_filter = TextureFilter::Trilinear;
bool g_forcePoint = false;

} // namespace

void SetTextureFilter(TextureFilter filter) { g_filter = filter; }

TextureFilter CurrentTextureFilter() { return g_filter; }

void SetForcePoint(bool on) { g_forcePoint = on; }

bool ForcePoint() { return g_forcePoint; }

uint32_t Mode96Sampler() {
	return g_forcePoint ? (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
			BGFX_SAMPLER_MIP_POINT) : UINT32_MAX;
}

bool TextureFilterFromName(const std::string& name, TextureFilter& out) {
	if (name == "Bilinear") { out = TextureFilter::Bilinear; return true; }
	if (name == "Trilinear") { out = TextureFilter::Trilinear; return true; }
	if (name == "Anisotropic") { out = TextureFilter::Anisotropic; return true; }
	return false;
}

const char* TextureFilterName(TextureFilter filter) {
	switch (filter) {
	case TextureFilter::Bilinear: return "Bilinear";
	case TextureFilter::Trilinear: return "Trilinear";
	case TextureFilter::Anisotropic: return "Anisotropic";
	}
	return "?";
}

uint32_t FilteredSampler(uint32_t materialFlags, bool lightmap) {
	// Pf.Mode96. The lightmap keeps its filtering: point-sampling it reads as
	// blocky shading rather than crunchy texels.
	if (g_forcePoint && !lightmap) {
		const uint32_t kKeep = ~(BGFX_SAMPLER_MIN_MASK | BGFX_SAMPLER_MAG_MASK |
				BGFX_SAMPLER_MIP_MASK);
		return (materialFlags & kKeep) | BGFX_SAMPLER_MIN_POINT |
				BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
	}
	// The engine skips only filter byte 2 (point); bilinear_nomips is
	// overridden with the rest. bgfx: no bits = linear on all three.
	if (materialFlags & BGFX_SAMPLER_MIN_POINT) return materialFlags;
	const uint32_t kFilterBits = BGFX_SAMPLER_MIN_MASK | BGFX_SAMPLER_MAG_MASK |
			BGFX_SAMPLER_MIP_MASK;
	uint32_t flags = materialFlags & ~kFilterBits;
	switch (g_filter) {
	case TextureFilter::Bilinear: flags |= BGFX_SAMPLER_MIP_POINT; break;
	case TextureFilter::Trilinear: break;
	case TextureFilter::Anisotropic:
		flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
		break;
	}
	return flags;
}

} // namespace painful

namespace painful {

void ApplyTextureFilterName(const std::string& name) {
	TextureFilter filter;
	if (!TextureFilterFromName(name, filter)) {
		LogWarn("texture filtering: Cfg.TextureFiltering '%s' unknown, keeping %s",
				name.c_str(), TextureFilterName(g_filter));
		return;
	}
	if (filter != g_filter) LogInfo("texture filtering: %s", TextureFilterName(filter));
	SetTextureFilter(filter);
}

} // namespace painful
