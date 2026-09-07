#include "EngineBoot.h"

#include "../Core/AppPaths.h"
#include "../Core/Debug.h"
#include "../Core/Log.h"
#include "../Render/Camera.h"
#include <string>

namespace painful {

bool EngineBoot::Init(const std::string& dataRoot, const char* exePath, const char* title,
                      int width, int height) {
    root_ = dataRoot;
    shaderDir_ = ShaderDirFor(exePath);

    if (!window_.Open(title, width, height)) return false;
    if (!renderer_.Init(window_)) return false;
    LogInfo("renderer: %s", renderer_.BackendName().c_str());
    // Which diagnostic switches this run had on, so a log explains its own odd
    // behaviour. PainfulTools traces lists them all.
    if (const std::string on = DebugActive(); !on.empty()) LogInfo("switches: %s", on.c_str());

    textures_.Init(root_ + "/Textures");
    if (!shaders_.LoadDirectory(root_ + "/Shaders/Scripts"))
        for (const std::string& e : shaders_.errors()) LogWarn("%s", e.c_str());
    emitters_.Init(root_ + "/Scripts");

    physics_.SetProbeRadius(kCameraRadius);
    // The player's own pusher: the widest of the four spheres the shape factory
    // builds for BodyTypes.Player at bodyScale 1.0 (Engine.dll 0x101b3e20).
    physics_.SetPawnProbeRadius(0.4f);

    debugLinesReady_ = debugLines_.Init(shaderDir_);
    return true;
}

}  // namespace painful
