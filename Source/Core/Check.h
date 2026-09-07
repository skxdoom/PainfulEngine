#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "Log.h"

// A defensive early return, made audible. An expression, so an existing guard
// becomes:
//
//     if (!PAINFUL_CHECK(e, "SetPosition: no entity %d", handle)) return;
//
// A failure logs once per call site, counts the rest, and carries on;
// ReportChecks() prints the tally. PAINFUL_ASSERT adds a Debug-build trap;
// PAINFUL_CHECK_BREAK=1 traps on the first failure in any configuration.
// Which guards are checks and which are ordinary answers:
// Docs/Reference/Diagnostics.md
namespace painful {

// One call site that has failed, with the first failure's message.
struct CheckFailure {
	const char* file = nullptr;
	int line = 0;
	const char* expr = nullptr;
	std::string detail;
	size_t count = 0;
};

// Logs the first failure at this site and counts the rest. Always returns
// false, so it reads as the failing branch of the condition it guards.
bool CheckFailed(const char* file, int line, const char* expr,
		PAINFUL_FORMAT_STRING(const char* fmt), ...) PAINFUL_FORMAT_ATTR(4, 5);

// Every site that has failed, in first-failure order.
const std::vector<CheckFailure>& CheckFailures();
// Total failures across all sites.
size_t CheckFailureCount();

// One block naming each failed site and its count, or a single clean line.
// The reports call it at the end of a run.
void ReportChecks();
void ResetChecks();

} // namespace painful

#if defined(_MSC_VER)
#define PAINFUL_TRAP() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define PAINFUL_TRAP() __builtin_trap()
#else
#define PAINFUL_TRAP() ((void)0)
#endif

// The condition is evaluated exactly once. The message is required - a check
// that cannot say what broke is not worth the branch. (It also keeps the macro
// off __VA_OPT__, which MSVC only offers under /Zc:preprocessor.)

#define PAINFUL_CHECK(cond, ...) \
	((cond) ? true \
			: ::painful::CheckFailed(__FILE__, __LINE__, #cond, __VA_ARGS__))

#ifdef NDEBUG
#define PAINFUL_ASSERT(cond, ...) PAINFUL_CHECK(cond, __VA_ARGS__)
#else
#define PAINFUL_ASSERT(cond, ...) \
	((cond) ? true : (PAINFUL_CHECK(cond, __VA_ARGS__), PAINFUL_TRAP(), false))
#endif
