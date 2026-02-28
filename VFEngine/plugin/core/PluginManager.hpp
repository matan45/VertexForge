#pragma once
#include "../api/IPlugin.hpp"
#include "DynamicLibrary.hpp"
#include "PluginContextImpl.hpp"
#include <filesystem>
#include <unordered_set>
#include <string>
#include <vector>
#include <memory>

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

    class PluginManager
    {
    public:
        explicit PluginManager(std::unordered_set<std::string> capabilities);
        ~PluginManager();

        // Discover and load all plugin DLLs from the given directory.
        void loadAll(const std::filesystem::path& pluginDirectory);

        // Load a single plugin DLL.
        bool loadPlugin(const std::filesystem::path& dllPath);

        // Initialize all loaded plugins (call onInitialize with context).
        void initializeAll();

        // Called each frame.
        void updateAll(float deltaTime);

        // Shutdown and unload all plugins in reverse order.
        void shutdownAll();

        // Query loaded plugins.
        const std::vector<LoadedPlugin>& getLoadedPlugins() const;
        size_t getPluginCount() const;

        // Collect and take ownership of all plugin-registered import stages.
        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeAllImportStages();

    private:
        std::vector<LoadedPlugin> plugins;
        std::unordered_set<std::string> capabilities;

        bool validatePluginVersion(DynamicLibrary& lib) const;
    };

}
