#pragma once

#ifdef VF_ECSREGISTRY_BUILD_DLL
    #define VF_ECSREGISTRY_API __declspec(dllexport)
#else
    #define VF_ECSREGISTRY_API __declspec(dllimport)
#endif
