#pragma once
#include <string>

// The PAINFUL_* diagnostic switches, in one table.
//
// They were 40 scattered getenv calls: undiscoverable as a set, inconsistently
// cached, and a typo read as "off" with no complaint. The table is the list
// `PainfulTools traces` prints; an accessor whose name is not in it, or whose
// kind disagrees with it, is a check failure rather than a silent default.
//
// The environment is read once, on first access. Changing a variable after
// that needs a restart, which is what every call site already assumed by
// caching in a `static const`.
namespace painful {

bool        DebugFlag(const char* name);
int         DebugInt(const char* name, int fallback);
float       DebugFloat(const char* name, float fallback);
// Null when unset, so a caller can tell "absent" from "empty".
const char* DebugText(const char* name);

// Every switch: name, kind, what it does, and what it is set to now.
void DebugList();
// Just the ones that are set, one line - for the head of a log or a report.
// Empty when none are.
std::string DebugActive();

}  // namespace painful
