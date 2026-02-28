#include "PluginManager.hpp"
#include "../api/PluginVersion.hpp"
#include "Pipeline.hpp"
#include "print/EditorLogger.hpp"

namespace plugin {

    PluginManager::PluginManager(std::unordered_set<std::string> capabilities)
        : capabilities(std::move(capabilities))
    {
    }

    PluginManager::~PluginManager()
    {
        shutdownAll();
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

        vfLogInfo("Loaded {} plugin(s)", plugins.size());
    }

    bool PluginManager::loadPlugin(const std::filesystem::path& dllPath)
    {
        vfLogInfo("Loading plugin: {}", dllPath.filename().string());

        auto library = std::make_unique<DynamicLibrary>(dllPath);
        if (!library->isLoaded()) {
            vfLogError("Failed to load plugin DLL: {}", dllPath.string());
            return false;
        }

        if (!validatePluginVersion(*library)) {
            vfLogError("Plugin '{}' has incompatible API version", dllPath.filename().string());
            return false;
        }

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

        IPlugin* instance = createFunc();
        if (!instance) {
            vfLogError("Plugin '{}' vfCreatePlugin returned null", dllPath.filename().string());
            return false;
        }

        PluginInfo info = instance->getInfo();
        vfLogInfo("  Name: {} v{}.{}.{} by {}", info.name, info.versionMajor, info.versionMinor, info.versionPatch, info.author);

        LoadedPlugin loaded;
        loaded.library = std::move(library);
        loaded.instance = instance;
        loaded.dllPath = dllPath.string();
        loaded.info = info;
        loaded.destroyFunc = destroyFunc;
        loaded.initialized = false;

        plugins.push_back(std::move(loaded));
        return true;
    }

    void PluginManager::initializeAll()
    {
        for (auto& plugin : plugins) {
            if (plugin.initialized) {
                continue;
            }

            plugin.context = std::make_unique<PluginContextImpl>(plugin.info.name, capabilities);

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
                plugin.context->cleanupAll();
                plugin.context.reset();
                if (plugin.destroyFunc && plugin.instance) {
                    plugin.destroyFunc(plugin.instance);
                    plugin.instance = nullptr;
                }
            }
        }
    }

    void PluginManager::updateAll(float deltaTime)
    {
        for (auto& plugin : plugins) {
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

            if (plugin.context) {
                plugin.context->cleanupAll();
                plugin.context.reset();
            }

            if (plugin.destroyFunc && plugin.instance) {
                plugin.destroyFunc(plugin.instance);
                plugin.instance = nullptr;
            }
        }

        plugins.clear();
    }

    const std::vector<LoadedPlugin>& PluginManager::getLoadedPlugins() const
    {
        return plugins;
    }

    size_t PluginManager::getPluginCount() const
    {
        return plugins.size();
    }

    std::vector<std::unique_ptr<pipeline::PipelineStage>> PluginManager::takeAllImportStages()
    {
        std::vector<std::unique_ptr<pipeline::PipelineStage>> allStages;
        for (auto& plugin : plugins) {
            if (plugin.initialized && plugin.context) {
                auto stages = plugin.context->takeImportStages();
                for (auto& stage : stages) {
                    allStages.push_back(std::move(stage));
                }
            }
        }
        return allStages;
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
