// PainfulEngine - open reimplementation of PainEngine (Painkiller, 2004).
//
// The entry point, and nothing else. Launched with no arguments it finds the
// game data beside itself and opens the first campaign level; `game` names a
// data root and a level explicitly. Everything that reports on the data rather
// than playing it is a separate executable, PainfulTools.

#include "GameApp.h"

#include "Core/AppPaths.h"
#include "Core/CrashReport.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"

#include <string>

using namespace painful;

static int Usage() {
    LogInfo("PainfulEngine\n");
    LogInfo("  PainfulEngine                                 find the game data, open the menu");
    LogInfo("  PainfulEngine game <DataRoot> [level] [flags]  play a named level (none: the menu)");
    LogInfo("");
    LogInfo("  flags: --shot <file>   capture one frame to a .tga and exit");
    LogInfo("         --exec <lua>    run a Lua chunk once the world is up");
    LogInfo("         -dev            the debug overlay and the F1-F4 toggles");
    LogInfo("         -mp             multiplayer movement (MultiPlayerMove tweaks)");
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
    // No level: the original's own start. Game:Init(true), the main menu up
    // over an empty world, and the map screen loads whatever is chosen.
    return GameCmd(root.c_str(), "", exePath, "", nullptr, false, false);
}

int main(int argc, char** argv) {
    InstallCrashHandler("PainfulEngine");
    if (argc < 2) return DefaultRun(argv[0]);
    if (std::string(argv[1]) != "game" || argc < 3) return Usage();

    MountRoot(argv[2]);
    std::string shot;
    const char* exec = nullptr;
    bool devUI = false;
    bool mpMove = false;
    // argv[3] is the level name unless it is a flag: `game <root> -dev` has
    // no level and boots to the menu.
    const bool hasLevel = argc >= 4 && argv[3][0] != '-';
    for (int i = hasLevel ? 4 : 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (arg == "--exec" && i + 1 < argc) exec = argv[++i];
        else if (arg == "-dev" || arg == "--dev") devUI = true;
        else if (arg == "-mp" || arg == "--mp") mpMove = true;
    }
    // `game <root>` alone boots to the menu like a plain launch; a level name
    // goes straight into it, which is what the probes and the screenshots use.
    return GameCmd(argv[2], hasLevel ? argv[3] : "", argv[0], shot, exec, devUI, mpMove);
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
