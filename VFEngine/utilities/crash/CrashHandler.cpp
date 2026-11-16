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
#include <cwchar>
#include <filesystem>
#include <iterator>
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
        std::atomic<CrashNotifier> crashNotifier{nullptr};

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

        // UTF-16 to UTF-8 into a fixed buffer (no heap at crash time). Converts at most
        // (capacity - 1) / 3 UTF-16 units, never ending on the first half of a surrogate pair: one unit
        // never needs more than 3 UTF-8 bytes (a pair is 2 units for 4 bytes), so the result always
        // fits. Always NUL-terminates. VK-1684: the report is UTF-8 whatever the process code page.
        void utf8Prefix(const wchar_t* text, char* out, int capacity) noexcept
        {
            if (capacity <= 0)
                return;
            out[0] = '\0';
            if (text == nullptr)
                return;

            const int maxUnits = (capacity - 1) / 3;
            int units = 0;
            while (units < maxUnits && text[units] != L'\0')
                ++units;
            // Stopped at maxUnits with more text to come: do not split a surrogate pair.
            if (units > 0 && text[units] != L'\0' && text[units - 1] >= 0xD800 && text[units - 1] <= 0xDBFF)
                --units;
            if (units == 0)
                return;

            const int written = WideCharToMultiByte(CP_UTF8, 0, text, units, out, capacity - 1, nullptr, nullptr);
            out[written > 0 ? written : 0] = '\0';
        }

        // Longest symbol name read back, in UTF-16 units.
        constexpr ULONG kMaxSymbolName = 512;

        void writeStackTrace(std::FILE* out, CONTEXT* context)
        {
            HANDLE process = GetCurrentProcess();
            HANDLE thread = GetCurrentThread();

            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            SymInitializeW(process, nullptr, TRUE);

            STACKFRAME64 frame = {};
            frame.AddrPC.Offset = context->Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context->Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context->Rsp;
            frame.AddrStack.Mode = AddrModeFlat;

            // The wide dbghelp functions (VK-1684): names and paths come back as UTF-16 and are written
            // as UTF-8, independent of the process code page.
            alignas(SYMBOL_INFOW) unsigned char symbolBuffer[sizeof(SYMBOL_INFOW) + kMaxSymbolName * sizeof(WCHAR)] = {0};
            SYMBOL_INFOW* symbol = reinterpret_cast<SYMBOL_INFOW*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
            symbol->MaxNameLen = kMaxSymbolName;

            for (int i = 0; i < 128; ++i)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, context,
                                 nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                    break;
                if (frame.AddrPC.Offset == 0)
                    break;

                DWORD64 addr = frame.AddrPC.Offset;

                char moduleName[3 * MAX_PATH + 1] = "<unknown>";
                DWORD64 moduleBase = SymGetModuleBase64(process, addr);
                if (moduleBase)
                {
                    wchar_t modPath[MAX_PATH];
                    if (GetModuleFileNameW(reinterpret_cast<HMODULE>(moduleBase), modPath, MAX_PATH))
                    {
                        modPath[MAX_PATH - 1] = L'\0';  // a truncated path is NUL-terminated only on Vista+
                        const wchar_t* base = std::wcsrchr(modPath, L'\\');
                        utf8Prefix(base ? base + 1 : modPath, moduleName, static_cast<int>(sizeof(moduleName)));
                    }
                }

                DWORD64 displacement = 0;
                if (SymFromAddrW(process, addr, &displacement, symbol))
                {
                    // NameLen is the full length, which may exceed what fits; the buffer holds
                    // kMaxSymbolName + 1 units.
                    symbol->Name[symbol->NameLen < kMaxSymbolName ? symbol->NameLen : kMaxSymbolName] = L'\0';
                    char symbolName[3 * kMaxSymbolName + 1];
                    utf8Prefix(symbol->Name, symbolName, static_cast<int>(sizeof(symbolName)));

                    IMAGEHLP_LINEW64 line = {};
                    line.SizeOfStruct = sizeof(IMAGEHLP_LINEW64);
                    DWORD lineDisp = 0;
                    if (SymGetLineFromAddrW64(process, addr, &lineDisp, &line))
                    {
                        char fileName[3 * MAX_PATH + 1];
                        utf8Prefix(line.FileName, fileName, static_cast<int>(sizeof(fileName)));
                        std::fprintf(out, "  [%2d] %s!%s + 0x%llx  (%s:%lu)\n", i, moduleName,
                                     symbolName, static_cast<unsigned long long>(displacement),
                                     fileName, line.LineNumber);
                    }
                    else
                    {
                        std::fprintf(out, "  [%2d] %s!%s + 0x%llx\n", i, moduleName, symbolName,
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

            char dmpPath[MAX_PATH];
            char txtPath[MAX_PATH];
            std::snprintf(dmpPath, sizeof(dmpPath), "crashes/crash_%s.dmp", stamp);
            std::snprintf(txtPath, sizeof(txtPath), "crashes/crash_%s.txt", stamp);

            // VK-1682: before anything that can be slow or fault again (directory creation, the
            // minidump, symbolization) and before the message box.
            if (CrashNotifier notifier = crashNotifier.load())
            {
                const DWORD notifyCode = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
                notifier(notifyCode, exceptionLabel(notifyCode), dmpPath);
            }

            std::error_code ec;
            std::filesystem::create_directories("crashes", ec);

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

            // Surface the crash for users who aren't watching the console. VK-1684: wide, with the
            // dump's absolute path, so a folder name in any script shows whatever the code page is.
            wchar_t relativeDump[64];
            swprintf_s(relativeDump, L"crashes\\crash_%hs.dmp", stamp);
            wchar_t absoluteDump[MAX_PATH + 64];
            const DWORD absoluteLength =
                GetFullPathNameW(relativeDump, static_cast<DWORD>(std::size(absoluteDump)), absoluteDump, nullptr);
            const wchar_t* shownDump =
                (absoluteLength > 0 && absoluteLength < std::size(absoluteDump)) ? absoluteDump : relativeDump;
            // Fits: the 50-unit text plus at most MAX_PATH + 63 units of path.
            wchar_t box[MAX_PATH + 160];
            swprintf_s(box, L"VertexForge crashed.\nA crash dump was written to:\n%ls", shownDump);
            MessageBoxW(nullptr, box, L"VertexForge - Crash", MB_ICONERROR | MB_OK);

            return EXCEPTION_EXECUTE_HANDLER;
        }
    }  // namespace

    void installCrashHandler()
    {
        bool expected = false;
        if (!installed.compare_exchange_strong(expected, true))
            return;

        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

        // Reserve a stack guard region so that on EXCEPTION_STACK_OVERFLOW the filter
        // still has room to run. vfCrashFilter does real work (dump/symbolize/format),
        // which needs more than the few bytes left at the point of a stack overflow;
        // without this reservation that exact case — one of the crashes we most want to
        // capture — would re-fault before writing anything.
        ULONG stackGuaranteeBytes = 64 * 1024;
        SetThreadStackGuarantee(&stackGuaranteeBytes);

        previousFilter = SetUnhandledExceptionFilter(&vfCrashFilter);
    }

    void setCrashNotifier(CrashNotifier notifier) noexcept
    {
        crashNotifier.store(notifier);
    }

    void setCrashLogContext(const std::string& context)
    {
        size_t n = context.size() < kContextCapacity - 1 ? context.size() : kContextCapacity - 1;
        // A cut text ends before the character it would split (VK-1684: the context is UTF-8, e.g. a
        // script path), so the report stays valid UTF-8.
        if (n < context.size())
        {
            while (n > 0 && (static_cast<unsigned char>(context[n]) & 0xC0) == 0x80)
                --n;
        }
        std::memcpy(crashContext, context.data(), n);
        crashContext[n] = '\0';
    }
}  // namespace util
