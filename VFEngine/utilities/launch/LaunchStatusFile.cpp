#include "LaunchStatusFile.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <charconv>
#include <system_error>

namespace launch
{
    namespace
    {
        // A reader that has the file open blocks the replace with ERROR_ACCESS_DENIED, whether or not
        // it shares FILE_SHARE_DELETE (checked on NTFS). Every line is a one-shot state change that
        // nothing rewrites later, so ride out a slow reader or a scanner for about a second.
        constexpr int kReplaceRetries = 50;
        constexpr DWORD kReplaceRetryDelayMs = 20;

        // How long a crash write waits for a normal write in flight on another thread.
        constexpr int kCrashWaitSteps = 30;
        constexpr DWORD kCrashWaitStepMs = 10;

        // Appends into a caller buffer, always keeping room for the NUL. Never allocates.
        class Sink
        {
        public:
            Sink(char* out, std::size_t capacity) noexcept : out(out), capacity(capacity) {}

            void put(char c) noexcept
            {
                if (length + 1 >= capacity)
                {
                    overflow = true;
                    return;
                }
                out[length++] = c;
            }

            void put(std::string_view text) noexcept
            {
                for (char c : text)
                {
                    put(c);
                }
            }

            template <typename T>
            void putNumber(T value) noexcept
            {
                char digits[24];
                auto [ptr, ec] = std::to_chars(digits, digits + sizeof(digits), value);
                if (ec != std::errc{})
                {
                    overflow = true;
                    return;
                }
                put(std::string_view(digits, static_cast<std::size_t>(ptr - digits)));
            }

            std::size_t finish() noexcept
            {
                if (capacity == 0)
                {
                    return 0;
                }
                if (overflow)
                {
                    out[0] = '\0';
                    return 0;
                }
                out[length] = '\0';
                return length;
            }

        private:
            char* out;
            std::size_t capacity;
            std::size_t length = 0;
            bool overflow = false;
        };

        // Length of the valid UTF-8 sequence that starts at text[i], or 0 when the bytes there are
        // not valid UTF-8 (stray continuation byte, overlong form, surrogate, beyond U+10FFFF, cut off).
        std::size_t validSequenceLength(std::string_view text, std::size_t i) noexcept
        {
            const auto byteAt = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };

            const unsigned char lead = byteAt(i);
            if (lead < 0x80)
            {
                return 1;
            }

            std::size_t length = 0;
            unsigned char secondLow = 0x80;
            unsigned char secondHigh = 0xBF;
            if (lead >= 0xC2 && lead <= 0xDF)
            {
                length = 2;
            }
            else if (lead == 0xE0)
            {
                length = 3;
                secondLow = 0xA0;
            }
            else if ((lead >= 0xE1 && lead <= 0xEC) || lead == 0xEE || lead == 0xEF)
            {
                length = 3;
            }
            else if (lead == 0xED)
            {
                length = 3;
                secondHigh = 0x9F;
            }
            else if (lead == 0xF0)
            {
                length = 4;
                secondLow = 0x90;
            }
            else if (lead >= 0xF1 && lead <= 0xF3)
            {
                length = 4;
            }
            else if (lead == 0xF4)
            {
                length = 4;
                secondHigh = 0x8F;
            }
            else
            {
                return 0;
            }

            if (i + length > text.size())
            {
                return 0;
            }
            const unsigned char second = byteAt(i + 1);
            if (second < secondLow || second > secondHigh)
            {
                return 0;
            }
            for (std::size_t k = 2; k < length; ++k)
            {
                const unsigned char next = byteAt(i + k);
                if (next < 0x80 || next > 0xBF)
                {
                    return 0;
                }
            }
            return length;
        }

        void putEscaped(Sink& sink, std::string_view message) noexcept
        {
            static constexpr char hexDigits[] = "0123456789abcdef";

            std::size_t i = 0;
            while (i < message.size())
            {
                const std::size_t sequence = validSequenceLength(message, i);
                const std::size_t consumed = sequence == 0 ? 1 : sequence;
                if (i + consumed > maxLaunchStatusMessageBytes)
                {
                    break;
                }

                if (sequence == 0)
                {
                    sink.put('?');
                }
                else if (sequence > 1)
                {
                    sink.put(message.substr(i, sequence));
                }
                else
                {
                    const char c = message[i];
                    const auto value = static_cast<unsigned char>(c);
                    if (c == '"')
                    {
                        sink.put(std::string_view("\\\""));
                    }
                    else if (c == '\\')
                    {
                        sink.put(std::string_view("\\\\"));
                    }
                    else if (value < 0x20)
                    {
                        sink.put(std::string_view("\\u00"));
                        sink.put(hexDigits[value >> 4]);
                        sink.put(hexDigits[value & 0x0F]);
                    }
                    else
                    {
                        sink.put(c);
                    }
                }
                i += consumed;
            }
        }

        // Writes `data` to `tempPath`, pushes it past the OS cache, then renames it over `finalPath`
        // in one directory operation (as resource::replaceFileAtomically does, AtomicFileReplace.cpp).
        // Never allocates and never logs, so the crash filter can use it.
        bool writeAndReplace(const wchar_t* tempPath, const wchar_t* finalPath, const char* data,
                             std::size_t size) noexcept
        {
            HANDLE file = CreateFileW(tempPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            DWORD written = 0;
            bool ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) != FALSE &&
                      written == size;
            ok = ok && FlushFileBuffers(file) != FALSE;
            CloseHandle(file);

            if (ok)
            {
                for (int attempt = 0;; ++attempt)
                {
                    if (MoveFileExW(tempPath, finalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
                    {
                        return true;
                    }
                    const DWORD error = GetLastError();
                    if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) ||
                        attempt >= kReplaceRetries)
                    {
                        break;
                    }
                    Sleep(kReplaceRetryDelayMs);
                }
            }

            DeleteFileW(tempPath);
            return false;
        }
    }

    std::string_view launchStateName(LaunchState state) noexcept
    {
        switch (state)
        {
            case LaunchState::Loading: return "loading";
            case LaunchState::Ready:   return "ready";
            case LaunchState::Failed:  return "failed";
        }
        return "failed";
    }

    std::size_t formatLaunchStatus(char* out, std::size_t capacity, LaunchState state, int code,
                                   std::string_view messageUtf8, unsigned long pid) noexcept
    {
        Sink sink(out, capacity);
        sink.put(std::string_view("{\"state\":\""));
        sink.put(launchStateName(state));
        sink.put(std::string_view("\",\"code\":"));
        sink.putNumber(code);
        sink.put(std::string_view(",\"message\":\""));
        putEscaped(sink, messageUtf8);
        sink.put(std::string_view("\",\"pid\":"));
        sink.putNumber(pid);
        sink.put(std::string_view("}\n"));
        return sink.finish();
    }

    bool LaunchStatusFile::open(const std::filesystem::path& path)
    {
        if (opened.load())
        {
            return false;
        }

        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (ec)
        {
            return false;
        }
        absolute = absolute.lexically_normal();
        if (!absolute.has_filename())
        {
            return false;
        }

        if (!std::filesystem::is_directory(absolute.parent_path(), ec) || ec)
        {
            return false;
        }
        std::error_code existsEc;
        if (std::filesystem::is_directory(absolute, existsEc))
        {
            return false;
        }

        // The pid keeps two editors that were handed the same status path from sharing a temp file.
        const std::wstring pid = std::to_wstring(GetCurrentProcessId());
        finalPath = absolute;
        tempPath = absolute.native() + L"." + pid + L".tmp";
        crashTempPath = absolute.native() + L"." + pid + L".crash.tmp";
        opened.store(true);
        return true;
    }

    bool LaunchStatusFile::isOpen() const noexcept
    {
        return opened.load();
    }

    const std::filesystem::path& LaunchStatusFile::path() const noexcept
    {
        return finalPath;
    }

    bool LaunchStatusFile::write(LaunchState state, int code, std::string_view messageUtf8) noexcept
    {
        if (!opened.load())
        {
            return false;
        }

        // Announce the write BEFORE checking `terminal` (both seq_cst): either this write sees the
        // crash's flag and backs off, or the crash sees this write in flight and waits for it.
        writersInFlight.fetch_add(1);
        bool written = false;
        if (!terminal.load() && (state != LaunchState::Failed || !terminal.exchange(true)))
        {
            char buffer[launchStatusBufferSize];
            const std::size_t length = formatLaunchStatus(buffer, sizeof(buffer), state, code, messageUtf8,
                                                          GetCurrentProcessId());
            written = length != 0 && writeAndReplace(tempPath.c_str(), finalPath.c_str(), buffer, length);
            if (!written && state == LaunchState::Failed)
            {
                // Nothing landed: leave the file open to a later failure (a crash during shutdown).
                terminal.store(false);
            }
        }
        writersInFlight.fetch_sub(1);
        return written;
    }

    bool LaunchStatusFile::writeFromCrash(int code, const char* messageUtf8) noexcept
    {
        if (!opened.load())
        {
            return false;
        }
        // The first failure wins: a crash after the editor already reported why it gave up (or a
        // second thread crashing at the same time) keeps the earlier line.
        if (terminal.exchange(true))
        {
            return false;
        }

        // A crash on the thread that is itself inside write() never sees the count drop; the wait is
        // bounded for that case.
        for (int step = 0; step < kCrashWaitSteps && writersInFlight.load() != 0; ++step)
        {
            Sleep(kCrashWaitStepMs);
        }

        const std::string_view message = messageUtf8 ? std::string_view(messageUtf8) : std::string_view();
        const std::size_t length = formatLaunchStatus(crashBuffer, sizeof(crashBuffer), LaunchState::Failed,
                                                      code, message, GetCurrentProcessId());
        return length != 0 && writeAndReplace(crashTempPath.c_str(), finalPath.c_str(), crashBuffer, length);
    }

    LaunchStatusFile& processLaunchStatus() noexcept
    {
        // Never destroyed: the crash filter and the SIGABRT handler stay installed until the process
        // is gone, and a fault during static destruction must still find live paths.
        static LaunchStatusFile* const instance = new LaunchStatusFile();
        return *instance;
    }
}
