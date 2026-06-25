#pragma once

// CpuMemory is a SharedLib/DLL so its CpuMemoryManager singleton resolves to a
// single instance across every module that records or queries CPU RAM (the
// Import/Audio DLLs, Graphics-in-the-exe, the executables, the test runner).
// Mirrors VF_ECSREGISTRY_API / VF_THREADING_API / VF_ASSETDB_API.

#ifdef VF_CPUMEMORY_BUILD_DLL
    #define VF_CPUMEMORY_API __declspec(dllexport)
#else
    #define VF_CPUMEMORY_API __declspec(dllimport)
#endif
