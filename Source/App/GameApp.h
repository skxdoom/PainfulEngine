#pragma once

// The game itself: the script-driven run.
//
// Game:LoadLevel reads the level and creates its entities through the native
// API, Game:OnPlay creates the player, and the frame loop ticks the script
// layer against the engine subsystems. This is what launching the executable
// does; the hand-driven loader behind PainfulTools' `run` is a diagnostic and
// shares nothing with it but the renderer underneath.

#include <string>

namespace painful {

// shotPath: capture one frame to a .tga and exit. exec: a Lua chunk run once
// the world is up, or null. devUI: the -dev launch flag - the debug overlay
// and the F1-F4/F6 toggles, all off in a normal launch.
int GameCmd(const char* dataRoot, const char* levelName, const char* exePath,
            const std::string& shotPath, const char* exec, bool devUI);

}  // namespace painful
