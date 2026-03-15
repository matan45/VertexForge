#include "print/Log.hpp"
#include "PluginManager.hpp"
#include "PluginSerializationBridge.hpp"
#include "../api/PluginVersion.hpp"
#include "Pipeline.hpp"
#include <algorithm>
#include <unordered_set>

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

        // Phase 1: Collect descriptors from .vfplugin files
        std::vector<PluginDescriptor> descriptors;
        std::unordered_set<std::string> dllsWithDescriptor;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(pluginDirectory)) {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().extension() == ".vfplugin")
            {
                auto desc = PluginDescriptor::loadFromFile(entry.path());
                if (!desc.has_value())
                {
                    vfLogError("Failed to parse plugin descriptor: {}", entry.path().string());
                    continue;
                }

                if (!desc->enabled)
                {
                    vfLogInfo("Plugin '{}' is disabled, skipping", desc->name);
                    continue;
                }

                if (desc->apiVersion != VF_PLUGIN_API_VERSION)
                {
                    vfLogError("Plugin '{}' API version mismatch: descriptor={}, engine={}",
                               desc->name, desc->apiVersion, VF_PLUGIN_API_VERSION);
                    continue;
                }

                auto libPath = desc->getLibraryPath();
                if (!std::filesystem::exists(libPath))
                {
                    vfLogError("Plugin '{}' library not found: {}", desc->name, libPath.string());
                    continue;
                }

                dllsWithDescriptor.insert(std::filesystem::canonical(libPath).string());
                descriptors.push_back(std::move(*desc));
            }
        }

        // Phase 2: Sort by loadOrder (ascending), then by name
        std::sort(descriptors.begin(), descriptors.end(),
            [](const PluginDescriptor& a, const PluginDescriptor& b)
            {
                if (a.loadOrder != b.loadOrder)
                    return a.loadOrder < b.loadOrder;
                return a.name < b.name;
            });

        // Phase 3: Validate dependencies
        std::unordered_set<std::string> availableNames;
        for (const auto& desc : descriptors)
            availableNames.insert(desc.name);

        std::vector<PluginDescriptor> validDescriptors;
        for (auto& desc : descriptors)
        {
            bool depsMet = true;
            for (const auto& dep : desc.dependencies)
            {
                if (!availableNames.contains(dep))
                {
                    vfLogError("Plugin '{}' has unmet dependency: '{}'", desc.name, dep);
                    depsMet = false;
                    break;
                }
            }
            if (depsMet)
                validDescriptors.push_back(std::move(desc));
        }

        // Phase 4: Load plugins from descriptors
        for (const auto& desc : validDescriptors)
        {
            loadPlugin(desc);
        }

        // Phase 5: Backward compatibility — load orphan DLLs without descriptors
        for (const auto& entry : std::filesystem::directory_iterator(pluginDirectory)) {
            if (!entry.is_regular_file())
                continue;

            const auto& path = entry.path();
            if (path.extension() != ".dll")
                continue;
            auto canonical = std::filesystem::canonical(path).string();
            if (dllsWithDescriptor.contains(canonical))
                continue;

            vfLogWarning("Plugin '{}' has no .vfplugin descriptor, loading with legacy method",
                         path.filename().string());
            loadPluginFromDll(path);
        }

        vfLogInfo("Loaded {} plugin(s)", plugins.size());
    }

    bool PluginManager::loadPlugin(const PluginDescriptor& descriptor)
    {
        auto dllPath = descriptor.getLibraryPath();
        vfLogInfo("Loading plugin: {} v{} ({})", descriptor.name, descriptor.version,
                  dllPath.filename().string());

        auto library = std::make_unique<DynamicLibrary>(dllPath);
        if (!library->isLoaded()) {
            vfLogError("Failed to load plugin DLL: {}", dllPath.string());
            return false;
        }

        auto createFunc = library->getFunction<IPlugin*(*)()>("vfCreatePlugin");
        if (!createFunc) {
            vfLogError("Plugin '{}' missing vfCreatePlugin export", descriptor.name);
            return false;
        }

        auto destroyFunc = library->getFunction<void(*)(IPlugin*)>("vfDestroyPlugin");
        if (!destroyFunc) {
            vfLogError("Plugin '{}' missing vfDestroyPlugin export", descriptor.name);
            return false;
        }

        IPlugin* instance = createFunc();
        if (!instance) {
            vfLogError("Plugin '{}' vfCreatePlugin returned null", descriptor.name);
            return false;
        }

        PluginInfo info = instance->getInfo();

        // Cross-check descriptor metadata against runtime info
        if (info.name != descriptor.name)
        {
            vfLogWarning("Plugin name mismatch: descriptor='{}', runtime='{}'",
                         descriptor.name, info.name);
        }

        vfLogInfo("  Name: {} v{}.{}.{} by {}", info.name,
                  info.versionMajor, info.versionMinor, info.versionPatch, info.author);

        LoadedPlugin loaded;
        loaded.library = std::move(library);
        loaded.instance = instance;
        loaded.dllPath = dllPath.string();
        loaded.info = info;
        loaded.descriptor = descriptor;
        loaded.destroyFunc = destroyFunc;
        loaded.initialized = false;

        plugins.push_back(std::move(loaded));
        return true;
    }

    bool PluginManager::loadPluginFromDll(const std::filesystem::path& dllPath)
    {
        vfLogInfo("Loading plugin (legacy): {}", dllPath.filename().string());

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
        vfLogInfo("  Name: {} v{}.{}.{} by {}", info.name,
                  info.versionMajor, info.versionMinor, info.versionPatch, info.author);

        // Build a descriptor from runtime info for consistency
        PluginDescriptor desc;
        desc.name = info.name;
        desc.version = std::to_string(info.versionMajor) + "." +
                       std::to_string(info.versionMinor) + "." +
                       std::to_string(info.versionPatch);
        desc.apiVersion = VF_PLUGIN_API_VERSION;
        desc.author = info.author;
        desc.description = info.description;
        desc.library = dllPath.filename().string();
        desc.basePath = dllPath.parent_path();
        desc.versionMajor = info.versionMajor;
        desc.versionMinor = info.versionMinor;
        desc.versionPatch = info.versionPatch;

        LoadedPlugin loaded;
        loaded.library = std::move(library);
        loaded.instance = instance;
        loaded.dllPath = dllPath.string();
        loaded.info = info;
        loaded.descriptor = desc;
        loaded.destroyFunc = destroyFunc;
        loaded.initialized = false;

        plugins.push_back(std::move(loaded));
        return true;
    }

    void PluginManager::initializeAll()
    {
        PluginSerializationBridge::init();

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

        PluginSerializationBridge::shutdown();
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
