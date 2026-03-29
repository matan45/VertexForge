#pragma once

#ifdef VF_WORLD_BUILD_DLL
    #define VF_WORLD_API __declspec(dllexport)
#else
    #define VF_WORLD_API __declspec(dllimport)
#endif
