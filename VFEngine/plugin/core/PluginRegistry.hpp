#pragma once
#include "../api/IPlugin.hpp"
#include "DynamicLibrary.hpp"
#include "PluginContextImpl.hpp"
#include <vector>
#include <memory>
#include <string>

namespace plugin {

    struct LoadedPlugin
    {
        std::unique_ptr<DynamicLibrary> library;
        IPlugin* instance = nullptr;
        std::unique_ptr<PluginContextImpl> context;
        std::string dllPath;
        PluginInfo info;
        bool initialized = false;

        // Function pointers from the DLL
        using DestroyFunc = void(*)(IPlugin*);
        DestroyFunc destroyFunc = nullptr;
    };

    class PluginRegistry
    {
    public:
        PluginRegistry() = default;
        ~PluginRegistry() = default;

        void addPlugin(LoadedPlugin plugin);
        std::vector<LoadedPlugin>& getPlugins();
        const std::vector<LoadedPlugin>& getPlugins() const;
        size_t getPluginCount() const;
        void clear();

    private:
        std::vector<LoadedPlugin> plugins;
    };

}
