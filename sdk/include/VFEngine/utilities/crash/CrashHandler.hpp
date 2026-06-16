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
}
