#pragma once
#include "../../interfaces/asset/IAssetDatabaseService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/asset/AssetDatabaseEvents.hpp"
#include <resource/AssetTypes.hpp>
#include <vector>

namespace services
{
    class AssetDatabaseServiceImpl : public IAssetDatabaseService
    {
    public:
        AssetDatabaseServiceImpl() = default;
        ~AssetDatabaseServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void onProjectLoaded(const std::string& projectFilePath);
        void onProjectClosed();
        void onFileMoved(const std::string& oldPath, const std::string& newPath);
        void onFileDeleted(const std::string& path);
        void onImportCompleted(const std::string& outputPath, resource::AssetType type,
                               const std::string& sourcePath);
        void onAssetSaved(const std::string& filePath);
        bool rebuildDatabase();
        ::events::assetdb::RegenerateMetadataResult regenerateMissingMetadata();
        std::string getProjectRoot() const;

        // Shared post-rebuild tail: rescan dependencies, persist the index, and broadcast the
        // rebuilt notification. Returns whether saveIndex succeeded.
        bool finalizeDatabaseRebuild(const std::string& projRoot);

        std::vector<::events::SubscriptionToken> subscriptions;
    };
}
