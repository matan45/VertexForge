#pragma once
#include "../api/IPlugin.hpp"
#include "DynamicLibrary.hpp"
#include "PluginContextImpl.hpp"
#include <filesystem>
#include <unordered_set>
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

        using DestroyFunc = void(*)(IPlugin*);
        DestroyFunc destroyFunc = nullptr;
    };

    class PluginManager
    {
    private:
        std::vector<LoadedPlugin> plugins;
        std::unordered_set<std::string> capabilities;
    public:
        explicit PluginManager(std::unordered_set<std::string> capabilities);
        ~PluginManager();

        void loadAll(const std::filesystem::path& pluginDirectory);
        bool loadPlugin(const std::filesystem::path& dllPath);
        void initializeAll();
        void updateAll(float deltaTime);
        void shutdownAll();

        const std::vector<LoadedPlugin>& getLoadedPlugins() const;
        size_t getPluginCount() const;
        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeAllImportStages();

    private:
        bool validatePluginVersion(DynamicLibrary& lib) const;
    };

}
