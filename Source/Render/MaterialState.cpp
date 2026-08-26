#include "MaterialState.h"

#include <bgfx/bgfx.h>
#include <cstdlib>
#include <sstream>

namespace painful {

namespace {

bool IsTrue(const std::string& v) { return v == "true"; }

void Warn(std::string* warning, const std::string& what) {
    if (!warning) return;
    if (!warning->empty()) *warning += "; ";
    *warning += what;
}

uint64_t BlendBits(const std::string& mode, std::string* warning) {
    if (mode.empty() || mode == "none") return 0;
    if (mode == "translucent" || mode == "alpha") return BGFX_STATE_BLEND_ALPHA;
    if (mode == "add") return BGFX_STATE_BLEND_ADD;
    if (mode == "modulate")
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);
    if (mode == "invmodulate")
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_COLOR);
    if (mode == "destalpha" || mode == "desttranslucent")
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_ALPHA,
                                     BGFX_STATE_BLEND_INV_DST_ALPHA);
    if (mode == "revsubtract")
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
               BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_REVSUB);
    Warn(warning, "unknown blend '" + mode + "'");
    return 0;
}

// texenv[N] = wrap|clamp  bilinear|bilinear_nomips|point
uint32_t SamplerBits(const std::string& value, std::string* warning) {
    uint32_t flags = 0;
    std::istringstream in(value);
    std::string word;
    while (in >> word) {
        if (word == "wrap") {
            // bgfx's default addressing is repeat.
        } else if (word == "clamp") {
            flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        } else if (word == "bilinear") {
            // bgfx's default filtering.
        } else if (word == "bilinear_nomips") {
            // No per-sampler way to pin the base mip in bgfx; nearest-mip is
            // the closest approximation.
            flags |= BGFX_SAMPLER_MIP_POINT;
        } else if (word == "point") {
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                     BGFX_SAMPLER_MIP_POINT;
        } else {
            Warn(warning, "unknown texenv word '" + word + "'");
        }
    }
    return flags;
}

} // namespace

MaterialState MaterialState::FromPass(const ShaderPass& pass, std::string* warning) {
    MaterialState out;

    out.state = BGFX_STATE_WRITE_RGB;
    if (pass.Get("colorwrite") == "false") out.state &= ~BGFX_STATE_WRITE_RGB;
    if (pass.Get("alphawrite", "true") == "true") out.state |= BGFX_STATE_WRITE_A;
    if (pass.Get("depthwrite", "true") == "true") out.state |= BGFX_STATE_WRITE_Z;
    if (pass.Get("depthtest", "true") == "true") out.state |= BGFX_STATE_DEPTH_TEST_LESS;

    const std::string cull = pass.Get("cull", "ccw");
    if (cull == "ccw") out.state |= BGFX_STATE_CULL_CCW;
    else if (cull == "cw") out.state |= BGFX_STATE_CULL_CW;
    else if (cull != "none") Warn(warning, "unknown cull '" + cull + "'");

    out.state |= BlendBits(pass.Get("blend", "none"), warning);

    if (IsTrue(pass.Get("alphatest"))) {
        const std::string func = pass.Get("alphafunc", "greater");
        if (func != "greater") Warn(warning, "unknown alphafunc '" + func + "'");
        out.alphaRef = float(std::atof(pass.Get("alpharef", "0").c_str())) / 255.f;
    }

    for (int i = 0; i < 4; ++i) {
        const std::string env = pass.Get("texenv[" + std::to_string(i) + "]");
        if (!env.empty()) out.sampler[i] = SamplerBits(env, warning);
    }

    // The lightmap combine: "texture modulate previous" is x1,
    // "texture modulate2x previous" the overbright x2.
    for (int i = 0; i < 4; ++i) {
        const std::string op = pass.Get("texop[" + std::to_string(i) + "]");
        if (op.find("modulate2x") != std::string::npos) out.lightScale = 2.f;
    }

    // UV scrolling: "pan[N] = u v" in units per second (conveyors etc.).
    auto readPan = [&](const char* key, float pan[2]) {
        std::istringstream in(pass.Get(key));
        in >> pan[0] >> pan[1];
    };
    readPan("pan[0]", out.pan0);
    readPan("pan[1]", out.pan1);
    return out;
}

} // namespace painful
