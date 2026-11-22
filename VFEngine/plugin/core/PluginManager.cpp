#include "print/Log.hpp"
#include "PluginManager.hpp"
#include "PluginContextImpl.hpp"
#include "../api/PluginVersion.hpp"
#include "serialization/SceneSerialization.hpp"
#include "serialization/MetaJsonSerializer.hpp"
#include "serialization/AssetRefSerializationHelper.hpp"
#include "asset/AssetRef.hpp"
#include "scene/EntityRegistry.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "Pipeline.hpp"
#include "registry/AssetImporter.hpp"
#include "export/PluginDescriptorEdit.hpp"
#include "launch/Utf8Path.hpp"
#include "resource/AtomicFileReplace.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <unordered_set>
#include <cassert>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

    std::filesystem::path PluginManager::resolvePluginsDirectory()
    {
        auto containsDescriptors = [](const std::filesystem::path& dir)
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) return false;
            for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            {
                if (it->is_regular_file(ec) && it->path().extension() == ".vfplugin") return true;
            }
            return false;
        };

        // Anchor on the executable, not the CWD — IDE launchers (Rider/VS) set
        // the working directory to the project folder, which broke CWD-relative
        // lookup ("VFEngine/editor/plugins: no plugins found").
        std::filesystem::path exeDir;
#ifdef _WIN32
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        {
            exeDir = std::filesystem::path(exePath).parent_path();
        }
#endif
        if (exeDir.empty()) exeDir = std::filesystem::current_path();

        // Deployed layout first (plugins/ beside the executable), then walk up
        // toward the dev-tree root (bin/Editor/<Config>/x64 -> repo root).
        std::filesystem::path dir = exeDir;
        for (int depth = 0; depth < 6 && !dir.empty(); ++depth)
        {
            auto candidate = dir / "plugins";
            if (containsDescriptors(candidate)) return candidate;
            auto parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
        return exeDir / "plugins";
    }

    namespace
    {
        namespace fs = std::filesystem;
        using launch::pathToUtf8;

        enum class ReadOutcome { Ok, Missing, Failed };

        // The whole file, as bytes.
        ReadOutcome readWholeFile(const fs::path& path, std::string& bytes)
        {
            std::error_code ec;
            const bool exists = fs::exists(path, ec);
            if (ec)
                return ReadOutcome::Failed;
            if (!exists)
                return ReadOutcome::Missing;
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open())
                return ReadOutcome::Failed;
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return in.bad() ? ReadOutcome::Failed : ReadOutcome::Ok;
        }

        // Replaces `file` with `bytes` through "<file>.tmp" and resource::replaceFileAtomically. The temp
        // file never outlives a failure. Returns an error message, empty on success.
        std::string replaceFileContents(const fs::path& file, const std::string& bytes)
        {
            std::error_code ec;
            const fs::file_status status = fs::status(file, ec);
            // MSVC maps FILE_ATTRIBUTE_READONLY to "no write bits"; the replace would only fail later.
            if (!ec && fs::exists(status) && (status.permissions() & fs::perms::owner_write) == fs::perms::none)
                return pathToUtf8(file) + " is read-only";

            fs::path tempPath = file;
            tempPath += ".tmp";
            const auto removeTemp = [&tempPath]
            {
                std::error_code removeError;
                fs::remove(tempPath, removeError);
            };

            // A reader holding the file open blocks the replace for a moment: retry, writing the temp
            // file afresh each time.
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (attempt > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                bool written = false;
                {
                    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                    if (out.is_open())
                    {
                        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                        out.close();
                        written = !out.fail();
                    }
                }
                if (!written)
                {
                    removeTemp();
                    return "cannot write " + pathToUtf8(tempPath);
                }
                if (::resource::replaceFileAtomically(tempPath, file))
                    return {};
                removeTemp();
            }
            return "cannot replace " + pathToUtf8(file) + " (is it open in another program?)";
        }

        const char* rootLabel(PluginRootKind root)
        {
            return root == PluginRootKind::Engine ? "shipped" : "user";
        }
    }

    void PluginManager::loadAll(const std::filesystem::path& pluginDirectory)
    {
        PluginLoadConfig config;
        config.engineRoot = pluginDirectory;
        loadAll(config);
    }

    void PluginManager::loadAll(const PluginLoadConfig& config)
    {
        loadConfig = config;

        std::error_code ec;
        const bool engineRootExists = !config.engineRoot.empty() && fs::exists(config.engineRoot, ec);
        const bool userRootExists = !config.userRoot.empty() && fs::is_directory(config.userRoot, ec);
        if (!engineRootExists && !userRootExists) {
            vfLogInfo("Plugin directory '{}' does not exist, skipping plugin loading", pathToUtf8(config.engineRoot));
            return;
        }

        if (engineRootExists) {
            vfLogInfo("Scanning for plugins in '{}'...", pathToUtf8(config.engineRoot));
            pluginsDirectory = config.engineRoot;
        } else {
            vfLogInfo("Plugin directory '{}' does not exist", pathToUtf8(config.engineRoot));
        }
        if (userRootExists)
            vfLogInfo("Scanning for your plugins in '{}'...", pathToUtf8(config.userRoot));

        const PluginOverrides overrides = readOverrides();
        if (overrides.status == PluginOverridesStatus::Invalid)
            vfLogWarning("Ignoring {}: {}; the shipped plugins follow their own descriptors",
                         pathToUtf8(config.overridesFile), overrides.problem);
        else if (overrides.status == PluginOverridesStatus::Newer)
            vfLogWarning("Ignoring {}: {}; the shipped plugins follow their own descriptors, and this editor "
                         "will not change the file", pathToUtf8(config.overridesFile), overrides.problem);

        // Phase 1: Collect descriptors from .vfplugin files
        std::vector<fs::path> unparsed;
        std::vector<DiscoveredPlugin> discovered = discover(overrides, &unparsed);
        for (const auto& path : unparsed)
            vfLogError("Failed to parse plugin descriptor: {}", pathToUtf8(path));

        std::vector<PluginDescriptor> descriptors;
        std::unordered_set<std::wstring> dllsWithDescriptor;
        for (auto& found : discovered)
        {
            const PluginDescriptor& desc = found.descriptor;
            if (found.shadowed)
            {
                vfLogWarning("Plugin '{}' ({}) is shadowed by the {} plugin with that name ({}), skipping",
                             desc.name, pathToUtf8(desc.descriptorPath), rootLabel(found.shadowedByRoot),
                             pathToUtf8(found.shadowedBy));
                continue;
            }

            if (!found.effectiveEnabled)
            {
                if (found.hasOverride)
                    vfLogInfo("Plugin '{}' is disabled in {}, skipping", desc.name, pathToUtf8(config.overridesFile));
                else
                    vfLogInfo("Plugin '{}' is disabled, skipping", desc.name);
                continue;
            }

            if (desc.apiVersion != VF_PLUGIN_API_VERSION)
            {
                vfLogError("Plugin '{}' API version mismatch: descriptor={}, engine={}",
                           desc.name, desc.apiVersion, VF_PLUGIN_API_VERSION);
                continue;
            }

            auto libPath = desc.getLibraryPath();
            std::error_code libEc;
            if (!fs::exists(libPath, libEc))
            {
                vfLogError("Plugin '{}' library not found: {}", desc.name, pathToUtf8(libPath));
                continue;
            }

            std::error_code canonicalEc;
            const fs::path canonical = fs::canonical(libPath, canonicalEc);
            dllsWithDescriptor.insert((canonicalEc ? libPath : canonical).wstring());
            descriptors.push_back(std::move(found.descriptor));
        }

        // Phase 2+3: Topological sort with dependency validation and cycle detection
        auto sortedDescriptors = resolveLoadOrder(descriptors);

        // Phase 4: Load plugins from descriptors
        for (const auto& desc : sortedDescriptors)
        {
            loadPlugin(desc);
        }

        // Phase 5: Backward compatibility — load orphan DLLs without descriptors (engine root only)
        if (engineRootExists) {
            std::error_code dirEc;
            for (auto it = fs::directory_iterator(config.engineRoot, fs::directory_options::skip_permission_denied, dirEc);
                 !dirEc && it != fs::directory_iterator(); it.increment(dirEc)) {
                std::error_code fileEc;
                if (!it->is_regular_file(fileEc))
                    continue;

                const auto& path = it->path();
                if (path.extension() != ".dll")
                    continue;
                std::error_code canonicalEc;
                const fs::path canonical = fs::canonical(path, canonicalEc);
                if (dllsWithDescriptor.contains((canonicalEc ? path : canonical).wstring()))
                    continue;

                vfLogWarning("Plugin '{}' has no .vfplugin descriptor, loading with legacy method",
                             pathToUtf8(path.filename()));
                loadPluginFromDll(path);
            }
        }

        vfLogInfo("Loaded {} plugin(s)", plugins.size());
    }

    std::vector<PluginManager::DescriptorFile> PluginManager::listDescriptorFiles() const
    {
        std::vector<DescriptorFile> files;
        const auto isDescriptor = [](const fs::directory_entry& entry)
        {
            std::error_code ec;
            return entry.is_regular_file(ec) && entry.path().extension() == ".vfplugin";
        };

        // The engine root: recursively, sorted by path (directory order is not a contract).
        std::error_code ec;
        if (!loadConfig.engineRoot.empty() && fs::is_directory(loadConfig.engineRoot, ec))
        {
            std::vector<fs::path> found;
            for (auto it = fs::recursive_directory_iterator(loadConfig.engineRoot,
                                                            fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (isDescriptor(*it))
                    found.push_back(it->path());
            }
            std::sort(found.begin(), found.end());
            for (auto& path : found)
                files.push_back({std::move(path), PluginRootKind::Engine});
        }

        // The user root: <root>/<Folder>/*.vfplugin only. It is the user's Documents folder, where a deep
        // scan could wander into anything.
        ec.clear();
        if (!loadConfig.userRoot.empty() && fs::is_directory(loadConfig.userRoot, ec))
        {
            std::vector<fs::path> found;
            for (auto dir = fs::directory_iterator(loadConfig.userRoot, fs::directory_options::skip_permission_denied, ec);
                 !ec && dir != fs::directory_iterator(); dir.increment(ec))
            {
                std::error_code dirEc;
                if (!dir->is_directory(dirEc))
                    continue;
                for (auto file = fs::directory_iterator(dir->path(), fs::directory_options::skip_permission_denied, dirEc);
                     !dirEc && file != fs::directory_iterator(); file.increment(dirEc))
                {
                    if (isDescriptor(*file))
                        found.push_back(file->path());
                }
            }
            std::sort(found.begin(), found.end());
            for (auto& path : found)
                files.push_back({std::move(path), PluginRootKind::User});
        }
        return files;
    }

    PluginOverrides PluginManager::readOverrides() const
    {
        if (loadConfig.overridesFile.empty())
            return {};

        std::string text;
        switch (readWholeFile(loadConfig.overridesFile, text))
        {
        case ReadOutcome::Missing:
            return {};
        case ReadOutcome::Failed:
        {
            PluginOverrides unreadable;
            unreadable.status = PluginOverridesStatus::Invalid;
            unreadable.problem = "it could not be read";
            return unreadable;
        }
        case ReadOutcome::Ok:
            break;
        }
        return parsePluginOverrides(std::string_view(text));
    }

    std::vector<DiscoveredPlugin> PluginManager::discover(const PluginOverrides& overrides,
                                                          std::vector<fs::path>* unparsed) const
    {
        std::vector<DiscoveredPlugin> found;
        for (auto& file : listDescriptorFiles())
        {
            std::optional<PluginDescriptor> desc;
            try
            {
                desc = PluginDescriptor::loadFromFile(file.path);
            }
            catch (const std::exception&)
            {
                desc.reset();
            }
            if (!desc.has_value())
            {
                if (unparsed)
                    unparsed->push_back(std::move(file.path));
                continue;
            }

            DiscoveredPlugin plugin;
            plugin.descriptor = std::move(*desc);
            plugin.root = file.root;
            found.push_back(std::move(plugin));
        }

        std::vector<PluginRootCandidate> candidates;
        candidates.reserve(found.size());
        for (const auto& plugin : found)
            candidates.push_back({plugin.descriptor.name, plugin.root});
        const auto shadowedBy = findShadowed(candidates);

        for (std::size_t i = 0; i < found.size(); ++i)
        {
            DiscoveredPlugin& plugin = found[i];
            if (shadowedBy[i])
            {
                plugin.shadowed = true;
                plugin.shadowedBy = found[*shadowedBy[i]].descriptor.descriptorPath;
                plugin.shadowedByRoot = found[*shadowedBy[i]].root;
            }
            plugin.hasOverride = plugin.root == PluginRootKind::Engine &&
                                 overrides.status == PluginOverridesStatus::Ok &&
                                 overrides.enabled.contains(plugin.descriptor.name);
            plugin.effectiveEnabled = effectiveEnabled(plugin.root, plugin.descriptor.name,
                                                       plugin.descriptor.enabled, &overrides);
        }
        return found;
    }

    std::vector<DiscoveredPlugin> PluginManager::discoverPlugins() const
    {
        return discover(readOverrides(), nullptr);
    }

    std::filesystem::path PluginManager::getPluginDataRoot() const
    {
        if (!loadConfig.dataRoot.empty())
            return loadConfig.dataRoot;
        if (!pluginsDirectory.empty())
            return pluginsDirectory / "data";
        return {};
    }

    std::string PluginManager::setPluginEnabled(const DiscoveredPlugin& plugin, bool enabled)
    {
        const std::string& name = plugin.descriptor.name;
        if (plugin.shadowed)
            return "'" + name + "' is shadowed by the plugin at " + pathToUtf8(plugin.shadowedBy) +
                   " and never loads";

        try
        {
            if (plugin.root == PluginRootKind::Engine && usesEnabledOverrides())
            {
                const fs::path& file = loadConfig.overridesFile;
                PluginOverrides overrides = readOverrides();
                if (!overrides.writable())
                    return pathToUtf8(file) + ": " + overrides.problem + ". Fix or delete it to change shipped plugins.";

                overrides.enabled[name] = enabled;
                std::error_code ec;
                fs::create_directories(file.parent_path(), ec);
                if (ec)
                    return "cannot create " + pathToUtf8(file.parent_path()) + ": " + ec.message();
                return replaceFileContents(file, serializePluginOverrides(overrides.enabled));
            }

            const fs::path& file = plugin.descriptor.descriptorPath;
            std::string original;
            if (readWholeFile(file, original) != ReadOutcome::Ok)
                return "cannot read " + pathToUtf8(file);
            const std::optional<std::string> edited = gameExport::descriptorTextWithEnabled(original, enabled);
            if (!edited)
                return pathToUtf8(file) + " is not a JSON object";
            if (*edited == original)
                return {};
            return replaceFileContents(file, *edited);
        }
        catch (const std::exception& e)
        {
            return std::string("cannot change '") + name + "': " + e.what();
        }
    }

    std::string PluginManager::resetPluginEnabled(const std::string& name)
    {
        if (!usesEnabledOverrides())
            return {};

        try
        {
            const fs::path& file = loadConfig.overridesFile;
            PluginOverrides overrides = readOverrides();
            if (overrides.status == PluginOverridesStatus::Absent)
                return {};
            if (!overrides.writable())
                return pathToUtf8(file) + ": " + overrides.problem + ". Fix or delete it to change shipped plugins.";
            if (overrides.enabled.erase(name) == 0)
                return {};
            return replaceFileContents(file, serializePluginOverrides(overrides.enabled));
        }
        catch (const std::exception& e)
        {
            return std::string("cannot reset '") + name + "': " + e.what();
        }
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
                        const char* name = member.name().data();
                        if (!name) continue;
                        // AssetRef field — emit GUID hex + <name>Path sibling inside the
                        // component object (engine owns AssetRef serialization).
                        if (member.type().info() == entt::type_id<asset::AssetRef>())
                        {
                            serialization::writeAssetRef(compJson, name, val.cast<asset::AssetRef>());
                            continue;
                        }
                        auto serialized = serialization::meta::serializeMetaAny(val, member.type());
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

                    // Collect reflected field names for the unknown-key diagnostic below.
                    std::unordered_set<std::string> reflectedNames;
                    for (auto&& [id, member] : bridge.metaType.data())
                    {
                        const char* name = member.name().data();
                        if (!name) continue;
                        reflectedNames.insert(name);
                        // AssetRef field — read GUID hex + <name>Path sibling from the
                        // parent component object (engine owns AssetRef deserialization).
                        if (member.type().info() == entt::type_id<asset::AssetRef>())
                        {
                            member.set(instance, serialization::readAssetRef(compJson, name, ""));
                            continue;
                        }
                        if (!compJson.contains(name)) continue;
                        serialization::meta::deserializeMetaData(member, instance, compJson[name]);
                    }

                    // Diagnostics: warn on JSON sub-keys that match no reflected field
                    // (ignore the AssetRef <name>Path siblings, which are expected).
                    for (auto it = compJson.begin(); it != compJson.end(); ++it)
                    {
                        const std::string& subKey = it.key();
                        if (reflectedNames.count(subKey)) continue;
                        if (subKey.size() > 4 && subKey.compare(subKey.size() - 4, 4, "Path") == 0
                            && reflectedNames.count(subKey.substr(0, subKey.size() - 4)))
                            continue;
                        vfLogWarning("Plugin component '{}': JSON key '{}' matches no reflected field",
                                     bridge.name, subKey);
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

        // VK-1290: Invoke script binding registrar AFTER all plugins have initialized
        // (plugins register component bridges during onInitialize)
        if (auto registrar = PluginContextImpl::getScriptBindingRegistrar())
        {
            registrar(PluginContextImpl::getAllBridges());
        }

        // VK-1365: apply per-scene plugin overrides on every scene load/clear.
        subscribeSceneEvents();
    }

    void PluginManager::subscribeSceneEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                auto overrides = events::EventDispatcher::instance()
                                     .query(events::scene::GetScenePluginSettingsQuery{});
                applySceneActiveStates(overrides);
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                resetActiveStatesToGlobal();
            });
    }

    void PluginManager::unsubscribeSceneEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneLoadedToken.isValid())
        {
            dispatcher.unsubscribe(sceneLoadedToken);
            sceneLoadedToken = {};
        }
        if (sceneClearedToken.isValid())
        {
            dispatcher.unsubscribe(sceneClearedToken);
            sceneClearedToken = {};
        }
    }

    bool PluginManager::setPluginActive(const std::string& name, bool active)
    {
        for (auto& plugin : plugins)
        {
            // The descriptor name is the key used by the UI and scene overrides;
            // legacy orphan DLLs have no descriptor, so fall back to info.name.
            if (plugin.descriptor.name != name && plugin.info.name != name)
                continue;

            if (!plugin.initialized || !plugin.instance || !plugin.context)
                return false;
            if (plugin.active == active)
                return false;

            if (active)
            {
                // Restore engine channels first so the plugin can re-enqueue/bind
                // into live channels from onActivate.
                plugin.context->setActive(true);
                plugin.active = true;
                try {
                    plugin.instance->onActivate();
                }
                catch (const std::exception& e) {
                    vfLogError("Plugin '{}' threw exception during onActivate: {}", name, e.what());
                }
                vfLogInfo("Plugin '{}' activated", name);
            }
            else
            {
                // Let the plugin quiesce its own state (audio/VFX/physics) while
                // its channels are still live, then suppress.
                try {
                    plugin.instance->onDeactivate();
                }
                catch (const std::exception& e) {
                    vfLogError("Plugin '{}' threw exception during onDeactivate: {}", name, e.what());
                }
                plugin.context->setActive(false);
                plugin.active = false;
                vfLogInfo("Plugin '{}' deactivated", name);
            }
            return true;
        }
        return false;
    }

    bool PluginManager::isPluginActive(const std::string& name) const
    {
        for (const auto& plugin : plugins)
        {
            if (plugin.descriptor.name == name || plugin.info.name == name)
                return plugin.active;
        }
        return false;
    }

    void PluginManager::applySceneActiveStates(const std::map<std::string, bool>& overrides)
    {
        for (auto& plugin : plugins)
        {
            // Overrides are keyed by descriptor name (what the UI shows and the
            // scene stores); legacy orphan DLLs fall back to info.name.
            const std::string& key = !plugin.descriptor.name.empty()
                                         ? plugin.descriptor.name
                                         : plugin.info.name;
            auto it = overrides.find(key);
            // Loaded plugins are globally enabled by definition, so inherit == active.
            bool effective = (it != overrides.end()) ? it->second : true;
            setPluginActive(key, effective);
        }
    }

    void PluginManager::resetActiveStatesToGlobal()
    {
        for (auto& plugin : plugins)
        {
            const std::string& key = !plugin.descriptor.name.empty()
                                         ? plugin.descriptor.name
                                         : plugin.info.name;
            setPluginActive(key, true);
        }
    }

    void PluginManager::updateAll(float deltaTime)
    {
        for (auto& plugin : plugins) {
            if (!plugin.initialized || !plugin.instance || !plugin.active) {
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
        unsubscribeSceneEvents();

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

        // Drop the engine-side script-binding cache before plugins.clear() unloads the DLLs.
        // PluginComponentAPI::storedBridges holds a copy of the MetaComponentBridges whose
        // std::functions were instantiated in the plugin DLLs; destroying them after the DLLs
        // are freed calls std::function managers in unmapped code (shutdown ACCESS_VIOLATION).
        PluginContextImpl::clearScriptBindings();

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

    std::vector<std::pair<std::string, std::unique_ptr<import::AssetImporter>>> PluginManager::takeAllAssetImporters()
    {
        std::vector<std::pair<std::string, std::unique_ptr<import::AssetImporter>>> allImporters;
        for (auto& plugin : plugins) {
            if (plugin.initialized && plugin.context) {
                for (auto& importer : plugin.context->takeAssetImporters()) {
                    allImporters.emplace_back(plugin.context->getPluginName(), std::move(importer));
                }
            }
        }
        return allImporters;
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
