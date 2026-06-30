#pragma once
#include "../resource/AssetTypes.hpp"
#include "AssetTypeRegistry.hpp"
#include <string>
#include <unordered_set>

namespace asset
{
    // Single source of truth for asset file extensions. The built-in tables
    // (and any plugin-registered types, VK-1449) now live in
    // asset::AssetTypeRegistry, which is compiled into the AssetDB DLL so a
    // mutable registry resolves to one instance across the process. These
    // functions are thin forwarders preserved for source compatibility — the
    // built-in classification they return is identical to the old inline
    // tables (guarded by a golden-parity unit test).
    //
    // NOTE: calling these forces a link against AssetDB. Engine consumers
    // (DependencyScanner, AssetDatabaseMigrator, Tests, …) already link it.
    // Plugins must not call these (they use the PluginContext asset-type API);
    // merely #including this header is fine (inline functions are only emitted
    // when ODR-used).
    namespace extensions
    {
        inline resource::AssetType typeForExtension(const std::string& extension)
        {
            return AssetTypeRegistry::instance().typeForExtension(extension);
        }

        inline const std::unordered_set<std::string>& allAssetExtensions()
        {
            return AssetTypeRegistry::instance().allAssetExtensions();
        }

        inline const std::unordered_set<std::string>& jsonContainerExtensions()
        {
            return AssetTypeRegistry::instance().jsonContainerExtensions();
        }

        inline bool isAssetExtension(const std::string& extension)
        {
            return AssetTypeRegistry::instance().isAssetExtension(extension);
        }

        inline bool isJsonContainerExtension(const std::string& extension)
        {
            return AssetTypeRegistry::instance().isJsonContainerExtension(extension);
        }
    }
}
