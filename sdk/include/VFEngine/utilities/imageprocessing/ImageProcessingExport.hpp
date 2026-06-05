#pragma once

#ifdef VF_IMAGEPROCESSING_BUILD_DLL
    #define VF_IMAGEPROCESSING_API __declspec(dllexport)
#else
    #define VF_IMAGEPROCESSING_API __declspec(dllimport)
#endif
