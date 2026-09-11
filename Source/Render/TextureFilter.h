#pragma once
#include <cstdint>
#include <string>

namespace painful {

// Cfg.TextureFiltering, the Video Options row: what the min/mag/mip filter is
// for every scene texture whose material did not ask for point sampling.
// The original's MaterialSystem::SetTexFiltering rewrites the per-stage
// filter byte the same way; Menu.md, "Texture filtering".
enum class TextureFilter { Bilinear, Trilinear, Anisotropic };

// The process-wide setting. The renderers read it at bind time, so a change
// from the menu takes effect on the next frame with nothing reloaded.
void SetTextureFilter(TextureFilter filter);
TextureFilter CurrentTextureFilter();

// "Bilinear" / "Trilinear" / "Anisotropic", the strings the menu stores.
bool TextureFilterFromName(const std::string& name, TextureFilter& out);
const char* TextureFilterName(TextureFilter filter);

// A material's sampler flags with the current filter in the min/mag/mip
// bits. Point sampling (texenv "point") is the material's own and stays.
uint32_t FilteredSampler(uint32_t materialFlags);

// Cfg.TextureFiltering as the scripts hold it; an unknown word is logged and
// leaves the setting alone. Boot and R3D.SetTexFiltering both come here.
void ApplyTextureFilterName(const std::string& name);

} // namespace painful
