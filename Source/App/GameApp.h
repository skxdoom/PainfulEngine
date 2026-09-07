#pragma once

// The game itself: the script-driven run. One function, not a class - the
// header used to imply otherwise.
//
// Game:LoadLevel reads the level and creates its entities through the native
// API, Game:OnPlay creates the player, and the frame loop ticks the script
// layer against the engine subsystems. This is what launching the executable
// does; the hand-driven loader behind PainfulTools' `run` is a diagnostic, and
// the two now share their boot through Game/EngineBoot.h - the window, the
// device and the caches that do not belong to any one level.
//
// The frame loop itself is still one long body. Its phases (input, the debug
// keys, the tick chain, drawing) are not split out because none of them can be
// exercised without a person at the keyboard, and an untestable extraction of
// recovered frame order is a bad trade.

#include <string>

namespace painful {

// shotPath: capture one frame to a .tga and exit. exec: a Lua chunk run once
// the world is up, or null. devUI: the -dev launch flag - the debug overlay,
// the F1-F4 toggles and noclip, and it puts the SCRIPTS into their developer
// build too (debugMarek, IsFinalBuild). PAINFUL_DEV is the same switch, and
// there is no way to turn any of it on mid-run: it is a build, not an option.
int GameCmd(const char* dataRoot, const char* levelName, const char* exePath,
		const std::string& shotPath, const char* exec, bool devUI, bool mpMove);

} // namespace painful
