#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

// Process-wide crash capture for VertexForge.
//
// NOTE: this header deliberately exposes only plain declarations. All Win32 /
// DbgHelp includes live in CrashHandler.cpp so that including this from the
// editor/runtime entry points never drags <windows.h>/<DbgHelp.h> (and their
// APIENTRY macro) into translation units that have their own Vulkan/Win32
// header-ordering expectations (see the #undef APIENTRY at the top of Log.hpp).
namespace util
{
    // Installs an unhandled-exception filter that, on a hard crash (access
    // violation, stack overflow, illegal instruction, ...), writes:
    //   crash_YYYYMMDD_HHMMSS_<pid>.dmp  - a minidump openable in Visual Studio
    //   crash_YYYYMMDD_HHMMSS_<pid>.txt  - exception code/address + symbolized stack (UTF-8)
    // into the crash folder ("crashes/" under the working directory, or the folder setCrashDirectory
    // gave), shows a message box with the dump's absolute path, then terminates. The process id in the
    // name (VK-1732) keeps two processes that crash in the same second, in a shared folder, apart, and
    // lets the launcher pick its own editor's report.
    // Idempotent. Call as the very first statement in main().
    void installCrashHandler();

    // VK-1732: the longest crash folder setCrashDirectory takes, in UTF-16 units, so that the folder
    // plus the longest file name ("crash_" + 15 + "_" + 10 + ".dmp") stays under MAX_PATH.
    inline constexpr std::size_t maxCrashDirectoryUnits = 220;

    // VK-1732: writes the crash files into `folder` (absolute, at most maxCrashDirectoryUnits UTF-16
    // units) instead of "crashes/" under the working directory; its missing folders are created when a
    // crash is written. Returns false, and changes nothing, for a relative or too long folder. Call at
    // startup, before any thread can fault: the filter reads the folder without a lock.
    bool setCrashDirectory(const std::filesystem::path& folder);

    // Records a breadcrumb (e.g. the script/entity currently executing) that is
    // written into the crash .txt if the process faults. This is the one
    // mechanism that attributes an *uncatchable* native fault (e.g. an mType JIT
    // access violation) to the script that was running. Cheap and allocation-free
    // internally; safe to call on a hot per-frame path.
    void setCrashLogContext(const std::string& context);

    // Called first when the crash filter runs, before the minidump, the report and the message box
    // (which keeps the process alive until someone closes it). VK-1682: the editor writes "failed"
    // into its --launch-status file from here, so a launcher learns of the crash at once.
    // `exceptionCode` is also the process exit code; `dumpPath` is the dump about to be written, in
    // UTF-8: absolute once setCrashDirectory took a folder, else relative to the working directory.
    // Runs on the faulting thread, possibly with the heap corrupt: it must not allocate, lock or
    // throw. nullptr (the default) disables it.
    using CrashNotifier = void (*)(unsigned long exceptionCode, const char* exceptionLabel,
                                   const char* dumpPath) noexcept;
    void setCrashNotifier(CrashNotifier notifier) noexcept;
}
