#pragma once

namespace launch
{
    // VK-1684: Editor.exe, Runtime.exe and Tests.exe embed utilities/launch/Utf8CodePage.manifest, which
    // makes the process code page UTF-8 on Windows 10 version 1903 or later. argv, getenv, the CRT's
    // narrow file functions, narrow std::filesystem paths and every -A Win32 call then take UTF-8
    // strings. Older Windows ignores the manifest and keeps the ANSI code page.

    // GetACP(): 65001 (CP_UTF8) when the manifest took effect.
    unsigned int activeCodePage() noexcept;

    // Narrow strings are UTF-8 in this process: the manifest took effect.
    bool processUsesUtf8() noexcept;

    // The console has an output code page of its own (the OEM page, e.g. 437), whatever the process
    // code page is, so UTF-8 written with printf or std::cerr would show as mojibake. While alive, this
    // switches the console's output code page to UTF-8; its destructor restores the previous one.
    //
    // Only when STDOUT is a console. When stdout is not one, EditorConsole::open (--console) leaves the
    // inherited console with FreeConsole, and the restore must never land on a different console. A
    // redirected stdout (a launcher's log file) leaves the console alone.
    //
    // The console may belong to the parent too (cmd, PowerShell): a crash, Ctrl+C or exit() skips the
    // destructor and leaves it at UTF-8.
    class Utf8ConsoleOutput
    {
    public:
        Utf8ConsoleOutput() noexcept;
        ~Utf8ConsoleOutput();

        Utf8ConsoleOutput(const Utf8ConsoleOutput&) = delete;
        Utf8ConsoleOutput& operator=(const Utf8ConsoleOutput&) = delete;

    private:
        unsigned int previousCodePage = 0;  // 0: nothing to restore
    };
}
