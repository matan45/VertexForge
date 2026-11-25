#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

// VK-1683: the editor's own console window (`Editor.exe --console`).
//
// The launcher starts Editor.exe with stdout and stderr redirected into a per-launch log file and
// without a console window (CREATE_NO_WINDOW). --console gives the window back: the editor opens a
// console of its own and a background thread mirrors the log file into it, from the first byte (so
// the launcher's header shows too), coloured like the editor's own console output (print/Log.hpp).
// The standard handles are left alone, so the log keeps receiving everything.
//
// Closing that console window ends the editor (CTRL_CLOSE_EVENT, exit code 0xC000013A), exactly
// like closing the console of an editor started from Explorer. Ctrl+C and Ctrl+Break are ignored:
// copying text from the window must never kill the editor.
//
// This header carries no Win32 types: it is exported into the plugin SDK.
namespace launch
{
    // Makes redirected stdout and stderr unbuffered. When stdout is a disk file or a pipe the CRT
    // buffers it fully (4 KB), so the launcher's log would lag behind and lose its tail in a crash.
    // Every module shares the /MD CRT, so this covers every printf of every DLL. Call it first in
    // main, before anything is written (setvbuf must precede output). A console is left as it is.
    void prepareStandardStreams() noexcept;

    // The kind of a line print/Log.hpp writes: "[HH:MM:SS] TAG: message".
    enum class LogLineLevel
    {
        Plain,  // anything else: mType print(), spdlog lines from DLLs, "Editor.exe: ..." from Main
        Trace,
        Debug,
        Info,
        Warning,
        Error
    };

    // Strict: '[', two digits, ':', two digits, ':', two digits, "] ", then exactly one of
    // "TRACE: ", "DEBUG: ", "INFO: ", "WARNING: ", "ERROR: ". Anything else is Plain.
    LogLineLevel classifyLogLine(std::string_view line) noexcept;

    // The console colour print/Log.hpp gives the level (a Win32 text attribute); 0 for Plain, which
    // means "leave the colour alone". EditorConsole.cpp pins the values to the FOREGROUND_* flags.
    constexpr std::uint16_t consoleAttributeFor(LogLineLevel level) noexcept
    {
        switch (level)
        {
            case LogLineLevel::Trace:   return 0x08;  // FOREGROUND_INTENSITY
            case LogLineLevel::Debug:   return 0x03;  // FOREGROUND_GREEN | FOREGROUND_BLUE
            case LogLineLevel::Info:    return 0x02;  // FOREGROUND_GREEN
            case LogLineLevel::Warning: return 0x06;  // FOREGROUND_GREEN | FOREGROUND_RED
            case LogLineLevel::Error:   return 0x04;  // FOREGROUND_RED
            case LogLineLevel::Plain:   break;
        }
        return 0;
    }

    // The colour print/Log.hpp resets to after every coloured line.
    inline constexpr std::uint16_t consoleResetAttribute = 0x0F;

    // Cuts a byte stream into lines without touching the heap: the mirror thread must never hold
    // the heap lock, because the process can end (exit(), a crash) while that thread is running.
    //
    // feed() calls sink(fragment, endsLine) for every complete line, without its '\n' and without a
    // '\r' just before it; a line split across feed() calls comes out whole. A line longer than
    // `capacity` comes out as several fragments: every one but the last has endsLine == false, they
    // concatenate back to the line, and none splits a UTF-8 sequence (a fragment ends before the
    // lead byte of a sequence the cut would break). A fragment is valid only during the sink call.
    class LogLineAssembler
    {
    public:
        // The longest fragment handed to the sink, in bytes.
        static constexpr std::size_t capacity = 8192;

        template <class Sink>
            requires std::invocable<Sink&, std::string_view, bool>
        void feed(std::string_view bytes, Sink&& sink)
        {
            for (const char c : bytes)
            {
                if (c == '\n')
                {
                    emitLine(sink);
                    continue;
                }
                // Only a byte that is not the line's end overflows, so a line of exactly `capacity`
                // bytes still comes out as one fragment.
                if (length == capacity)
                {
                    emitFull(sink);
                }
                buffer[length++] = c;
            }
        }

        // Hands over the partial line at the end of the stream as a complete one (endsLine true).
        template <class Sink>
            requires std::invocable<Sink&, std::string_view, bool>
        void flush(Sink&& sink)
        {
            if (length > 0)
            {
                emitLine(sink);
            }
        }

        // True while a partial line waits for its '\n' (or for flush()).
        bool hasPending() const noexcept { return length > 0; }

    private:
        template <class Sink>
        void emitLine(Sink& sink)
        {
            std::size_t size = length;
            if (size > 0 && buffer[size - 1] == '\r')
            {
                --size;
            }
            length = 0;
            sink(std::string_view(buffer.data(), size), true);
        }

        template <class Sink>
        void emitFull(Sink& sink)
        {
            const std::size_t cut = utf8SafeLength(buffer.data(), length);
            sink(std::string_view(buffer.data(), cut), false);
            // The bytes of a cut-off UTF-8 sequence (at most 3) start the next fragment.
            const std::size_t tail = length - cut;
            std::memmove(buffer.data(), buffer.data() + cut, tail);
            length = tail;
        }

        // The longest prefix of data[0, size) that does not end inside a UTF-8 sequence: when one of
        // the last three bytes is a lead byte whose sequence runs past the end, the prefix stops
        // before it. Bytes that are not UTF-8 (e.g. a line in the ANSI code page, on Windows older than
        // 10 1903) are never held back for more than three bytes, and the result is never 0 for a full
        // buffer.
        static constexpr std::size_t utf8SafeLength(const char* data, std::size_t size) noexcept
        {
            for (std::size_t back = 1; back <= 3 && back < size; ++back)
            {
                const auto byte = static_cast<unsigned char>(data[size - back]);
                if ((byte & 0xC0) == 0x80)
                {
                    continue;  // a continuation byte: look further back for its lead byte
                }
                std::size_t sequence = 1;
                if (byte >= 0xC2 && byte <= 0xDF)
                {
                    sequence = 2;
                }
                else if (byte >= 0xE0 && byte <= 0xEF)
                {
                    sequence = 3;
                }
                else if (byte >= 0xF0 && byte <= 0xF4)
                {
                    sequence = 4;
                }
                return sequence > back ? size - back : size;
            }
            return size;
        }

        std::array<char, capacity> buffer{};
        std::size_t length = 0;
    };

    // The console window of `Editor.exe --console`. Destroying it stops the mirror after a final
    // drain of what the log file holds by then (bounded); the console window stays open until the
    // process ends.
    class EditorConsole
    {
    public:
        struct Result
        {
            enum class Status
            {
                AlreadyConsole,       // stdout already is a console (a terminal, an Explorer start): nothing done
                Opened,               // a console window of its own, mirroring the redirected output
                OpenedWithoutMirror,  // a console window that cannot show the output; `message` says why
                Failed                // no console window; `message` says why
            };

            Status status = Status::Failed;
            std::string message;  // UTF-8; empty for AlreadyConsole and Opened
        };

        EditorConsole() noexcept;
        ~EditorConsole();

        EditorConsole(const EditorConsole&) = delete;
        EditorConsole& operator=(const EditorConsole&) = delete;
        EditorConsole(EditorConsole&&) = delete;
        EditorConsole& operator=(EditorConsole&&) = delete;

        // Opens the window titled `titleUtf8` and starts the mirror. Call once, from the main
        // thread. Never throws: every failure is reported through the Result.
        Result open(std::string_view titleUtf8);

    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
