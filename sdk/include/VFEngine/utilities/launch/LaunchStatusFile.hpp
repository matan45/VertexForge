#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

// VK-1682: the `--launch-status <file>` contract. Editor.exe keeps one line of JSON in that file:
//
//   {"state":"loading","code":0,"message":"Loading project C:\\Games\\Demo\\Demo.vfproj","pid":1234}
//
// state   loading (written at start), ready (the project and its startup scene are loaded) or
//         failed (code says why: an EditorExitCode the editor exits with, or the exception code
//         of a crash; the process may stay alive behind the crash message box).
// message UTF-8 text for a person; never parse it.
// pid     the editor's process id.
//
// Every write replaces the whole file atomically (sibling temp file, flush, MoveFileExW), so a
// reader sees either the previous or the new complete line. Once "failed" is written nothing
// overwrites it.
//
// Reading the state after the process ended: exit code 0 is always a normal exit, whatever the
// file says (a normal close leaves "ready"; closing the window during the load leaves "loading").
// A non-zero exit with the state still loading or ready means the editor died without reporting
// why (vfLogAssert's exit(-1), a __fastfail, a killed process, ...).
//
// A reader that holds the file open blocks the replace (MoveFileExW fails with ERROR_ACCESS_DENIED
// even when the reader shares FILE_SHARE_DELETE, as Java NIO does): read the whole file in one call
// and close it at once. The editor retries a blocked replace for about a second.
//
// This header carries no Win32 types: it is exported into the plugin SDK.
namespace launch
{
    enum class LaunchState
    {
        Loading,
        Ready,
        Failed
    };

    std::string_view launchStateName(LaunchState state) noexcept;

    // Longest message kept, in input bytes; longer messages are cut on a UTF-8 boundary.
    inline constexpr std::size_t maxLaunchStatusMessageBytes = 1024;
    // Fits the longest message even when every byte is escaped as \u00XX.
    inline constexpr std::size_t launchStatusBufferSize = 8192;

    // Formats the status line (with a trailing '\n') into `out`, NUL-terminated. Never allocates.
    // `"`, `\` and control characters are escaped; bytes that are not valid UTF-8 become '?'.
    // Returns the length without the NUL, or 0 when `capacity` is too small.
    std::size_t formatLaunchStatus(char* out, std::size_t capacity, LaunchState state, int code,
                                   std::string_view messageUtf8, unsigned long pid) noexcept;

    class LaunchStatusFile
    {
    public:
        LaunchStatusFile() = default;
        LaunchStatusFile(const LaunchStatusFile&) = delete;
        LaunchStatusFile& operator=(const LaunchStatusFile&) = delete;

        // Resolves `path` against the current directory. Fails when the folder does not exist, the
        // path names a folder, or the file is already open. Call before any write.
        bool open(const std::filesystem::path& path);
        bool isOpen() const noexcept;
        const std::filesystem::path& path() const noexcept;

        // Main-thread writes. Returns false when not open, when "failed" was already written (or a
        // crash is being written), or when the file could not be replaced within about a second.
        // A "failed" line that could not be written does not close the file to later failures.
        bool write(LaunchState state, int code, std::string_view messageUtf8) noexcept;

        // For the crash filter and the SIGABRT handler: writes "failed" once, from any thread, with
        // no heap use. Waits up to ~300 ms for a write in flight on another thread, so the crash
        // line lands last. Returns false when not open or when a failure was already written.
        bool writeFromCrash(int code, const char* messageUtf8) noexcept;

    private:
        std::filesystem::path finalPath;
        std::wstring tempPath;
        std::wstring crashTempPath;
        std::atomic<bool> opened{false};
        // Set by the first "failed" write (normal or crash); no write follows it.
        std::atomic<bool> terminal{false};
        std::atomic<int> writersInFlight{0};
        // Only the one writer that wins `terminal` formats into it, so the crash path needs no stack.
        char crashBuffer[launchStatusBufferSize] = {};
    };

    // The editor process's status file (opened by Main from --launch-status).
    LaunchStatusFile& processLaunchStatus() noexcept;
}
