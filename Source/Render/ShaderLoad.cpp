#include "ShaderLoad.h"

#include "../Core/Common.h"
#include "../Core/Log.h"

#include <vector>

#include "ShaderBlobs.h"        // generated: the arrays and PAINFUL_SHADER_LIST

namespace painful {

namespace {

// One row per shader, each holding every backend's bytecode.
// bgfx::createEmbeddedShader picks the row's entry for the running renderer.
const bgfx::EmbeddedShader kEmbeddedShaders[] = {
#define X(n) BGFX_EMBEDDED_SHADER(n),
    PAINFUL_SHADER_LIST(X)
#undef X
    BGFX_EMBEDDED_SHADER_END()
};

} // namespace

bgfx::ShaderHandle LoadShader(const std::string& shaderDir, const char* name) {
    if (!shaderDir.empty()) {
        std::vector<uint8_t> data;
        if (ReadFile(shaderDir + "/" + name + ".bin", data) && !data.empty()) {
            // Said once, because an override is a development state and a build
            // that renders oddly should say why before anyone goes looking.
            static bool said = false;
            if (!said) { said = true; LogInfo("shaders: %s (on-disk override)", shaderDir.c_str()); }
            return bgfx::createShader(bgfx::copy(data.data(), uint32_t(data.size())));
        }
    }
    const bgfx::ShaderHandle h =
        bgfx::createEmbeddedShader(kEmbeddedShaders, bgfx::getRendererType(), name);
    // Only reachable when the running backend has no array in the table - a
    // renderer bgfx chose that this build was not compiled for.
    if (!bgfx::isValid(h))
        LogWarn("no %s for %s", name, bgfx::getRendererName(bgfx::getRendererType()));
    return h;
}

} // namespace painful
