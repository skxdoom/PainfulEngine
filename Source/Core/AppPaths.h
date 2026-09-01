#pragma once

// Locating the game's data and the executable's own resources.
//
// Shared by both executables: PainfulEngine finds a data root on a bare launch,
// PainfulTools mounts whichever one a command names. Nothing here knows about
// rendering or gameplay, so it sits in Core beside the VFS it mounts into.
// The reasoning behind each rule is with the code, in AppPaths.cpp.

#include <string>

namespace painful {

// The game data next to the executable. Empty when there is none.
std::string FindDataRoot(const char* exePath);

// Mounts a data root's .pak archives. Returns dataRoot, so it composes.
const char* MountRoot(const char* dataRoot);

// Mounts the data root a bare file path points into.
void MountForPath(const char* anyPath, const char* exePath);

// Where the compiled shaders sit, relative to the executable.
std::string ShaderDirFor(const char* exePath);

std::string MapNameWithoutExtension(const std::string& mapFile);

}  // namespace painful
