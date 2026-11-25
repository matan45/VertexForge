#pragma once
#include "../api/IPlugin.hpp"
#include "../api/PluginDescriptor.hpp"
#include "DynamicLibrary.hpp"
#include "PluginContextImpl.hpp"
#include "PluginRoots.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>
#include <memory>

namespace plugin {

    // VK-1732: where plugins load from and where their per-user state lives. The dev tree (and the
    // Runtime) set only engineRoot, which gives the behaviour from before VK-1732.
    struct PluginLoadConfig
    {
        // The shipped plugins (resolvePluginsDirectory()), scanned recursively.
        std::filesystem::path engineRoot;
        // An installed engine's user plugins (Documents\VertexForge\Plugins), scanned after the engine
        // root at <userRoot>/<Folder>/*.vfplugin only. Empty: none.
        std::filesystem::path userRoot;
        // The base of PluginContext::getPluginDataPath(). Empty: <engineRoot>/data.
        std::filesystem::path dataRoot;
        // The Plugin Manager's choices for shipped plugins (PluginRoots.hpp). Empty: a toggle edits the
        // shipped .vfplugin in place.
        std::filesystem::path overridesFile;
    };

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
        PluginLoadConfig loadConfig;
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

        // Loads every plugin of both roots. Per descriptor, in order: it must parse, must not be shadowed
        // (a shipped plugin wins over a user's with its name; the first by path wins within a root),
        // must be enabled (the overrides file decides for a shipped plugin), must match the API version,
        // and its DLL must exist. DLLs without a descriptor load (legacy) from the engine root's top level
        // only. A Newer or Invalid overrides file is logged and ignored.
        void loadAll(const PluginLoadConfig& config);
        // The engine root only (the Runtime): the behaviour from before VK-1732.
        void loadAll(const std::filesystem::path& pluginDirectory);
        void initializeAll();
        void updateAll(float deltaTime);
        void shutdownAll();

        const std::vector<LoadedPlugin>& getLoadedPlugins() const;
        size_t getPluginCount() const;
        // The engine root, once loadAll found it (empty when it does not exist).
        const std::filesystem::path& getPluginsDirectory() const { return pluginsDirectory; }
        const PluginLoadConfig& getLoadConfig() const { return loadConfig; }
        const std::filesystem::path& getUserPluginsDirectory() const { return loadConfig.userRoot; }
        // The folder holding every plugin's data folder: dataRoot, else <engine root>/data; empty before
        // loadAll found either.
        std::filesystem::path getPluginDataRoot() const;
        // Shipped plugins keep the Plugin Manager's choices in the overrides file (an installed engine).
        bool usesEnabledOverrides() const { return !loadConfig.overridesFile.empty(); }

        // VK-1732: rescans both roots and re-reads the overrides file (every descriptor that parses, in
        // load-scan order: the engine root's sorted by path, then the user root's).
        std::vector<DiscoveredPlugin> discoverPlugins() const;
        // Takes effect at the next start. A shipped plugin with overrides in use: the explicit choice goes
        // into the overrides file. Otherwise the plugin's own descriptor is edited in place (member order,
        // a BOM, CRLF and the final newline kept). Both are replaced atomically through "<file>.tmp".
        // Returns an error message, empty on success.
        std::string setPluginEnabled(const DiscoveredPlugin& plugin, bool enabled);
        // Removes a shipped plugin's override, so its descriptor decides again. Empty on success.
        std::string resetPluginEnabled(const std::string& name);
        static const PluginManager* getActive() { return activeInstance; }
        // VK-1365: mutable access for the Plugin Manager window's live toggles.
        static PluginManager* getActiveMutable() { return activeInstance; }
        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeAllImportStages();
        // Plugin-registered asset importers paired with the owning plugin's
        // name (used as the ImporterRegistry owner tag for unload cleanup).
        std::vector<std::pair<std::string, std::unique_ptr<import::AssetImporter>>> takeAllAssetImporters();

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
        struct DescriptorFile
        {
            std::filesystem::path path;
            PluginRootKind root = PluginRootKind::Engine;
        };
        std::vector<DescriptorFile> listDescriptorFiles() const;
        PluginOverrides readOverrides() const;
        std::vector<DiscoveredPlugin> discover(const PluginOverrides& overrides,
                                               std::vector<std::filesystem::path>* unparsed) const;

        bool loadPlugin(const PluginDescriptor& descriptor);
        bool loadPluginFromDll(const std::filesystem::path& dllPath);
        bool validatePluginVersion(DynamicLibrary& lib) const;
        std::vector<PluginDescriptor> resolveLoadOrder(std::vector<PluginDescriptor>& descriptors);
        void subscribeSceneEvents();
        void unsubscribeSceneEvents();
    };

}
