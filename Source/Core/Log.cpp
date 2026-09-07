#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace painful {

namespace {

FILE* g_main = nullptr;
FILE* g_script = nullptr;
FILE* g_shader = nullptr;

FILE* OpenLog(const std::string& dir, const char* name) {
    const std::string path = dir.empty() ? std::string(name) : dir + "/" + name;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return nullptr;
    const std::time_t now = std::time(nullptr);
    char when[64] = {};
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(f, "PainfulEngine %s\n", when);
    std::fflush(f);
    return f;
}

void Write(FILE* f, const char* prefix, const char* text) {
    if (!f) return;
    std::fputs(prefix, f);
    std::fputs(text, f);
    std::fputc('\n', f);
    std::fflush(f);   // a crash must not lose the last lines
}

}  // namespace

void LogOpen(const std::string& dir) {
    if (g_main) return;
    g_main = OpenLog(dir, "painful.log");
    g_script = OpenLog(dir, "painful_script.log");
    g_shader = OpenLog(dir, "painful_shader.log");
}

void LogClose() {
    if (g_main) std::fclose(g_main);
    if (g_script) std::fclose(g_script);
    if (g_shader) std::fclose(g_shader);
    g_main = g_script = g_shader = nullptr;
}

void LogLine(LogSink sink, const char* prefix, const char* text) {
    std::fputs(prefix, stdout);
    std::fputs(text, stdout);
    std::fputc('\n', stdout);
    Write(g_main, prefix, text);
    if (sink == LogSink::kScript) Write(g_script, prefix, text);
    if (sink == LogSink::kShader) Write(g_shader, prefix, text);
}

namespace {
// One line, truncated rather than wrapped: anything longer than this is a
// dump, and a dump belongs in a report.
void Emit(LogSink sink, const char* prefix, const char* fmt, va_list args) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, args);
    LogLine(sink, prefix, buf);
}
}  // namespace

#define PAINFUL_EMIT(sink, prefix) \
    va_list args;                  \
    va_start(args, fmt);           \
    Emit(sink, prefix, fmt, args); \
    va_end(args)

void LogInfo(const char* fmt, ...) { PAINFUL_EMIT(LogSink::kMain, ""); }
void LogWarn(const char* fmt, ...) { PAINFUL_EMIT(LogSink::kMain, "warning: "); }
void LogScript(const char* fmt, ...) { PAINFUL_EMIT(LogSink::kScript, "warning: "); }
void LogShader(const char* fmt, ...) { PAINFUL_EMIT(LogSink::kShader, ""); }

#undef PAINFUL_EMIT

}  // namespace painful
