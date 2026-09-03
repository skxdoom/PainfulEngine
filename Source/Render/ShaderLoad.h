// The compiled shaders: embedded in the executable, overridable from disk.
#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace painful {

// `name` is the bare shader name - "vs_world", not "vs_world.bin".
//
// A .bin of that name in shaderDir wins when it is there, which is the
// development path: recompile one shader, drop it beside the executable, no
// rebuild. Otherwise the copy built into the binary for the running backend.
bgfx::ShaderHandle LoadShader(const std::string& shaderDir, const char* name);

} // namespace painful
