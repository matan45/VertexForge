#pragma once

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
    //   crashes/crash_YYYYMMDD_HHMMSS.dmp  - a minidump openable in Visual Studio
    //   crashes/crash_YYYYMMDD_HHMMSS.txt  - exception code/address + symbolized stack
    // into a "crashes/" folder under the working directory, then terminates.
    // Idempotent. Call as the very first statement in main().
    void installCrashHandler();

    // Records a breadcrumb (e.g. the script/entity currently executing) that is
    // written into the crash .txt if the process faults. This is the one
    // mechanism that attributes an *uncatchable* native fault (e.g. an mType JIT
    // access violation) to the script that was running. Cheap and allocation-free
    // internally; safe to call on a hot per-frame path.
    void setCrashLogContext(const std::string& context);

    // Called first when the crash filter runs, before the minidump, the report and the message box
    // (which keeps the process alive until someone closes it). VK-1682: the editor writes "failed"
    // into its --launch-status file from here, so a launcher learns of the crash at once.
    // `exceptionCode` is also the process exit code; `dumpPath` is the dump about to be written
    // (relative to the working directory). Runs on the faulting thread, possibly with the heap
    // corrupt: it must not allocate, lock or throw. nullptr (the default) disables it.
    using CrashNotifier = void (*)(unsigned long exceptionCode, const char* exceptionLabel,
                                   const char* dumpPath) noexcept;
    void setCrashNotifier(CrashNotifier notifier) noexcept;
}
