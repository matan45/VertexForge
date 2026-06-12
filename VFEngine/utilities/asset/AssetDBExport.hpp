#pragma once

#ifdef VF_ASSETDB_BUILD_DLL
    #define VF_ASSETDB_API __declspec(dllexport)
#else
    #define VF_ASSETDB_API __declspec(dllimport)
#endif
