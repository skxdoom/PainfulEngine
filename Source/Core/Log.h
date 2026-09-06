#pragma once
#include <cstdio>
#include <string>

// Logging: stdout always, plus the files beside the executable once
// LogOpen has been called - painful.log for everything, painful_script.log
// for script errors, painful_shader.log for the shader loader. The tools
// never call LogOpen and keep printing to the console only.
namespace painful {

enum class LogSink { kMain, kScript, kShader };

// Opens the three files in `dir`. Safe to call once; later calls are ignored.
void LogOpen(const std::string& dir);
void LogClose();
// One line to stdout and to the main log, and to the sink's own file too
// when the sink is not the main one.
void LogLine(LogSink sink, const char* prefix, const char* text);

namespace detail {
template <typename... Args>
inline void Emit(LogSink sink, const char* prefix, const char* fmt, Args... args) {
    char buf[2048];
    std::snprintf(buf, sizeof buf, fmt, args...);
    LogLine(sink, prefix, buf);
}
inline void Emit(LogSink sink, const char* prefix, const char* fmt) {
    LogLine(sink, prefix, fmt);
}
}  // namespace detail

template <typename... Args>
inline void LogInfo(const char* fmt, Args... args) {
    detail::Emit(LogSink::kMain, "", fmt, args...);
}

template <typename... Args>
inline void LogWarn(const char* fmt, Args... args) {
    detail::Emit(LogSink::kMain, "warning: ", fmt, args...);
}

// A script error: the main log and painful_script.log.
template <typename... Args>
inline void LogScript(const char* fmt, Args... args) {
    detail::Emit(LogSink::kScript, "warning: ", fmt, args...);
}

// The shader loader: the main log and painful_shader.log.
template <typename... Args>
inline void LogShader(const char* fmt, Args... args) {
    detail::Emit(LogSink::kShader, "", fmt, args...);
}

} // namespace painful
