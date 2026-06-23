#include "ProcessMemoryProbe.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace windows
{
    ProcessMemoryInfo queryProcessMemory()
    {
        ProcessMemoryInfo info;
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            info.workingSetBytes = static_cast<uint64_t>(pmc.WorkingSetSize);

        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem))
        {
            info.systemTotalBytes = static_cast<uint64_t>(mem.ullTotalPhys);
            info.systemAvailBytes = static_cast<uint64_t>(mem.ullAvailPhys);
        }
#endif
        return info;
    }
}
