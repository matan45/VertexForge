#pragma once

#ifdef VF_AUDIO_BUILD_DLL
    #define VF_AUDIO_API __declspec(dllexport)
#else
    #define VF_AUDIO_API __declspec(dllimport)
#endif
