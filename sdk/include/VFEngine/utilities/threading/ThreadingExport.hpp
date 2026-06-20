#pragma once

#ifdef VF_THREADING_BUILD_DLL
    #define VF_THREADING_API __declspec(dllexport)
#else
    #define VF_THREADING_API __declspec(dllimport)
#endif
