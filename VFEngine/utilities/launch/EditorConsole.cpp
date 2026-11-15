#include "EditorConsole.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <initializer_list>
#include <utility>

namespace launch
{
    static_assert(consoleAttributeFor(LogLineLevel::Trace) == FOREGROUND_INTENSITY);
    static_assert(consoleAttributeFor(LogLineLevel::Debug) == (FOREGROUND_GREEN | FOREGROUND_BLUE));
    static_assert(consoleAttributeFor(LogLineLevel::Info) == FOREGROUND_GREEN);
    static_assert(consoleAttributeFor(LogLineLevel::Warning) == (FOREGROUND_GREEN | FOREGROUND_RED));
    static_assert(consoleAttributeFor(LogLineLevel::Error) == FOREGROUND_RED);
    static_assert(consoleAttributeFor(LogLineLevel::Plain) == 0);
    // util::resetConsoleColor(): bright white on black.
    static_assert(consoleResetAttribute ==
                  (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY));

    namespace
    {
        // How often the mirror looks for new output once it has read everything.
        constexpr DWORD kPollIntervalMs = 50;
        // After the stop request the mirror still copies what the editor wrote last, for at most this long.
        constexpr ULONGLONG kFinalDrainMs = 200;
        // How long the destructor waits for the mirror. A console with a selection in progress blocks
        // WriteConsoleW until the selection ends, so the wait must not be unbounded.
        constexpr DWORD kStopWaitMs = 1000;

        // ALLOC_CONSOLE_OPTIONS and AllocConsoleWithOptions (Windows 11 24H2 and later). The SDK
        // declares them only for NTDDI_VERSION >= NTDDI_WIN11_GE and older Windows lacks the export,
        // so the function is looked up at run time, with a local copy of the struct.
        struct ConsoleAllocOptions
        {
            int mode;  // ALLOC_CONSOLE_MODE
            BOOL useShowWindow;
            WORD showWindow;
        };
        using AllocConsoleWithOptionsFunction = HRESULT(WINAPI*)(ConsoleAllocOptions* options, int* result);
        constexpr int kAllocConsoleModeNewWindow = 1;    // ALLOC_CONSOLE_MODE_NEW_WINDOW
        constexpr int kAllocConsoleResultNoConsole = 0;  // ALLOC_CONSOLE_RESULT_NO_CONSOLE
        constexpr int kAllocConsoleResultNewConsole = 1; // ALLOC_CONSOLE_RESULT_NEW_CONSOLE

        bool isValidHandle(HANDLE handle) noexcept
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        std::string windowsErrorText(DWORD error)
        {
            return "Windows error " + std::to_string(error);
        }

        std::wstring widenUtf8(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0)
            {
                return {};
            }
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        void unbufferIfRedirected(DWORD which, FILE* stream) noexcept
        {
            const HANDLE handle = GetStdHandle(which);
            if (!isValidHandle(handle))
            {
                return;
            }
            const DWORD type = GetFileType(handle);
            if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE)
            {
                std::setvbuf(stream, nullptr, _IONBF, 0);
            }
        }

        // Copying text from the console with Ctrl+C must never kill the editor. Returning FALSE for
        // the other events (closing the window, log-off, shutdown) keeps the default: the process
        // ends, as an editor started from Explorer does when its console is closed.
        BOOL WINAPI ignoreInterrupts(DWORD controlType) noexcept
        {
            return controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT ? TRUE : FALSE;
        }

        // A standard handle that is redirected (valid and not a console handle) before the console
        // is allocated. Allocating a console may point the standard handles at it; the redirected
        // ones are put back, so GetStdHandle keeps naming the log file: print/Log.hpp colours
        // GetStdHandle's handle, which must stay a no-op on the file rather than race the mirror.
        struct RedirectedHandle
        {
            DWORD which = 0;
            HANDLE handle = nullptr;

            static RedirectedHandle of(DWORD which) noexcept
            {
                const HANDLE handle = GetStdHandle(which);
                DWORD mode = 0;
                if (!isValidHandle(handle) || GetConsoleMode(handle, &mode))
                {
                    return {which, nullptr};
                }
                return {which, handle};
            }

            void restore() const noexcept
            {
                if (handle != nullptr && GetStdHandle(which) != handle)
                {
                    SetStdHandle(which, handle);
                }
            }
        };

        // Why the output cannot be mirrored, for a stdout that is not a disk file.
        const char* unmirroredReason(HANDLE output) noexcept
        {
            if (!isValidHandle(output))
            {
                return "the editor has no standard output";
            }
            switch (GetFileType(output))
            {
                case FILE_TYPE_PIPE: return "the editor's output goes to a pipe, which cannot be read back";
                case FILE_TYPE_CHAR: return "the editor's output goes to a device (such as NUL), not to a file";
                default:             return "the editor's output does not go to a file";
            }
        }
    }

    // Everything the mirror thread touches. open() allocates it before it changes anything; the
    // thread's loop uses only these members and Win32 calls: no heap, no CRT stdio, no vfLog.
    struct EditorConsole::State
    {
        HANDLE console = INVALID_HANDLE_VALUE;  // CONOUT$ of the new console
        HANDLE log = INVALID_HANDLE_VALUE;      // our own read handle on the redirected stdout file
        HANDLE stop = nullptr;                  // manual-reset event: the destructor asks the mirror to stop
        HANDLE thread = nullptr;

        LogLineAssembler assembler;
        bool midLine = false;                   // the last fragment written did not end its line
        std::uint16_t lineAttribute = 0;        // colour of the line being written; 0 = left alone
        char readBuffer[16 * 1024] = {};
        // A fragment of N bytes never needs more than N UTF-16 units (UTF-8 or an ANSI code page).
        wchar_t wideBuffer[LogLineAssembler::capacity] = {};

        State() = default;
        State(const State&) = delete;
        State& operator=(const State&) = delete;

        // Only once the thread has ended (or was never started).
        ~State()
        {
            for (HANDLE* handle : {&thread, &stop})
            {
                if (*handle != nullptr)
                {
                    CloseHandle(*handle);
                }
            }
            for (HANDLE* handle : {&log, &console})
            {
                if (*handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(*handle);
                }
            }
        }

        void writeWide(const wchar_t* text, DWORD length) noexcept
        {
            while (length > 0)
            {
                DWORD written = 0;
                if (!WriteConsoleW(console, text, length, &written, nullptr) || written == 0)
                {
                    return;
                }
                text += written;
                length -= written;
            }
        }

        // UTF-8 first; a fragment that is not valid UTF-8 is read in the ANSI code page (some log
        // lines carry narrow paths).
        void writeBytes(std::string_view bytes) noexcept
        {
            if (bytes.empty())
            {
                return;
            }
            const int size = static_cast<int>(bytes.size());
            const int capacity = static_cast<int>(LogLineAssembler::capacity);
            int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, wideBuffer, capacity);
            if (length <= 0)
            {
                length = MultiByteToWideChar(CP_ACP, 0, bytes.data(), size, wideBuffer, capacity);
            }
            if (length > 0)
            {
                writeWide(wideBuffer, static_cast<DWORD>(length));
            }
        }

        void writeFragment(std::string_view fragment, bool endsLine) noexcept
        {
            if (!midLine)
            {
                // A line's first fragment holds its whole "[HH:MM:SS] TAG: " prefix (a fragment that
                // does not end its line is at least capacity - 3 bytes long); the line's later
                // fragments keep its colour.
                lineAttribute = consoleAttributeFor(classifyLogLine(fragment));
                if (lineAttribute != 0)
                {
                    SetConsoleTextAttribute(console, lineAttribute);
                }
            }
            writeBytes(fragment);
            if (endsLine)
            {
                writeWide(L"\r\n", 2);
                if (lineAttribute != 0)
                {
                    SetConsoleTextAttribute(console, consoleResetAttribute);
                }
            }
            midLine = !endsLine;
        }

        // Copies the next chunk of the log file into the console. Returns false at the end of the
        // file. A failed read counts as the end of the file: the mirror tries again later.
        bool copyChunk() noexcept
        {
            DWORD bytesRead = 0;
            if (!ReadFile(log, readBuffer, static_cast<DWORD>(sizeof(readBuffer)), &bytesRead, nullptr) ||
                bytesRead == 0)
            {
                return false;
            }
            assembler.feed(std::string_view(readBuffer, bytesRead),
                           [this](std::string_view fragment, bool endsLine) { writeFragment(fragment, endsLine); });
            return true;
        }

        void mirror() noexcept
        {
            // From offset 0: the launcher's header and everything written before open() show too.
            for (;;)
            {
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
                {
                    break;
                }
                if (!copyChunk() && WaitForSingleObject(stop, kPollIntervalMs) == WAIT_OBJECT_0)
                {
                    break;
                }
            }

            // The editor's last lines (its shutdown) were written before the stop request.
            const ULONGLONG deadline = GetTickCount64() + kFinalDrainMs;
            while (GetTickCount64() < deadline && copyChunk())
            {
            }
            assembler.flush([this](std::string_view fragment, bool endsLine) { writeFragment(fragment, endsLine); });
        }

        static DWORD WINAPI threadMain(void* parameter) noexcept
        {
            static_cast<State*>(parameter)->mirror();
            return 0;
        }
    };

    void prepareStandardStreams() noexcept
    {
        unbufferIfRedirected(STD_OUTPUT_HANDLE, stdout);
        unbufferIfRedirected(STD_ERROR_HANDLE, stderr);
    }

    LogLineLevel classifyLogLine(std::string_view line) noexcept
    {
        // "[HH:MM:SS] " from util::getCurrentTime().
        constexpr std::size_t timeLength = 11;
        if (line.size() < timeLength || line[0] != '[' || line[3] != ':' || line[6] != ':' || line[9] != ']' ||
            line[10] != ' ')
        {
            return LogLineLevel::Plain;
        }
        for (const std::size_t digit : {1u, 2u, 4u, 5u, 7u, 8u})
        {
            if (line[digit] < '0' || line[digit] > '9')
            {
                return LogLineLevel::Plain;
            }
        }

        struct Tag
        {
            std::string_view text;
            LogLineLevel level;
        };
        constexpr Tag tags[] = {
            {"TRACE: ", LogLineLevel::Trace},
            {"DEBUG: ", LogLineLevel::Debug},
            {"INFO: ", LogLineLevel::Info},
            {"WARNING: ", LogLineLevel::Warning},
            {"ERROR: ", LogLineLevel::Error},
        };
        const std::string_view rest = line.substr(timeLength);
        for (const Tag& tag : tags)
        {
            if (rest.starts_with(tag.text))
            {
                return tag.level;
            }
        }
        return LogLineLevel::Plain;
    }

    EditorConsole::EditorConsole() noexcept = default;

    EditorConsole::~EditorConsole()
    {
        if (state && state->thread != nullptr)
        {
            SetEvent(state->stop);
            if (WaitForSingleObject(state->thread, kStopWaitMs) != WAIT_OBJECT_0)
            {
                // Still blocked in a console write: leave it its state and handles (deliberately
                // leaked) rather than pull them from under it. The process is about to end anyway.
                static_cast<void>(state.release());
            }
        }
        // `state` (when still held) closes its handles. The console window stays: it closes when
        // the process ends.
    }

    EditorConsole::Result EditorConsole::open(std::string_view titleUtf8)
    {
        using Status = Result::Status;
        try
        {
            if (state)
            {
                return {Status::Failed, "the console window is already open"};
            }

            const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            // Not GetFileType == FILE_TYPE_CHAR: the NUL device is a character device too.
            if (isValidHandle(output) && GetConsoleMode(output, &mode))
            {
                return {Status::AlreadyConsole, {}};
            }

            // Allocated before anything changes, so running out of memory leaves the process as it was.
            auto opened = std::make_unique<State>();
            const std::wstring title = widenUtf8(titleUtf8);
            const RedirectedHandle redirected[] = {RedirectedHandle::of(STD_INPUT_HANDLE),
                                                   RedirectedHandle::of(STD_OUTPUT_HANDLE),
                                                   RedirectedHandle::of(STD_ERROR_HANDLE)};

            // A process started with CREATE_NO_WINDOW has a console without a window, and a process
            // can be attached to one console only: leave it first (failing when there is none is fine).
            FreeConsole();

            bool allocated = false;
            const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
            const auto allocWithOptions = kernel != nullptr
                ? reinterpret_cast<AllocConsoleWithOptionsFunction>(GetProcAddress(kernel, "AllocConsoleWithOptions"))
                : nullptr;
            if (allocWithOptions != nullptr)
            {
                // NEW_WINDOW: a visible window of its own (Windows Terminal when that is the default
                // terminal), whatever show-window flags the launcher started the editor with.
                ConsoleAllocOptions options{kAllocConsoleModeNewWindow, FALSE, 0};
                int allocResult = kAllocConsoleResultNoConsole;
                const HRESULT hr = allocWithOptions(&options, &allocResult);
                allocated = SUCCEEDED(hr) && allocResult == kAllocConsoleResultNewConsole;
            }
            if (!allocated && !AllocConsole())
            {
                return {Status::Failed, "cannot open a console window (" + windowsErrorText(GetLastError()) + ")"};
            }

            for (const RedirectedHandle& handle : redirected)
            {
                handle.restore();
            }

            SetConsoleTitleW(title.empty() ? L"VertexForge Editor" : title.c_str());
            SetConsoleCtrlHandler(&ignoreInterrupts, TRUE);

            opened->console = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr, OPEN_EXISTING, 0, nullptr);
            if (opened->console == INVALID_HANDLE_VALUE)
            {
                const DWORD openError = GetLastError();
                // A window that can never show anything would only be a trap (closing it ends the editor).
                FreeConsole();
                return {Status::Failed, "cannot write to the new console window (" + windowsErrorText(openError) + ")"};
            }
            state = std::move(opened);

            // Mirror through a read handle of our own: the standard handles stay on the log file
            // (never freopen("CONOUT$"), never SetStdHandle to the console), so the log keeps everything.
            const DWORD outputType = isValidHandle(output) ? GetFileType(output) : FILE_TYPE_UNKNOWN;
            std::string reason;
            if (outputType == FILE_TYPE_DISK)
            {
                state->log = ReOpenFile(output, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
                if (state->log == INVALID_HANDLE_VALUE)
                {
                    reason = "the editor's output file cannot be read back (" + windowsErrorText(GetLastError()) + ")";
                }
            }
            else
            {
                reason = unmirroredReason(output);
            }

            if (reason.empty())
            {
                state->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (state->stop == nullptr)
                {
                    reason = "cannot start the output mirror (" + windowsErrorText(GetLastError()) + ")";
                }
                else
                {
                    // Last step: nothing after it can fail, so `state` is never freed under the thread.
                    state->thread = CreateThread(nullptr, 0, &State::threadMain, state.get(), 0, nullptr);
                    if (state->thread == nullptr)
                    {
                        reason = "cannot start the output mirror (" + windowsErrorText(GetLastError()) + ")";
                    }
                }
            }

            if (reason.empty())
            {
                return {Status::Opened, {}};
            }

            // The output still reaches whatever captures it (a launcher's log) when it is a file or a pipe.
            const bool captured = outputType == FILE_TYPE_DISK || outputType == FILE_TYPE_PIPE;
            const std::wstring line =
                widenUtf8("This console cannot show the editor's output: " + reason + "." +
                          (captured ? " The launcher's log has it." : "")) + L"\r\n";
            state->writeWide(line.c_str(), static_cast<DWORD>(line.size()));
            return {Status::OpenedWithoutMirror, "the console window cannot show the editor's output: " + reason};
        }
        catch (...)
        {
            // Only a message could not be built (out of memory). The short text needs no allocation.
            return {Status::Failed, "out of memory"};
        }
    }
}
