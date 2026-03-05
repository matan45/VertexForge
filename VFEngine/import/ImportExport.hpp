#pragma once

#ifdef VF_IMPORT_BUILD_DLL
    #define VF_IMPORT_API __declspec(dllexport)
#else
    #define VF_IMPORT_API __declspec(dllimport)
#endif
