#pragma once
#include <string>

namespace painful {

class Console;

// The console's own commands for painful_config.ini, in the style of the
// scripts' `pkweapons` and `fov`: one lowercase word, the key behind a `pf`,
// no script touched - the line is claimed here before Hud_OnConsoleCommand
// sees it. Docs/Reference/Console.md, "pf".
//
//   pfhudaspect          its usage line and current value, as `fov` answers
//   pfhudaspect 2        sets it, writes the file, the loop applies it next frame
//
// Tab lists them. Returns false when the line is not one of these, so the
// scripts get it.
bool ConfigCommand(const std::string& line, Console& console);
// Tab on a line starting with pf: completes it the way Console:OnPrompt
// completes the scripts' commands. False when the line is not ours.
bool ConfigTab(const std::string& text, Console& console);

} // namespace painful
