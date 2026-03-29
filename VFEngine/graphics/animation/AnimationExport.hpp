#pragma once

#ifdef VF_ANIMATION_BUILD_DLL
    #define VF_ANIMATION_API __declspec(dllexport)
#else
    #define VF_ANIMATION_API __declspec(dllimport)
#endif
