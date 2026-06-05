#pragma once

#ifdef VF_GAMEEXPORT_BUILD_DLL
    #define VF_GAMEEXPORT_API __declspec(dllexport)
#else
    #define VF_GAMEEXPORT_API __declspec(dllimport)
#endif
