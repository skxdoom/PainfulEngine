#include "CrashReport.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

namespace painful {

namespace {

const char* g_name = "PainfulEngine";

// Beside the executable, where pk.log already goes. Built here rather than
// through AppPaths because a handler cannot rely on argv having survived.
std::string CrashLogPath() {
    char exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string path(exe, n);
    const size_t slash = path.find_last_of("\/");
    path = (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
    return path + g_name + "-crash.log";
}

// Both, always. stderr is what a console run and the headless tools show; the
// file is the only one a double-clicked GUI build leaves behind, and that is
// the case a bug report comes from.
void Say(FILE* file, const char* fmt, ...) {
    va_list args;
    for (FILE* out : {stderr, file}) {
        if (out == nullptr) continue;
        va_start(args, fmt);
        std::vfprintf(out, fmt, args);
        va_end(args);
    }
}

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
        case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
        case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "datatype misalignment";
        default:                              return "exception";
    }
}

// A frame, symbolised where a .pdb is there to do it with.
//
// Release builds carry no symbols, so the fallback matters: module + offset is
// still enough to place a frame against a .pdb kept from the same build.
void Frame(FILE* file, HANDLE proc, int index, DWORD64 pc) {
    char storage[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(storage);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 500;
    DWORD64 symOffset = 0;

    if (SymFromAddr(proc, pc, &symOffset, sym)) {
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineOffset = 0;
        if (SymGetLineFromAddr64(proc, pc, &lineOffset, &line))
            Say(file, "  %2d  %s  (%s:%lu)\n", index, sym->Name, line.FileName, line.LineNumber);
        else
            Say(file, "  %2d  %s + 0x%llx\n", index, sym->Name, symOffset);
        return;
    }

    IMAGEHLP_MODULE64 mod = {};
    mod.SizeOfStruct = sizeof(mod);
    if (SymGetModuleInfo64(proc, pc, &mod))
        Say(file, "  %2d  %s + 0x%llx\n", index, mod.ModuleName, pc - mod.BaseOfImage);
    else
        Say(file, "  %2d  0x%016llx\n", index, pc);
}

LONG WINAPI Handler(EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD& rec = *ep->ExceptionRecord;

    FILE* file = std::fopen((CrashLogPath()).c_str(), "w");
    const std::time_t now = std::time(nullptr);
    char when[64] = {};
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    Say(file, "\n*** %s crashed: %s (0x%08lx) at %p, %s\n", g_name,
        ExceptionName(rec.ExceptionCode), rec.ExceptionCode, rec.ExceptionAddress, when);
    // For an access violation the record says which way and where, and "read
    // from 0000000000000000" names a null dereference on its own.
    if (rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec.NumberParameters >= 2) {
        const ULONG_PTR kind = rec.ExceptionInformation[0];
        Say(file, "*** %s address 0x%016llx\n",
            kind == 0 ? "read from" : kind == 1 ? "write to" : "execute at",
            static_cast<unsigned long long>(rec.ExceptionInformation[1]));
    }

    const HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    // The walk needs a writable copy: StackWalk64 advances the context it is
    // given, and the one in EXCEPTION_POINTERS belongs to the OS.
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 frame = {};
#if defined(_M_X64)
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    frame.AddrPC.Offset = ctx.Pc;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrStack.Offset = ctx.Sp;
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
#error "CrashReport: no stack layout for this architecture"
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(machine, proc, GetCurrentThread(), &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;
        Frame(file, proc, i, frame.AddrPC.Offset);
    }

    SymCleanup(proc);
    if (file != nullptr) {
        std::fclose(file);
        std::fprintf(stderr, "*** written to %s\n", CrashLogPath().c_str());
    }
    std::fflush(stderr);
    // Let the process die rather than limp on: the state that produced this is
    // not one to keep running in.
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallCrashHandler(const char* name) {
    if (name != nullptr && name[0] != '\0') g_name = name;
    SetUnhandledExceptionFilter(Handler);
}

} // namespace painful

#else   // !_WIN32

namespace painful {
// POSIX would be a sigaction for SIGSEGV/SIGBUS/SIGFPE plus backtrace(3).
// Nothing yet, and a no-op is the honest state - see Docs/Status.md.
void InstallCrashHandler(const char*) {}
} // namespace painful

#endif
