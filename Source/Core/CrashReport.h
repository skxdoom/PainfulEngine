// A stack for a crash, rather than a silent exit code.
#pragma once

namespace painful {

// Installs the process-wide handler. Call once, first thing in main().
//
// `name` labels the report - "PainfulEngine" or "PainfulTools" - because both
// executables write to the same folder.
void InstallCrashHandler(const char* name);

// The calling thread's stack, to stderr, under `label`. For the case that has
// not crashed yet: an assert names the rule that was broken, this names who
// broke it. Costs a symbol load, so it is for diagnostics, not a hot path.
void LogStackHere(const char* label);

} // namespace painful
