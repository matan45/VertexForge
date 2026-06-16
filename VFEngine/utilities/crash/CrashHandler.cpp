#include "CrashHandler.hpp"

// Pulls in spdlog (for the on-disk log flush below). Included before <windows.h>
// to preserve the project's header-ordering conventions. Relative path because
// the Utilities project does not put VFEngine/utilities on its own include path.
#include "../print/Log.hpp"

#include <windows.h>
#include <DbgHelp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

// Belt-and-suspenders: also emit a linker directive so dbghelp is pulled in even
// if a consuming target forgets the premake `links { "dbghelp" }`.
#pragma comment(lib, "dbghelp.lib")

namespace util
{
    namespace
    {
        std::atomic<bool> installed{false};
        LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;

        // Fixed buffer (no heap interaction at crash time, when the heap may be
        // corrupt). A benign race on concurrent writers only ever yields a
        // slightly stale breadcrumb, which is acceptable for a diagnostic hint.
        constexpr size_t kContextCapacity = 1024;
        char crashContext[kContextCapacity] = {0};

        const char* exceptionLabel(DWORD code)
        {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
                case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
                case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
                case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
                case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
                case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
                case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
                default:                                 return "UNKNOWN";
            }
        }

        void writeStackTrace(std::FILE* out, CONTEXT* context)
        {
            HANDLE process = GetCurrentProcess();
            HANDLE thread = GetCurrentThread();

            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            SymInitialize(process, nullptr, TRUE);

            STACKFRAME64 frame = {};
            frame.AddrPC.Offset = context->Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context->Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context->Rsp;
            frame.AddrStack.Mode = AddrModeFlat;

            alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 512] = {0};
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 512;

            for (int i = 0; i < 128; ++i)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, context,
                                 nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                    break;
                if (frame.AddrPC.Offset == 0)
                    break;

                DWORD64 addr = frame.AddrPC.Offset;

                char moduleName[MAX_PATH] = "<unknown>";
                DWORD64 moduleBase = SymGetModuleBase64(process, addr);
                if (moduleBase)
                {
                    char modPath[MAX_PATH];
                    if (GetModuleFileNameA(reinterpret_cast<HMODULE>(moduleBase), modPath, MAX_PATH))
                    {
                        const char* base = strrchr(modPath, '\\');
                        strncpy_s(moduleName, base ? base + 1 : modPath, _TRUNCATE);
                    }
                }

                DWORD64 displacement = 0;
                if (SymFromAddr(process, addr, &displacement, symbol))
                {
                    IMAGEHLP_LINE64 line = {};
                    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                    DWORD lineDisp = 0;
                    if (SymGetLineFromAddr64(process, addr, &lineDisp, &line))
                    {
                        std::fprintf(out, "  [%2d] %s!%s + 0x%llx  (%s:%lu)\n", i, moduleName,
                                     symbol->Name, static_cast<unsigned long long>(displacement),
                                     line.FileName, line.LineNumber);
                    }
                    else
                    {
                        std::fprintf(out, "  [%2d] %s!%s + 0x%llx\n", i, moduleName, symbol->Name,
                                     static_cast<unsigned long long>(displacement));
                    }
                }
                else
                {
                    std::fprintf(out, "  [%2d] %s!0x%llx\n", i, moduleName,
                                 static_cast<unsigned long long>(addr));
                }
            }

            SymCleanup(process);
        }

        LONG WINAPI vfCrashFilter(EXCEPTION_POINTERS* ep)
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char stamp[32];
            std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth,
                          st.wDay, st.wHour, st.wMinute, st.wSecond);

            std::error_code ec;
            std::filesystem::create_directories("crashes", ec);

            char dmpPath[MAX_PATH];
            char txtPath[MAX_PATH];
            std::snprintf(dmpPath, sizeof(dmpPath), "crashes/crash_%s.dmp", stamp);
            std::snprintf(txtPath, sizeof(txtPath), "crashes/crash_%s.txt", stamp);

            // --- minidump ---
            HANDLE hFile = CreateFileA(dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                MINIDUMP_EXCEPTION_INFORMATION mei{};
                mei.ThreadId = GetCurrentThreadId();
                mei.ExceptionPointers = ep;
                mei.ClientPointers = FALSE;

                // Stacks + indirectly-referenced heap + thread/module info. Enough
                // to inspect engine/script state near the fault without the size of
                // a full-heap dump (use MiniDumpWithFullMemory if these fall short).
                MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                    MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                    MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType,
                                  ep ? &mei : nullptr, nullptr, nullptr);
                CloseHandle(hFile);
            }

            // --- human-readable companion ---
            std::FILE* out = nullptr;
            if (fopen_s(&out, txtPath, "w") == 0 && out)
            {
                const EXCEPTION_RECORD* rec = ep ? ep->ExceptionRecord : nullptr;
                DWORD code = rec ? rec->ExceptionCode : 0;

                std::fprintf(out, "VertexForge crash report\n");
                std::fprintf(out, "Time            : %04d-%02d-%02d %02d:%02d:%02d\n", st.wYear,
                             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                std::fprintf(out, "Exception       : 0x%08lx (%s)\n", code, exceptionLabel(code));
                if (rec)
                    std::fprintf(out, "Fault address   : 0x%p\n", rec->ExceptionAddress);
                if (rec && code == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
                {
                    const char* op = rec->ExceptionInformation[0] == 0   ? "read"
                                     : rec->ExceptionInformation[0] == 1 ? "write"
                                     : rec->ExceptionInformation[0] == 8 ? "execute"
                                                                         : "?";
                    std::fprintf(out, "Access          : %s at 0x%llx\n", op,
                                 static_cast<unsigned long long>(rec->ExceptionInformation[1]));
                }
                if (crashContext[0] != '\0')
                {
                    crashContext[kContextCapacity - 1] = '\0';
                    std::fprintf(out, "Last context    : %s\n", crashContext);
                }

                std::fprintf(out, "\nCall stack:\n");
                if (ep && ep->ContextRecord)
                {
                    CONTEXT contextCopy = *ep->ContextRecord;  // StackWalk64 mutates the context
                    writeStackTrace(out, &contextCopy);
                }
                std::fclose(out);
            }

            // Flush the on-disk log so it reflects everything up to the fault.
            try { spdlog::default_logger()->flush(); } catch (...) {}

            // Surface the crash for users who aren't watching the console.
            char box[MAX_PATH + 64];
            std::snprintf(box, sizeof(box),
                          "VertexForge crashed.\nA crash dump was written to:\n%s", dmpPath);
            MessageBoxA(nullptr, box, "VertexForge - Crash", MB_ICONERROR | MB_OK);

            return EXCEPTION_EXECUTE_HANDLER;
        }
    }  // namespace

    void installCrashHandler()
    {
        bool expected = false;
        if (!installed.compare_exchange_strong(expected, true))
            return;

        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        previousFilter = SetUnhandledExceptionFilter(&vfCrashFilter);
    }

    void setCrashLogContext(const std::string& context)
    {
        const size_t n = context.size() < kContextCapacity - 1 ? context.size() : kContextCapacity - 1;
        std::memcpy(crashContext, context.data(), n);
        crashContext[n] = '\0';
    }
}  // namespace util
