#include "print/Log.hpp"
#include "PluginManager.hpp"
#include "PluginContextImpl.hpp"
#include "../api/PluginVersion.hpp"
#include "serialization/SceneSerialization.hpp"
#include "scene/EntityRegistry.hpp"
#include "Pipeline.hpp"
#include <algorithm>
#include <unordered_set>
#include <cassert>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace plugin {

    PluginManager::PluginManager(std::unordered_set<std::string> capabilities)
        : capabilities(std::move(capabilities))
    {
        assert(activeInstance == nullptr && "Only one PluginManager instance may exist at a time");
        activeInstance = this;
    }

    PluginManager::~PluginManager()
    {
        shutdownAll();
        activeInstance = nullptr;
    }

    void PluginManager::loadAll(const std::filesystem::path& pluginDirectory)
    {
        if (!std::filesystem::exists(pluginDirectory)) {
            vfLogInfo("Plugin directory '{}' does not exist, skipping plugin loading", pluginDirectory.string());
            return;
        }

        vfLogInfo("Scanning for plugins in '{}'...", pluginDirectory.string());
        pluginsDirectory = pluginDirectory;

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

        // Phase 2+3: Topological sort with dependency validation and cycle detection
        auto sortedDescriptors = resolveLoadOrder(descriptors);

        // Phase 4: Load plugins from descriptors
        for (const auto& desc : sortedDescriptors)
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

    std::vector<PluginDescriptor> PluginManager::resolveLoadOrder(std::vector<PluginDescriptor>& descriptors)
    {
        // Build name → descriptor index map
        std::unordered_map<std::string, size_t> nameToIndex;
        for (size_t i = 0; i < descriptors.size(); ++i)
            nameToIndex[descriptors[i].name] = i;

        // Remove plugins with missing dependencies
        std::vector<PluginDescriptor> valid;
        for (auto& desc : descriptors)
        {
            bool depsMet = true;
            for (const auto& dep : desc.dependencies)
            {
                if (!nameToIndex.contains(dep))
                {
                    vfLogError("Plugin '{}' has unmet dependency: '{}', skipping", desc.name, dep);
                    depsMet = false;
                    break;
                }
            }
            if (depsMet)
                valid.push_back(std::move(desc));
        }

        // Rebuild index map for valid set
        nameToIndex.clear();
        for (size_t i = 0; i < valid.size(); ++i)
            nameToIndex[valid[i].name] = i;

        // Kahn's algorithm: compute in-degrees
        std::vector<int> inDegree(valid.size(), 0);
        // adjacency: plugin index → list of indices that depend on it
        std::vector<std::vector<size_t>> dependents(valid.size());

        for (size_t i = 0; i < valid.size(); ++i)
        {
            for (const auto& dep : valid[i].dependencies)
            {
                auto it = nameToIndex.find(dep);
                if (it != nameToIndex.end())
                {
                    dependents[it->second].push_back(i);
                    inDegree[i]++;
                }
            }
        }

        // Seed queue with zero in-degree plugins, sorted by loadOrder then name
        std::vector<size_t> queue;
        for (size_t i = 0; i < valid.size(); ++i)
        {
            if (inDegree[i] == 0)
                queue.push_back(i);
        }

        std::vector<PluginDescriptor> sorted;
        sorted.reserve(valid.size());

        while (!queue.empty())
        {
            // Sort current wave by loadOrder, then name
            std::sort(queue.begin(), queue.end(), [&](size_t a, size_t b)
            {
                if (valid[a].loadOrder != valid[b].loadOrder)
                    return valid[a].loadOrder < valid[b].loadOrder;
                return valid[a].name < valid[b].name;
            });

            std::vector<size_t> nextQueue;
            for (size_t idx : queue)
            {
                sorted.push_back(std::move(valid[idx]));

                for (size_t dependent : dependents[idx])
                {
                    inDegree[dependent]--;
                    if (inDegree[dependent] == 0)
                        nextQueue.push_back(dependent);
                }
            }
            queue = std::move(nextQueue);
        }

        // Cycle detection: any remaining plugins with in-degree > 0
        if (sorted.size() < valid.size())
        {
            for (size_t i = 0; i < valid.size(); ++i)
            {
                if (inDegree[i] > 0 && !valid[i].name.empty())
                {
                    vfLogError("Plugin '{}' is part of a dependency cycle, skipping", valid[i].name);
                }
            }
        }

        return sorted;
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

    static nlohmann::json serializeMetaAny(const entt::meta_any& value, const entt::meta_type& type)
    {
        if (type.info() == entt::type_id<int>()) return value.cast<int>();
        if (type.info() == entt::type_id<float>()) return value.cast<float>();
        if (type.info() == entt::type_id<bool>()) return value.cast<bool>();
        if (type.info() == entt::type_id<std::string>()) return value.cast<std::string>();
        if (type.info() == entt::type_id<glm::vec2>()) {
            auto v = value.cast<glm::vec2>();
            return nlohmann::json::array({v.x, v.y});
        }
        if (type.info() == entt::type_id<glm::vec3>()) {
            auto v = value.cast<glm::vec3>();
            return nlohmann::json::array({v.x, v.y, v.z});
        }
        if (type.info() == entt::type_id<glm::vec4>()) {
            auto v = value.cast<glm::vec4>();
            return nlohmann::json::array({v.x, v.y, v.z, v.w});
        }
        if (type.info() == entt::type_id<glm::quat>()) {
            auto q = value.cast<glm::quat>();
            return nlohmann::json::array({q.x, q.y, q.z, q.w});
        }
        return nullptr;
    }

    static void deserializeMetaData(entt::meta_data data, entt::meta_any& instance, const nlohmann::json& value)
    {
        auto type = data.type();
        if (type.info() == entt::type_id<int>() && value.is_number_integer())
            data.set(instance, value.get<int>());
        else if (type.info() == entt::type_id<float>() && value.is_number())
            data.set(instance, value.get<float>());
        else if (type.info() == entt::type_id<bool>() && value.is_boolean())
            data.set(instance, value.get<bool>());
        else if (type.info() == entt::type_id<std::string>() && value.is_string())
            data.set(instance, value.get<std::string>());
        else if (type.info() == entt::type_id<glm::vec2>() && value.is_array() && value.size() >= 2)
            data.set(instance, glm::vec2(value[0].get<float>(), value[1].get<float>()));
        else if (type.info() == entt::type_id<glm::vec3>() && value.is_array() && value.size() >= 3)
            data.set(instance, glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
        else if (type.info() == entt::type_id<glm::vec4>() && value.is_array() && value.size() >= 4)
            data.set(instance, glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()));
        else if (type.info() == entt::type_id<glm::quat>() && value.is_array() && value.size() >= 4)
            data.set(instance, glm::quat(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
    }

    void PluginManager::initializeAll()
    {
        // Set up plugin component serialization hooks
        serialization::SceneSerialization::setPluginSerializationHooks(
            // Serialize
            [](entt::registry& reg, entt::entity entity) -> nlohmann::json {
                nlohmann::json result = nlohmann::json::object();
                auto& bridges = PluginContextImpl::getAllBridges();
                for (const auto& bridge : bridges)
                {
                    if (!bridge.has(reg, entity)) continue;
                    void* ptr = bridge.tryGet(reg, entity);
                    if (!ptr || !bridge.metaType) continue;

                    auto instance = bridge.metaType.from_void(ptr);
                    if (!instance) continue;

                    nlohmann::json compJson = nlohmann::json::object();
                    for (auto&& [id, member] : bridge.metaType.data())
                    {
                        auto val = member.get(instance);
                        if (!val) continue;
                        const char* name = member.name();
                        if (!name) continue;
                        auto serialized = serializeMetaAny(val, member.type());
                        if (!serialized.is_null())
                            compJson[name] = std::move(serialized);
                    }
                    if (!compJson.empty())
                        result["plugin:" + std::string(bridge.name)] = std::move(compJson);
                }
                return result;
            },
            // Deserialize
            [](const nlohmann::json& componentsJson, entt::registry& reg, entt::entity entity) {
                auto& bridges = PluginContextImpl::getAllBridges();
                for (const auto& bridge : bridges)
                {
                    std::string key = "plugin:" + std::string(bridge.name);
                    if (!componentsJson.contains(key)) continue;

                    const auto& compJson = componentsJson[key];
                    if (!compJson.is_object()) continue;

                    bridge.emplace(reg, entity);
                    void* ptr = bridge.tryGet(reg, entity);
                    if (!ptr || !bridge.metaType) continue;

                    auto instance = bridge.metaType.from_void(ptr);
                    if (!instance) continue;

                    for (auto&& [id, member] : bridge.metaType.data())
                    {
                        const char* name = member.name();
                        if (!name || !compJson.contains(name)) continue;
                        deserializeMetaData(member, instance, compJson[name]);
                    }
                }
            }
        );

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

        serialization::SceneSerialization::setPluginSerializationHooks(nullptr, nullptr);
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
