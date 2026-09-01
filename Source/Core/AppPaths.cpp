#include "AppPaths.h"

#include "FileSystem.h"
#include "Log.h"

#include <filesystem>

namespace painful {

// Locates the game data next to the executable. The engine is meant to sit in
// the game's Bin folder, like the original Painkiller.exe, so the data root is
// a sibling of the exe's directory.
//
// Data, and ONLY Data. A loose tree of unpacked assets is reference material
// for reading formats by hand, not something the engine may run against: the
// moment it is a fallback, a missing or misnamed .pak stops being an error and
// silently becomes a different code path, and a diagnostic can pass against
// files the shipped game never reads. If Data is not there, say so and stop.
std::string FindDataRoot(const char* exePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(exePath, ec).parent_path();
    for (int depth = 0; depth < 2 && !dir.empty(); ++depth, dir = dir.parent_path()) {
        fs::path candidate = dir / "Data";
        if (fs::exists(candidate / "Levels.pak", ec) ||
            fs::exists(candidate / "Levels", ec))
            return candidate.string();
    }
    return {};
}

const char* MountRoot(const char* dataRoot) {
    const size_t n = FileSystem::Get().MountData(dataRoot);
    if (n) LogInfo("mounted %zu .pak archives from %s", n, dataRoot);
    return dataRoot;
}

void MountForPath(const char* anyPath, const char* exePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(anyPath, ec).parent_path();
    for (int depth = 0; depth < 8 && dir.has_relative_path(); ++depth) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".pak") {
                MountRoot(dir.string().c_str());
                return;
            }
        }
        dir = dir.parent_path();
    }
    const std::string root = FindDataRoot(exePath);
    if (!root.empty()) MountRoot(root.c_str());
}

std::string ShaderDirFor(const char* exePath) {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::absolute(exePath, ec);
    if (!ec) {
        std::filesystem::path beside = exe.parent_path() / "Shaders";
        if (std::filesystem::exists(beside, ec)) return beside.string();
    }
    return "Shaders";   // fall back to the working directory
}


std::string MapNameWithoutExtension(const std::string& mapFile) {
    size_t dot = mapFile.find_last_of('.');
    return dot == std::string::npos ? mapFile : mapFile.substr(0, dot);
}


}  // namespace painful
