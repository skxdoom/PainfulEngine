#include "Log.h"

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

}  // namespace painful
