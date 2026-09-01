// PainfulEngine - open reimplementation of PainEngine (Painkiller, 2004).
//
// The entry point, and nothing else. Launched with no arguments it finds the
// game data beside itself and opens the first campaign level; `game` names a
// data root and a level explicitly. Everything that reports on the data rather
// than playing it is a separate executable, PainfulTools.

#include "GameApp.h"

#include "Core/AppPaths.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"

#include <string>

using namespace painful;

static int Usage() {
    LogInfo("PainfulEngine\n");
    LogInfo("  PainfulEngine                                 find the game data and play");
    LogInfo("  PainfulEngine game <DataRoot> [level] [flags]  play a named level");
    LogInfo("");
    LogInfo("  flags: --shot <file>   capture one frame to a .tga and exit");
    LogInfo("         --exec <lua>    run a Lua chunk once the world is up");
    LogInfo("");
    LogInfo("  The level reports and the free-camera viewer are in PainfulTools.");
    return 1;
}

// Double-click launch: no arguments, so find the data ourselves and open the
// first campaign level. The [ ] keys cycle through every level from there.
static int DefaultRun(const char* exePath) {
    const std::string root = FindDataRoot(exePath);
    if (root.empty()) {
        LogInfo("no game data found. Place PainfulEngine.exe in the game's Bin");
        LogInfo("folder, next to the Data directory with the .pak archives.");
        return 2;
    }
    MountRoot(root.c_str());
    std::string level = "C1L1_Cathedral";
    if (!FileSystem::Get().IsDirectory(root + "/Levels/" + level)) {
        for (const DirEntry& entry : FileSystem::Get().List(root + "/Levels")) {
            if (entry.isDirectory) { level = entry.name; break; }
        }
    }
    return GameCmd(root.c_str(), level.c_str(), exePath, "", nullptr);
}

int main(int argc, char** argv) {
    if (argc < 2) return DefaultRun(argv[0]);
    if (std::string(argv[1]) != "game" || argc < 3) return Usage();

    MountRoot(argv[2]);
    std::string shot;
    const char* exec = nullptr;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (arg == "--exec" && i + 1 < argc) exec = argv[++i];
    }
    return GameCmd(argv[2], argc >= 4 ? argv[3] : "C1L1_Cathedral", argv[0], shot, exec);
}

#ifdef _WIN32
// The executable builds for the GUI subsystem so double-clicking it opens no
// console window. When launched FROM a console, attach to it so the CLI
// commands still print. __argc/__argv are populated by the CRT.
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Keep stdout/stderr as-is when the parent already redirected them (a
    // pipe or a file); rebinding to CONOUT$ would steal that output.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool redirected = out != nullptr && out != INVALID_HANDLE_VALUE;
    if (AttachConsole(ATTACH_PARENT_PROCESS) && !redirected) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    return main(__argc, __argv);
}
#endif
