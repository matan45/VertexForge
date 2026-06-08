#pragma once
#include "../api/IPlugin.hpp"
#include "../api/PluginDescriptor.hpp"
#include "DynamicLibrary.hpp"
#include "PluginContextImpl.hpp"
#include <filesystem>
#include <map>
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
        // VK-1365: runtime soft-disable state (DLL stays loaded). Loaded plugins
        // are globally enabled by definition (disabled ones are never loaded), so
        // the effective state is sceneOverride.value_or(true).
        bool active = true;

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
        // VK-1365: scene lifecycle subscriptions (apply per-scene plugin overrides)
        events::SubscriptionToken sceneLoadedToken;
        events::SubscriptionToken sceneClearedToken;
    public:
        explicit PluginManager(std::unordered_set<std::string> capabilities);
        ~PluginManager();

        // Locates the plugins directory relative to the EXECUTABLE (not the CWD —
        // IDE launchers set the working directory to the project folder). Tries
        // <exeDir>/plugins (deployed layout), then walks up the ancestors to find
        // the dev-tree root (bin/Editor/<Config>/x64 -> repo root). A candidate
        // counts only if it actually contains a .vfplugin descriptor, so stray
        // empty/data-only "plugins" folders can't hijack discovery.
        static std::filesystem::path resolvePluginsDirectory();

        void loadAll(const std::filesystem::path& pluginDirectory);
        void initializeAll();
        void updateAll(float deltaTime);
        void shutdownAll();

        const std::vector<LoadedPlugin>& getLoadedPlugins() const;
        size_t getPluginCount() const;
        const std::filesystem::path& getPluginsDirectory() const { return pluginsDirectory; }
        static const PluginManager* getActive() { return activeInstance; }
        // VK-1365: mutable access for the Plugin Manager window's live toggles.
        static PluginManager* getActiveMutable() { return activeInstance; }
        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeAllImportStages();

        // VK-1365 per-scene soft-disable. setPluginActive is idempotent and returns
        // true only on an actual transition (false for unloaded/uninitialized
        // plugins). Ordering: deactivate runs onDeactivate before suppressing the
        // plugin's channels; activate restores channels before onActivate.
        bool setPluginActive(const std::string& name, bool active);
        bool isPluginActive(const std::string& name) const;
        // Apply effective state per plugin: sceneOverride if present, else the
        // global flag (always enabled for loaded plugins).
        void applySceneActiveStates(const std::map<std::string, bool>& overrides);
        void resetActiveStatesToGlobal();

    private:
        bool loadPlugin(const PluginDescriptor& descriptor);
        bool loadPluginFromDll(const std::filesystem::path& dllPath);
        bool validatePluginVersion(DynamicLibrary& lib) const;
        std::vector<PluginDescriptor> resolveLoadOrder(std::vector<PluginDescriptor>& descriptors);
        void subscribeSceneEvents();
        void unsubscribeSceneEvents();
    };

}
