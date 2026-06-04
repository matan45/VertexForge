#pragma once
#include "../api/IPlugin.hpp"
#include "../api/PluginDescriptor.hpp"
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
        PluginDescriptor descriptor;
        bool initialized = false;

        using DestroyFunc = void(*)(IPlugin*);
        DestroyFunc destroyFunc = nullptr;
    };

    class PluginManager
    {
    private:
        std::vector<LoadedPlugin> plugins;
        std::unordered_set<std::string> capabilities;
        std::filesystem::path pluginsDirectory;
        inline static PluginManager* activeInstance = nullptr;
    public:
        explicit PluginManager(std::unordered_set<std::string> capabilities);
        ~PluginManager();

        void loadAll(const std::filesystem::path& pluginDirectory);
        void initializeAll();
        void updateAll(float deltaTime);
        void shutdownAll();

        const std::vector<LoadedPlugin>& getLoadedPlugins() const;
        size_t getPluginCount() const;
        const std::filesystem::path& getPluginsDirectory() const { return pluginsDirectory; }
        static const PluginManager* getActive() { return activeInstance; }
        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeAllImportStages();

    private:
        bool loadPlugin(const PluginDescriptor& descriptor);
        bool loadPluginFromDll(const std::filesystem::path& dllPath);
        bool validatePluginVersion(DynamicLibrary& lib) const;
        std::vector<PluginDescriptor> resolveLoadOrder(std::vector<PluginDescriptor>& descriptors);
    };

}
