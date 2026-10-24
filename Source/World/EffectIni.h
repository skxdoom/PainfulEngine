#pragma once
#include <map>
#include <string>

namespace painful {

// The effect definition files - Scripts/Decals, Scripts/Trails - read the way
// ConfigFile::GetString reads them: "section.key", both folded to lower case.
std::map<std::string, std::string> ParseIni(const std::string& text);

// The material-script blend names (FUN_100973a0), -1 for an unknown one.
int BlendModeByName(const std::string& name);

} // namespace painful
