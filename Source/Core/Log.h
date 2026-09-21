#pragma once
#include <string>

// One log: stdout always, plus painful.log beside the executable once LogOpen
// has been called. The tools never call LogOpen and print to the console only.
//
// Categories are line prefixes, so grep serves in place of separate files. A
// crash gets painful_crash.log of its own: it is the file worth handing to
// someone else, and it is written while the process is dying.
//
// The emitters are C variadic rather than variadic templates so the format
// string can be annotated: GCC and Clang then reject a mismatched argument at
// the call site, and MSVC's analysis does the same. A template hides the format
// from both, which is how "%s" with an int compiles silently.
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

// How much reaches the log. PAINFUL_LOG=warn|info|trace picks it; the default
// is trace, because the [stub] lines it carries are the native work queue and
// Docs/Plan.md measures progress by them.
enum class LogLevel { kWarn, kInfo, kTrace };

// Opens painful.log in `dir`. Safe to call once; later calls are ignored.
void LogOpen(const std::string& dir);
void LogClose();
// One line, already formatted and prefixed.
void LogLine(const char* prefix, const char* text);

void LogWarn(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
void LogInfo(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
// The high-volume diagnostic stream: instrumented stubs and the traces. First
// thing dropped when the level is turned down.
void LogTrace(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
// A script error - the scripts' own print output goes through LogInfo with a
// "lua:" tag, so an error and a Game:Print do not read alike. And a shader the
// loader could not resolve. Their own tags rather than their own files.
void LogScript(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);
void LogShader(PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(1, 2);

} // namespace painful
