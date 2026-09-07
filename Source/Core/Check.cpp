#include "Check.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace painful {

namespace {

std::vector<CheckFailure> g_failures;
size_t g_total = 0;

// PAINFUL_CHECK_BREAK=1: trap at the first failure, in any configuration.
// The tally tells you a check fired; this tells you which frame did it.
bool BreakOnFailure() {
    static const bool on = std::getenv("PAINFUL_CHECK_BREAK") != nullptr;
    return on;
}

// The file name alone. __FILE__ is an absolute path under MSVC and the tally
// is unreadable with twelve of them in it.
const char* BaseName(const char* path) {
    const char* slash = std::strrchr(path, '/');
    const char* back = std::strrchr(path, '\\');
    const char* last = slash > back ? slash : back;
    return last ? last + 1 : path;
}

CheckFailure* FindSite(const char* file, int line) {
    for (CheckFailure& f : g_failures)
        if (f.line == line && f.file == file) return &f;
    return nullptr;
}

}  // namespace

bool CheckFailed(const char* file, int line, const char* expr, const char* fmt, ...) {
    ++g_total;
    if (CheckFailure* seen = FindSite(file, line)) {
        ++seen->count;
        return false;
    }

    char detail[1024];
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(detail, sizeof detail, fmt, args);
        va_end(args);
    } else {
        std::snprintf(detail, sizeof detail, "%s", expr);
    }

    g_failures.push_back({file, line, expr, detail, 1});
    LogWarn("check failed: %s (%s:%d)", detail, BaseName(file), line);
    if (BreakOnFailure()) PAINFUL_TRAP();
    return false;
}

const std::vector<CheckFailure>& CheckFailures() { return g_failures; }

size_t CheckFailureCount() { return g_total; }

void ReportChecks() {
    if (g_failures.empty()) {
        LogInfo("checks: none failed");
        return;
    }
    LogInfo("checks failed: %zu distinct, %zu total", g_failures.size(), g_total);
    for (const CheckFailure& f : g_failures)
        LogInfo("  %6zu  %s:%d  %s", f.count, BaseName(f.file), f.line, f.detail.c_str());
}

void ResetChecks() {
    g_failures.clear();
    g_total = 0;
}

}  // namespace painful
