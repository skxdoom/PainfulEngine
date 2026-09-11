#pragma once

// The PORT's version, and the name it puts on the window and at the head of
// painful.log. Bump the two numbers; everything else composes from them.
//
// THIS IS NOT THE VERSION THE SCRIPTS ASK ABOUT. Game:Init refuses to run
// unless GetEngineVersionString answers exactly "1.4" (Script/Natives.cpp) -
// that is the original engine's own number, a gate the data depends on, and it
// must never follow this one.
//
// Macros so the pieces paste into one string literal, and so the .rc files can
// include this too - rc.exe runs the same preprocessor but cannot read C++,
// which is what RC_INVOKED guards below.
#define PAINFUL_VERSION_MAJOR 0
#define PAINFUL_VERSION_MINOR 6
#define PAINFUL_VERSION_STAGE "alpha"

#define PAINFUL_STRINGIFY2(x) #x
#define PAINFUL_STRINGIFY(x) PAINFUL_STRINGIFY2(x)
#define PAINFUL_VERSION \
	PAINFUL_STRINGIFY(PAINFUL_VERSION_MAJOR) "." PAINFUL_STRINGIFY(PAINFUL_VERSION_MINOR)

// The window, the taskbar and the process list: the plain product name, with no
// version in it.
#define PAINFUL_APP_NAME "Painful Engine"

// The log header and anywhere else diagnosing a build: "Painful Engine 0.5
// alpha". A log is read after the fact and has to say which build wrote it,
// which is exactly what the window leaves off.
#define PAINFUL_BUILD_ID PAINFUL_APP_NAME " " PAINFUL_VERSION " " PAINFUL_VERSION_STAGE

// What Explorer's Details tab and Task Manager's process list read. The
// FILEVERSION resource wants four raw numbers, not a string.
#define PAINFUL_VERSION_COMMAS PAINFUL_VERSION_MAJOR, PAINFUL_VERSION_MINOR, 0, 0
#define PAINFUL_COPYRIGHT "(c) Dmitry Karpukhin"

#ifndef RC_INVOKED
namespace painful {

// The same strings for code that would rather not use a macro.
inline constexpr const char* kAppName = PAINFUL_APP_NAME;
inline constexpr const char* kVersion = PAINFUL_VERSION;
inline constexpr const char* kBuildId = PAINFUL_BUILD_ID;

} // namespace painful
#endif
