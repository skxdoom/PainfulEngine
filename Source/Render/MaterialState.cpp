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
    auto readPair = [&](const char* key, float dst[2], float dx, float dy) {
        std::istringstream in(pass.Get(key));
        if (!(in >> dst[0] >> dst[1])) { dst[0] = dx; dst[1] = dy; }
    };
    readPair("pan[0]", out.pan0, 0.f, 0.f);
    readPair("pan[1]", out.pan1, 0.f, 0.f);
    // tile[N] is the stage scale, applied AFTER the pan - see
    // Docs/Reference/TextureTransforms.md. Absent means 1, i.e. no change at all, which
    // is what keeps every material that never mentions it identical.
    readPair("tile[0]", out.tile0, 1.f, 1.f);
    readPair("tile[1]", out.tile1, 1.f, 1.f);

    // The second stage, when the material names one. "disable" is the common
    // case and leaves stage1Op at 0. A $variable map (palskinnedemissive's
    // $emissivemap) has no name to load, so the stage stays off until whatever
    // supplies that variable is understood.
    const std::string op1 = pass.Get("texop[1]");
    if (!op1.empty() && op1.find("disable") == std::string::npos) {
        if (op1.find("modulatealphaadd") != std::string::npos) out.stage1Op = 3;
        else if (op1.find("modulate") != std::string::npos)    out.stage1Op = 1;
        else if (op1.find("add") != std::string::npos)         out.stage1Op = 2;
        std::string m = pass.Get("map[1]");
        if (!m.empty() && m[0] == '"') m = m.substr(1, m.find_last_of('"') - 1);
        if (!m.empty() && m[0] != '$') out.map1 = m;
        if (out.map1.empty()) out.stage1Op = 0;
    }
    return out;
}

void FogColorForBlend(int mode, const float levelFog[4], float out[4]) {
    float v = 0.f;
    bool level = false;
    switch (mode) {
        case kBlendNone: case kBlendTranslucent: case kBlendDestTranslucent: level = true; break;
        case kBlendModulate: case kBlendFilter: case kBlendModulate2x: v = 1.f; break;
        default: v = 0.f; break;                  // alpha, add, subtract, destalpha...
    }
    for (int i = 0; i < 3; ++i) out[i] = level ? levelFog[i] : v;
    out[3] = 1.f;
}

uint64_t BlendModeState(int mode) {
    // Translated one for one from the D3D render states D3Dev.dll's state
    // setter issues for each value; the table is in Docs/Reference/Particles.md.
    switch (mode) {
        case kBlendNone: return 0;
        case kBlendAlpha:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
        case kBlendAdd:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
        case kBlendModulate:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);
        case kBlendFilter:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR);
        case kBlendTranslucent:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                         BGFX_STATE_BLEND_INV_SRC_ALPHA);
        case kBlendInvModulate:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_COLOR);
        case kBlendSubtract:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
                   BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_SUB);
        case kBlendRevSubtract:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
                   BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_REVSUB);
        case kBlendDestTranslucent:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_ALPHA,
                                         BGFX_STATE_BLEND_INV_DST_ALPHA);
        case kBlendDestAlpha:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_ALPHA, BGFX_STATE_BLEND_ONE);
        case kBlendModulate2x:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_SRC_COLOR);
        default:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    }
}

} // namespace painful
