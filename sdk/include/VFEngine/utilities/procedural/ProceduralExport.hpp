#pragma once

#ifdef VF_PROCEDURAL_BUILD_DLL
    #define VF_PROCEDURAL_API __declspec(dllexport)
#else
    #define VF_PROCEDURAL_API __declspec(dllimport)
#endif
