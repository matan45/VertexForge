#pragma once

#ifdef VF_TERRAIN_BUILD_DLL
    #define VF_TERRAIN_API __declspec(dllexport)
#else
    #define VF_TERRAIN_API __declspec(dllimport)
#endif
