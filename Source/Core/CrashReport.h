// A stack for a crash, rather than a silent exit code.
#pragma once

namespace painful {

// Installs the process-wide handler. Call once, first thing in main().
//
// `name` labels the report - "PainfulEngine" or "PainfulTools" - because both
// executables write to the same folder.
void InstallCrashHandler(const char* name);

} // namespace painful
