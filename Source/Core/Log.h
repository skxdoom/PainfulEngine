#pragma once
#include <cstdio>
#include <string>

// Logging: stdout always, plus the files beside the executable once
// LogOpen has been called - painful.log for everything, painful_script.log
// for script errors, painful_shader.log for the shader loader. The tools
// never call LogOpen and keep printing to the console only.
//
// The emitters are C variadic rather than variadic templates so the format
// string can be annotated: GCC and Clang then reject a mismatched argument at
// the call site, and MSVC's analysis does the same. A template hides the
// format from both, which is how "%s" with an int compiles silently.
#if defined(__GNUC__) || defined(__clang__)
#define PAINFUL_FORMAT_ATTR(fmtIndex, firstArg) \
    __attribute__((format(printf, fmtIndex, firstArg)))
#define PAINFUL_FORMAT_STRING(decl) decl
#else
#define PAINFUL_FORMAT_ATTR(fmtIndex, firstArg)
#if defined(_MSC_VER)
#include <sal.h>
#define PAINFUL_FORMAT_STRING(decl) _Printf_format_string_ decl
#else
#define PAINFUL_FORMAT_STRING(decl) decl
#endif
#endif

namespace painful {

enum class LogSink { kMain, kScript, kShader };

// Opens the three files in `dir`. Safe to call once; later calls are ignored.
void LogOpen(const std::string& dir);
void LogClose();
// One line to stdout and to the main log, and to the sink's own file too
// when the sink is not the main one.
void LogLine(LogSink sink, const char* prefix, const char* text);

void LogInfo(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
void LogWarn(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
// A script error: the main log and painful_script.log.
void LogScript(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
// The shader loader: the main log and painful_shader.log.
void LogShader(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);

}  // namespace painful
