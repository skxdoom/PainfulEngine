#pragma once
#include "../Assets/Emitter.h"
#include "../Assets/ShaderScript.h"
#include <cstdint>

namespace painful {

// A resolved .shader pass translated into bgfx terms. Only state expressible
// through the fixed pipeline lives here; the texop combines and GPU programs
// are realised by our own shaders.
struct MaterialState {
    // Write/depth/blend/cull bits, without MSAA (the caller owns that).
    uint64_t state = 0;
    // Per-stage sampler flags from texenv[N] (wrap/clamp, filter).
    uint32_t sampler[4] = {0, 0, 0, 0};
    // Alpha-test reference for "alphafunc greater", 0..1; negative = disabled.
    // Modern APIs dropped fixed-function alpha test, so the fragment shader
    // discards below this value.
    float alphaRef = -1.f;
    // 1 for "modulate", 2 for "modulate2x" - the lightmap overbright factor.
    float lightScale = 1.f;
    // UV scroll in units per second for stages 0 and 1 (pan[N] in the
    // scripts - conveyor belts, waterfalls). Zero when static.
    float pan0[2] = {0, 0};
    float pan1[2] = {0, 0};

    // Builds the state from a resolved pass. Unknown values fall back to the
    // most common defaults and are reported through *warning when given.
    static MaterialState FromPass(const ShaderPass& pass, std::string* warning = nullptr);
};

// bgfx blend bits for one of the engine's numbered blend modes (the BlendMode
// enum in Assets/Emitter.h). Particles and billboards both feed their mode
// straight into the same D3Dev state field, so they share one translation.
uint64_t BlendModeState(int mode);

} // namespace painful
