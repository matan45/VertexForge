#pragma once
#include <cstdint>

// VK-1434: read-only OS process/system memory readout for the Memory Diagnostics
// CPU tab. Lets the user see total process working set (RSS) vs the bytes the
// CpuMemory system actually tracks — i.e. how much RAM escapes the budget
// (third-party libraries, small heap allocations, fragmentation). Editor-only.
//
// The implementation is isolated in a .cpp so <windows.h>/<psapi.h> (and their
// min/max macros) never leak into the ImGui window translation unit.

namespace windows
{
    struct ProcessMemoryInfo
    {
        uint64_t workingSetBytes = 0;   // process resident set (0 if unavailable)
        uint64_t systemTotalBytes = 0;  // total physical RAM
        uint64_t systemAvailBytes = 0;  // available physical RAM
    };

    ProcessMemoryInfo queryProcessMemory();
}
