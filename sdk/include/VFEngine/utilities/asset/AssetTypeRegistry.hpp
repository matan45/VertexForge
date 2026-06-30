#pragma once
#include "AssetDBExport.hpp"
#include "../resource/AssetTypes.hpp"
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace asset
{
    // A registered asset type. Built-in engine types are seeded once at
    // startup; plugins append their own through the plugin SDK (VK-1449).
    //
    // Records are VALUE-OWNED (deep std::strings/vectors) so a record survives
    // the plugin DLL that registered it being unloaded and freeing its memory
    // (the MetaComponentBridge.name / FieldAttributes lesson).
    struct AssetTypeRecord
    {
        std::string typeId;                  // stable unique id, e.g. "gas.ability"
        resource::AssetType type =           // built-in enum value, or PluginAsset
            resource::AssetType::PluginAsset;
        std::string displayName;
        std::string category;                // create-menu grouping
        std::vector<std::string> extensions; // lowercase, leading dot (".vfability")
        bool jsonContainer = false;          // participates in dependency scanning
        bool createMenuEntry = false;
        std::string icon;
        uint32_t badgeColor = 0xFFFFFFFFu;
        std::string defaultTemplate;         // JSON written on Create (plugin types)
        std::vector<std::string> alwaysIncludeGlobs; // export always-include patterns
        std::string owningPlugin;            // empty for built-ins; set for plugin types
        bool builtin = false;                // true for engine-seeded records
    };

    // Opaque handle returned by registerType. id == 0 means "invalid / rejected".
    struct AssetTypeHandle
    {
        uint32_t id = 0;
        explicit operator bool() const { return id != 0; }
        bool operator==(const AssetTypeHandle& o) const { return id == o.id; }
    };

    // Single source of truth for asset-type classification, shared across the
    // whole process. Lives inside the AssetDB SharedLib (like AssetDatabase) so
    // that types a plugin registers in the editor process are visible to the
    // GameExport/Serialization/Import DLLs — a StaticLib copy per module would
    // give each its own empty registry.
    //
    // The built-in extension tables (previously inline in AssetExtensions.hpp)
    // now live in AssetTypeRegistry.cpp and are seeded lazily on first use, so
    // correctness never depends on static-init order. Built-in classification
    // is byte-identical to the old hardcoded tables (guarded by a golden-parity
    // unit test); plugin types are consulted only for extensions the built-ins
    // do not own.
    //
    // PIMPL: all STL state (tables, records, mutex, caches) lives in the .cpp
    // (compiled into AssetDB.dll), so no dll-interface (C4251) members leak and
    // clients never need the member layout — they only call exported methods.
    class VF_ASSETDB_API AssetTypeRegistry
    {
    public:
        static AssetTypeRegistry& instance();

        AssetTypeRegistry(const AssetTypeRegistry&) = delete;
        AssetTypeRegistry& operator=(const AssetTypeRegistry&) = delete;

        // Register a plugin asset type. Returns an invalid handle (id == 0) and
        // logs if the typeId already exists or any extension collides with a
        // built-in or an already-registered extension (first registrant wins).
        AssetTypeHandle registerType(const AssetTypeRecord& record);
        void unregisterType(AssetTypeHandle handle);
        void unregisterByPlugin(const std::string& pluginName);

        // Lookups (copy out under shared lock — safe from any thread). Return
        // false when not found.
        bool findByExtension(const std::string& extension, AssetTypeRecord& out) const;
        bool findByTypeId(const std::string& typeId, AssetTypeRecord& out) const;

        // All currently-registered PLUGIN types (built-ins are excluded; they
        // have their own engine UI paths). Used by the content-browser create
        // menu and AssetRef pickers.
        std::vector<AssetTypeRecord> allPluginTypes() const;

        // --- Extension classification (the AssetExtensions.hpp forwarders) ---
        // Input is lowercased internally. Behaviour for built-in extensions is
        // identical to the pre-refactor inline tables.
        resource::AssetType typeForExtension(const std::string& extension) const;
        bool isAssetExtension(const std::string& extension) const;
        bool isJsonContainerExtension(const std::string& extension) const;

        // Built-in ∪ plugin extension sets. The returned reference is stable
        // until the next registry mutation (registerType/unregister), which
        // only happens on the main thread at plugin load/unload — never during
        // a dependency scan. Mirrors the old static-set contract.
        const std::unordered_set<std::string>& allAssetExtensions() const;
        const std::unordered_set<std::string>& jsonContainerExtensions() const;

        // Bumps on every mutation; drives editor cache invalidation.
        uint64_t revision() const;

    private:
        AssetTypeRegistry();

        struct Impl;
        Impl* impl;   // owned; lives for the process lifetime (singleton)
    };
}
