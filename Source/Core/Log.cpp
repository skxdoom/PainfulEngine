#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace painful {

namespace {

FILE* g_file = nullptr;

// Read here with getenv rather than through Core/Debug, which reports a bad
// switch through Core/Check, which logs: the three would initialise each other.
// PAINFUL_LOG is in the switch table for `PainfulTools traces` to list.
LogLevel Level() {
	static const LogLevel level = [] {
		const char* v = std::getenv("PAINFUL_LOG");
		if (!v) return LogLevel::kTrace;
		if (std::strcmp(v, "warn") == 0) return LogLevel::kWarn;
		if (std::strcmp(v, "info") == 0) return LogLevel::kInfo;
		return LogLevel::kTrace;
	}();
	return level;
}

void Emit(LogLevel level, const char* prefix, const char* fmt, va_list args) {
	if (level > Level()) return;
	// One line, truncated rather than wrapped: anything longer than this is a
	// dump, and a dump belongs in a report.
	char buf[2048];
	std::vsnprintf(buf, sizeof buf, fmt, args);
	LogLine(prefix, buf);
}

} // namespace

void LogOpen(const std::string& dir) {
	if (g_file) return;
	const std::string path = dir.empty() ? std::string("painful.log") : dir + "/painful.log";
	g_file = std::fopen(path.c_str(), "w");
	if (!g_file) return;
	const std::time_t now = std::time(nullptr);
	char when[64] = {};
	std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	std::fprintf(g_file, "PainfulEngine %s\n", when);
	std::fflush(g_file);
}

void LogClose() {
	if (g_file) std::fclose(g_file);
	g_file = nullptr;
}

void LogLine(const char* prefix, const char* text) {
	std::fputs(prefix, stdout);
	std::fputs(text, stdout);
	std::fputc('\n', stdout);
	if (!g_file) return;
	std::fputs(prefix, g_file);
	std::fputs(text, g_file);
	std::fputc('\n', g_file);
	std::fflush(g_file); // a crash must not lose the last lines
}

#define PAINFUL_EMIT(level, prefix) \
	va_list args; \
	va_start(args, fmt); \
	Emit(level, prefix, fmt, args); \
	va_end(args)

void LogWarn(const char* fmt, ...) { PAINFUL_EMIT(LogLevel::kWarn, "warning: "); }
void LogInfo(const char* fmt, ...) { PAINFUL_EMIT(LogLevel::kInfo, ""); }
void LogTrace(const char* fmt, ...) { PAINFUL_EMIT(LogLevel::kTrace, ""); }
void LogScript(const char* fmt, ...) { PAINFUL_EMIT(LogLevel::kWarn, "script: "); }
void LogShader(const char* fmt, ...) { PAINFUL_EMIT(LogLevel::kWarn, "shader: "); }

#undef PAINFUL_EMIT

} // namespace painful
