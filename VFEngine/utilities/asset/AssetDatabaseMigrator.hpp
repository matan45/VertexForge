#pragma once
#include "../resource/AssetTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace asset
{
    class AssetDatabaseMigrator
    {
    public:
        struct MigrationResult
        {
            uint32_t assetsRegistered = 0;
            uint32_t metaFilesCreated = 0;
            std::vector<std::string> errors;
        };

        static MigrationResult migrateProject(const std::string& projectRoot);

    private:
        static resource::AssetType detectAssetType(const std::string& extension);
    };
}
