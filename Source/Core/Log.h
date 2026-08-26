#pragma once
#include <cstdio>

// Deliberately tiny. Swap the body for a real sink later; nothing else changes.
namespace painful {

template <typename... Args>
inline void LogInfo(const char* fmt, Args... args) {
    std::printf(fmt, args...);
    std::printf("\n");
}

template <typename... Args>
inline void LogWarn(const char* fmt, Args... args) {
    std::printf("warning: ");
    std::printf(fmt, args...);
    std::printf("\n");
}

} // namespace painful
