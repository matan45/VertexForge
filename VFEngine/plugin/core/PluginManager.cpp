#include "PluginManager.hpp"
#include "../api/PluginVersion.hpp"
#include "print/EditorLogger.hpp"

namespace plugin {

    PluginManager::PluginManager() = default;

    PluginManager::~PluginManager()
    {
        shutdownAll();
    }

    void PluginManager::setCapabilities(const std::unordered_set<std::string>& caps)
    {
        capabilities = caps;
    }

    void PluginManager::loadAll(const std::filesystem::path& pluginDirectory)
    {
        if (!std::filesystem::exists(pluginDirectory)) {
            vfLogInfo("Plugin directory '{}' does not exist, skipping plugin loading", pluginDirectory.string());
            return;
        }

        vfLogInfo("Scanning for plugins in '{}'...", pluginDirectory.string());

        for (const auto& entry : std::filesystem::directory_iterator(pluginDirectory)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto& path = entry.path();
#ifdef _WIN32
            if (path.extension() != ".dll") {
                continue;
            }
#else
            if (path.extension() != ".so") {
                continue;
            }
#endif
            loadPlugin(path);
        }

        vfLogInfo("Loaded {} plugin(s)", registry.getPluginCount());
    }

    bool PluginManager::loadPlugin(const std::filesystem::path& dllPath)
    {
        vfLogInfo("Loading plugin: {}", dllPath.filename().string());

        // Load the DLL
        auto library = std::make_unique<DynamicLibrary>(dllPath);
        if (!library->isLoaded()) {
            vfLogError("Failed to load plugin DLL: {}", dllPath.string());
            return false;
        }

        // Validate API version
        if (!validatePluginVersion(*library)) {
            vfLogError("Plugin '{}' has incompatible API version", dllPath.filename().string());
            return false;
        }

        // Get factory and destroy functions
        auto createFunc = library->getFunction<IPlugin*(*)()>("vfCreatePlugin");
        if (!createFunc) {
            vfLogError("Plugin '{}' missing vfCreatePlugin export", dllPath.filename().string());
            return false;
        }

        auto destroyFunc = library->getFunction<void(*)(IPlugin*)>("vfDestroyPlugin");
        if (!destroyFunc) {
            vfLogError("Plugin '{}' missing vfDestroyPlugin export", dllPath.filename().string());
            return false;
        }

        // Create plugin instance
        IPlugin* instance = createFunc();
        if (!instance) {
            vfLogError("Plugin '{}' vfCreatePlugin returned null", dllPath.filename().string());
            return false;
        }

        // Get plugin info
        PluginInfo info = instance->getInfo();
        vfLogInfo("  Name: {} v{}.{}.{} by {}", info.name, info.versionMajor, info.versionMinor, info.versionPatch, info.author);

        // Register the plugin
        LoadedPlugin loaded;
        loaded.library = std::move(library);
        loaded.instance = instance;
        loaded.dllPath = dllPath.string();
        loaded.info = info;
        loaded.destroyFunc = destroyFunc;
        loaded.initialized = false;

        registry.addPlugin(std::move(loaded));
        return true;
    }

    void PluginManager::initializeAll()
    {
        for (auto& plugin : registry.getPlugins()) {
            if (plugin.initialized) {
                continue;
            }

            // Create context for this plugin
            plugin.context = std::make_unique<PluginContextImpl>(plugin.info.name, capabilities);

            // Initialize the plugin
            bool success = false;
            try {
                success = plugin.instance->onInitialize(plugin.context.get());
            }
            catch (const std::exception& e) {
                vfLogError("Plugin '{}' threw exception during initialization: {}", plugin.info.name, e.what());
            }

            if (success) {
                plugin.initialized = true;
                vfLogInfo("Plugin '{}' initialized successfully", plugin.info.name);
            }
            else {
                vfLogError("Plugin '{}' initialization failed", plugin.info.name);
                // Cleanup the failed plugin's context
                plugin.context->cleanupAll();
                plugin.context.reset();
                // Destroy the plugin instance
                if (plugin.destroyFunc && plugin.instance) {
                    plugin.destroyFunc(plugin.instance);
                    plugin.instance = nullptr;
                }
            }
        }
    }

    void PluginManager::updateAll(float deltaTime)
    {
        for (auto& plugin : registry.getPlugins()) {
            if (!plugin.initialized || !plugin.instance) {
                continue;
            }

            try {
                plugin.instance->onUpdate(deltaTime);
            }
            catch (const std::exception& e) {
                vfLogError("Plugin '{}' threw exception during update: {}", plugin.info.name, e.what());
            }
        }
    }

    void PluginManager::shutdownAll()
    {
        // Shutdown in reverse order
        auto& plugins = registry.getPlugins();
        for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
            auto& plugin = *it;

            if (plugin.initialized && plugin.instance) {
                vfLogInfo("Shutting down plugin: {}", plugin.info.name);
                try {
                    plugin.instance->onShutdown();
                }
                catch (const std::exception& e) {
                    vfLogError("Plugin '{}' threw exception during shutdown: {}", plugin.info.name, e.what());
                }
                plugin.initialized = false;
            }

            // Clean up context (unsubscribes events, removes windows)
            if (plugin.context) {
                plugin.context->cleanupAll();
                plugin.context.reset();
            }

            // Destroy plugin instance via the DLL's destroy function
            if (plugin.destroyFunc && plugin.instance) {
                plugin.destroyFunc(plugin.instance);
                plugin.instance = nullptr;
            }

            // DynamicLibrary destructor will call FreeLibrary
        }

        registry.clear();
    }

    const std::vector<LoadedPlugin>& PluginManager::getLoadedPlugins() const
    {
        return registry.getPlugins();
    }

    size_t PluginManager::getPluginCount() const
    {
        return registry.getPluginCount();
    }

    bool PluginManager::validatePluginVersion(DynamicLibrary& lib) const
    {
        auto getVersionFunc = lib.getFunction<uint32_t(*)()>("vfGetPluginAPIVersion");
        if (!getVersionFunc) {
            vfLogError("Plugin missing vfGetPluginAPIVersion export");
            return false;
        }

        uint32_t pluginVersion = getVersionFunc();
        if (pluginVersion != VF_PLUGIN_API_VERSION) {
            vfLogError("Plugin API version mismatch: plugin={}, engine={}", pluginVersion, VF_PLUGIN_API_VERSION);
            return false;
        }

        return true;
    }

}
