#pragma once

#ifdef VF_SERIALIZATION_BUILD_DLL
    #define VF_SERIALIZATION_API __declspec(dllexport)
#else
    #define VF_SERIALIZATION_API __declspec(dllimport)
#endif
