#pragma once
#include "PluginVersion.hpp"

#ifdef _WIN32
    #define VF_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
    #define VF_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Convenience macro for implementing plugin entry points.
// Usage in a plugin DLL:
//
//   class MyPlugin : public plugin::IPlugin { ... };
//   VF_IMPLEMENT_PLUGIN(MyPlugin)
//
#define VF_IMPLEMENT_PLUGIN(PluginClass)                                       \
    VF_PLUGIN_EXPORT uint32_t vfGetPluginAPIVersion()                          \
    {                                                                          \
        return plugin::VF_PLUGIN_API_VERSION;                                  \
    }                                                                          \
    VF_PLUGIN_EXPORT plugin::IPlugin* vfCreatePlugin()                         \
    {                                                                          \
        return new PluginClass();                                              \
    }                                                                          \
    VF_PLUGIN_EXPORT void vfDestroyPlugin(plugin::IPlugin* p)                  \
    {                                                                          \
        delete p;                                                              \
    }
