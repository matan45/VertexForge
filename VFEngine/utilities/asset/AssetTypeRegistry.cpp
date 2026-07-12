#include "AssetTypeRegistry.hpp"
#include "../print/Log.hpp"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace asset
{
    namespace
    {
        std::string toLower(const std::string& s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }
    }

    struct AssetTypeRegistry::Impl
    {
        mutable std::shared_mutex mutex;

        // --- Built-in tables (moved verbatim from the old AssetExtensions.hpp;
        // single source of truth now lives here, in AssetDB.dll) ---
        std::unordered_map<std::string, resource::AssetType> builtinExtToType;
        std::unordered_set<std::string> builtinAllExt;   // includes typeless .vfwater/.vfimposter
        std::unordered_set<std::string> builtinJsonExt;

        // --- Plugin-registered types, keyed by handle id ---
        std::unordered_map<uint32_t, AssetTypeRecord> records;     // handleId -> record
        std::unordered_map<std::string, uint32_t> typeIdToHandle;  // typeId -> handleId
        std::unordered_map<std::string, uint32_t> extToHandle;     // ".vfability" -> handleId
        uint32_t nextHandle = 1;

        // --- Caches (built-in ∪ plugin), rebuilt under unique lock on mutation ---
        std::unordered_set<std::string> allExtCache;
        std::unordered_set<std::string> jsonExtCache;

        uint64_t revision = 0;

        void seedBuiltins()
        {
            builtinExtToType = {
                {".vfimage", resource::AssetType::Texture},
                {".vfhdr", resource::AssetType::HDR},
                {".vfmesh", resource::AssetType::Mesh},
                {".vfaudio", resource::AssetType::Audio},
                {".vfanim", resource::AssetType::Animation},
                {".vfmat", resource::AssetType::Material},
                {".vfmatinstance", resource::AssetType::MaterialInstance},
                {".vfanimator", resource::AssetType::Animator},
                {".vfvfx", resource::AssetType::VFX},
                {".vfvfxsequence", resource::AssetType::VFXSequence},
                {".vffont", resource::AssetType::Font},
                {".vfnavmesh", resource::AssetType::Navmesh},
                {".vfnavindex", resource::AssetType::Navmesh},
                {".vfinputmapping", resource::AssetType::InputMapping},
                {".vfterrain", resource::AssetType::Terrain},
                {".vfterrainmat", resource::AssetType::TerrainMaterial},
                {".vfbehaviortree", resource::AssetType::BehaviorTree},
                {".vfphysanim", resource::AssetType::PhysicsShape},
                {".vfscene", resource::AssetType::Scene},
                {".vfsettings", resource::AssetType::Scene},
                {".vftheme", resource::AssetType::Theme},
                {".vfprefab", resource::AssetType::Prefab},
                {".vfrig", resource::AssetType::HumanoidRig},
                {".vfretarget", resource::AssetType::RetargetMap},
                {".vftoonprofile", resource::AssetType::ToonProfile},
                {".mt", resource::AssetType::Script},
            };

            // Every extension the asset database tracks. .vfwater/.vfimposter
            // have no AssetType yet but are still referenced by scenes, so they
            // stay registrable as dependency targets (classify to COUNT).
            builtinAllExt = {
                ".vfimage", ".vfhdr", ".vfmesh", ".vfaudio", ".vfanim",
                ".vfmat", ".vfmatinstance", ".vfanimator", ".vfvfx",
                ".vfvfxsequence",
                ".vffont", ".vfscene", ".vfsettings", ".vfprefab", ".vftheme",
                ".vfterrain", ".vfterrainmat", ".vfwater", ".vfnavmesh",
                ".vfnavindex", ".vfimposter", ".vfinputmapping",
                ".vfbehaviortree", ".vfphysanim", ".vfrig", ".vfretarget",
                ".vftoonprofile", ".mt"
            };

            // JSON-based asset files that can reference other assets — the set
            // the DependencyScanner parses.
            builtinJsonExt = {
                ".vfscene", ".vfsettings", ".vfprefab", ".vfmat",
                ".vfmatinstance", ".vfanimator", ".vfvfx", ".vfvfxsequence",
                ".vfterrainmat",
                ".vftheme", ".vfbehaviortree", ".vfinputmapping",
                ".vfrig", ".vfretarget"
            };
        }

        // Caller holds the unique lock.
        void rebuildExtensionCaches()
        {
            allExtCache = builtinAllExt;
            jsonExtCache = builtinJsonExt;
            for (const auto& [handle, rec] : records)
            {
                for (const auto& ext : rec.extensions)
                {
                    allExtCache.insert(ext);
                    if (rec.jsonContainer) jsonExtCache.insert(ext);
                }
            }
        }
    };

    AssetTypeRegistry::AssetTypeRegistry() : impl(new Impl())
    {
        impl->seedBuiltins();
        std::unique_lock lock(impl->mutex);
        impl->rebuildExtensionCaches();
    }

    AssetTypeRegistry& AssetTypeRegistry::instance()
    {
        static AssetTypeRegistry registry;   // C++11 thread-safe lazy init
        return registry;
    }

    AssetTypeHandle AssetTypeRegistry::registerType(const AssetTypeRecord& record)
    {
        if (record.typeId.empty())
        {
            vfLogWarning("AssetTypeRegistry: rejected registration with empty typeId");
            return {};
        }

        // Normalise extensions (lowercase) up front.
        AssetTypeRecord rec = record;
        rec.type = resource::AssetType::PluginAsset;   // all plugin types funnel here
        rec.builtin = false;
        for (auto& ext : rec.extensions) ext = toLower(ext);

        std::unique_lock lock(impl->mutex);

        if (impl->typeIdToHandle.count(rec.typeId))
        {
            vfLogWarning("AssetTypeRegistry: duplicate typeId '{}' rejected (first registrant wins)",
                         rec.typeId);
            return {};
        }

        // Reject if any extension collides with a built-in or an already
        // registered plugin extension (first registrant wins, deterministic).
        for (const auto& ext : rec.extensions)
        {
            if (ext.empty() || ext[0] != '.')
            {
                vfLogWarning("AssetTypeRegistry: type '{}' has invalid extension '{}' (must start with '.')",
                             rec.typeId, ext);
                return {};
            }
            if (impl->builtinAllExt.count(ext) || impl->extToHandle.count(ext))
            {
                vfLogWarning("AssetTypeRegistry: type '{}' extension '{}' already in use — rejected",
                             rec.typeId, ext);
                return {};
            }
        }

        const uint32_t handle = impl->nextHandle++;
        impl->typeIdToHandle[rec.typeId] = handle;
        for (const auto& ext : rec.extensions) impl->extToHandle[ext] = handle;
        impl->records.emplace(handle, std::move(rec));
        ++impl->revision;
        impl->rebuildExtensionCaches();
        return AssetTypeHandle{handle};
    }

    void AssetTypeRegistry::unregisterType(AssetTypeHandle handle)
    {
        if (!handle) return;
        std::unique_lock lock(impl->mutex);
        auto it = impl->records.find(handle.id);
        if (it == impl->records.end()) return;

        impl->typeIdToHandle.erase(it->second.typeId);
        for (const auto& ext : it->second.extensions) impl->extToHandle.erase(ext);
        impl->records.erase(it);
        ++impl->revision;
        impl->rebuildExtensionCaches();
    }

    void AssetTypeRegistry::unregisterByPlugin(const std::string& pluginName)
    {
        std::unique_lock lock(impl->mutex);
        std::vector<uint32_t> toRemove;
        for (const auto& [handle, rec] : impl->records)
            if (rec.owningPlugin == pluginName) toRemove.push_back(handle);

        if (toRemove.empty()) return;
        for (uint32_t handle : toRemove)
        {
            auto it = impl->records.find(handle);
            if (it == impl->records.end()) continue;
            impl->typeIdToHandle.erase(it->second.typeId);
            for (const auto& ext : it->second.extensions) impl->extToHandle.erase(ext);
            impl->records.erase(it);
        }
        ++impl->revision;
        impl->rebuildExtensionCaches();
    }

    bool AssetTypeRegistry::findByExtension(const std::string& extension, AssetTypeRecord& out) const
    {
        const std::string ext = toLower(extension);
        std::shared_lock lock(impl->mutex);
        auto eit = impl->extToHandle.find(ext);
        if (eit == impl->extToHandle.end()) return false;
        auto rit = impl->records.find(eit->second);
        if (rit == impl->records.end()) return false;
        out = rit->second;
        return true;
    }

    bool AssetTypeRegistry::findByTypeId(const std::string& typeId, AssetTypeRecord& out) const
    {
        std::shared_lock lock(impl->mutex);
        auto tit = impl->typeIdToHandle.find(typeId);
        if (tit == impl->typeIdToHandle.end()) return false;
        auto rit = impl->records.find(tit->second);
        if (rit == impl->records.end()) return false;
        out = rit->second;
        return true;
    }

    uint32_t AssetTypeRegistry::badgeColorForExtension(const std::string& extension, uint32_t fallback) const
    {
        const std::string ext = toLower(extension);
        std::shared_lock lock(impl->mutex);
        auto eit = impl->extToHandle.find(ext);
        if (eit == impl->extToHandle.end()) return fallback;
        auto rit = impl->records.find(eit->second);
        if (rit == impl->records.end()) return fallback;
        return rit->second.badgeColor;
    }

    std::vector<AssetTypeRecord> AssetTypeRegistry::allPluginTypes() const
    {
        std::shared_lock lock(impl->mutex);
        std::vector<AssetTypeRecord> result;
        result.reserve(impl->records.size());
        for (const auto& [handle, rec] : impl->records) result.push_back(rec);
        return result;
    }

    resource::AssetType AssetTypeRegistry::typeForExtension(const std::string& extension) const
    {
        const std::string ext = toLower(extension);
        std::shared_lock lock(impl->mutex);
        auto bit = impl->builtinExtToType.find(ext);
        if (bit != impl->builtinExtToType.end()) return bit->second;
        // Built-in but typeless (.vfwater/.vfimposter) — stays built-in COUNT,
        // never falls through to a plugin type.
        if (impl->builtinAllExt.count(ext)) return resource::AssetType::COUNT;
        if (impl->extToHandle.count(ext)) return resource::AssetType::PluginAsset;
        return resource::AssetType::COUNT;
    }

    bool AssetTypeRegistry::isAssetExtension(const std::string& extension) const
    {
        const std::string ext = toLower(extension);
        std::shared_lock lock(impl->mutex);
        return impl->builtinAllExt.count(ext) > 0 || impl->extToHandle.count(ext) > 0;
    }

    bool AssetTypeRegistry::isJsonContainerExtension(const std::string& extension) const
    {
        const std::string ext = toLower(extension);
        std::shared_lock lock(impl->mutex);
        if (impl->builtinJsonExt.count(ext)) return true;
        auto eit = impl->extToHandle.find(ext);
        if (eit == impl->extToHandle.end()) return false;
        auto rit = impl->records.find(eit->second);
        return rit != impl->records.end() && rit->second.jsonContainer;
    }

    const std::unordered_set<std::string>& AssetTypeRegistry::allAssetExtensions() const
    {
        // The cache is only mutated under the unique lock on the main thread at
        // plugin load/unload; the returned reference is stable until then.
        // Worker-thread scanners use the by-value bool helpers above instead.
        std::shared_lock lock(impl->mutex);
        return impl->allExtCache;
    }

    const std::unordered_set<std::string>& AssetTypeRegistry::jsonContainerExtensions() const
    {
        std::shared_lock lock(impl->mutex);
        return impl->jsonExtCache;
    }

    uint64_t AssetTypeRegistry::revision() const
    {
        std::shared_lock lock(impl->mutex);
        return impl->revision;
    }
}
