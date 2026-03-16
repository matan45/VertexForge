#include "AssetDatabaseServiceImpl.hpp"
#include "../../events/asset/AssetDatabaseEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/project/FileOperationsEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/AssetDatabaseMigrator.hpp"
#include "asset/DependencyScanner.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace services
{
    AssetDatabaseServiceImpl::~AssetDatabaseServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        for (auto& token : subscriptions)
        {
            dispatcher.unsubscribe(token);
        }
    }

    void AssetDatabaseServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Commands
        dispatcher.registerCommandHandler<events::assetdb::RegisterAssetCommand>(
            [this](const events::assetdb::RegisterAssetCommand& cmd)
            {
                auto guid = asset::AssetDatabase::instance().registerAsset(cmd.path, cmd.type, cmd.importSource);

                // Create .vfmeta sidecar
                asset::AssetMetadata metadata;
                metadata.guid = guid;
                metadata.type = cmd.type;
                metadata.importSourcePath = cmd.importSource;
                auto metaPath = asset::AssetMetadataSerializer::getMetaPath(cmd.path);
                asset::AssetMetadataSerializer::save(metadata, metaPath);

                // Publish notification
                events::assetdb::AssetRegisteredNotification notification;
                notification.guid = guid;
                notification.path = cmd.path;
                notification.type = cmd.type;
                events::EventDispatcher::instance().publish(notification);

                return guid;
            });

        dispatcher.registerCommandHandler<events::assetdb::UnregisterAssetCommand>(
            [](const events::assetdb::UnregisterAssetCommand& cmd)
            {
                // Remove .vfmeta sidecar
                auto pathOpt = asset::AssetDatabase::instance().getPath(cmd.guid);
                if (pathOpt)
                {
                    auto metaPath = asset::AssetMetadataSerializer::getMetaPath(*pathOpt);
                    std::error_code ec;
                    fs::remove(metaPath, ec);
                }

                asset::AssetDatabase::instance().unregisterAsset(cmd.guid);
            });

        dispatcher.registerCommandHandler<events::assetdb::RebuildDatabaseCommand>(
            [this](const events::assetdb::RebuildDatabaseCommand&)
            {
                return rebuildDatabase();
            });

        // Queries
        dispatcher.registerQueryHandler<events::assetdb::GetAssetPathQuery>(
            [](const events::assetdb::GetAssetPathQuery& q)
            {
                return asset::AssetDatabase::instance().getPath(q.guid);
            });

        dispatcher.registerQueryHandler<events::assetdb::GetAssetGUIDQuery>(
            [](const events::assetdb::GetAssetGUIDQuery& q)
            {
                return asset::AssetDatabase::instance().getGUID(q.path);
            });

        dispatcher.registerQueryHandler<events::assetdb::GetAssetDependentsQuery>(
            [](const events::assetdb::GetAssetDependentsQuery& q)
            {
                return asset::AssetDatabase::instance().getDependents(q.guid);
            });

        dispatcher.registerQueryHandler<events::assetdb::GetAssetDependenciesQuery>(
            [](const events::assetdb::GetAssetDependenciesQuery& q)
            {
                return asset::AssetDatabase::instance().getDependencies(q.guid);
            });

        dispatcher.registerQueryHandler<events::assetdb::GetAssetCountQuery>(
            [](const events::assetdb::GetAssetCountQuery&)
            {
                return asset::AssetDatabase::instance().getAssetCount();
            });

        // Subscribe to notifications
        subscriptions.push_back(
            dispatcher.subscribe<events::project::ProjectLoadedNotification>(
                [this](const events::project::ProjectLoadedNotification& n)
                {
                    onProjectLoaded(n.filePath);
                }));

        subscriptions.push_back(
            dispatcher.subscribe<events::project::ProjectClosedNotification>(
                [this](const events::project::ProjectClosedNotification&)
                {
                    onProjectClosed();
                }));

        subscriptions.push_back(
            dispatcher.subscribe<events::fileops::FileMovedNotification>(
                [this](const events::fileops::FileMovedNotification& n)
                {
                    onFileMoved(n.oldPath, n.newPath);
                }));

        subscriptions.push_back(
            dispatcher.subscribe<events::fileops::FileDeletedNotification>(
                [this](const events::fileops::FileDeletedNotification& n)
                {
                    onFileDeleted(n.path);
                }));

        subscriptions.push_back(
            dispatcher.subscribe<events::resource::ImportCompletedNotification>(
                [this](const events::resource::ImportCompletedNotification& n)
                {
                    for (const auto& result : n.results)
                    {
                        if (result.success && !result.outputPath.empty())
                        {
                            onImportCompleted(result.outputPath, result.assetType, result.sourcePath);
                        }
                    }
                }));

        subscriptions.push_back(
            dispatcher.subscribe<events::resource::AssetSavedNotification>(
                [this](const events::resource::AssetSavedNotification& n)
                {
                    onAssetSaved(n.filePath);
                }));
    }

    void AssetDatabaseServiceImpl::onProjectLoaded(const std::string& projectFilePath)
    {
        std::string projectRoot = fs::path(projectFilePath).parent_path().string();

        auto& db = asset::AssetDatabase::instance();

        // Always rebuild from .vfmeta files for accuracy
        db.rebuildFromMetaFiles(projectRoot);

        if (db.getAssetCount() == 0)
        {
            // First-time migration: generate .vfmeta for all existing assets
            auto migrationResult = asset::AssetDatabaseMigrator::migrateProject(projectRoot);
            if (migrationResult.assetsRegistered > 0)
            {
                vfLogInfo("Migrated project: registered {} assets, created {} meta files",
                          migrationResult.assetsRegistered, migrationResult.metaFilesCreated);
            }
        }

        // Scan dependencies and save index
        asset::DependencyScanner::scanAll(projectRoot);
        db.saveIndex(projectRoot);

        events::assetdb::AssetDatabaseRebuiltNotification notification;
        notification.assetCount = static_cast<uint32_t>(db.getAssetCount());
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("Asset database loaded with {} assets", db.getAssetCount());
    }

    void AssetDatabaseServiceImpl::onProjectClosed()
    {
        std::string projRoot = getProjectRoot();
        if (!projRoot.empty())
        {
            asset::AssetDatabase::instance().saveIndex(projRoot);
        }
        asset::AssetDatabase::instance().clear();
    }

    void AssetDatabaseServiceImpl::onFileMoved(const std::string& oldPath, const std::string& newPath)
    {
        auto& db = asset::AssetDatabase::instance();

        // Update database entry
        bool updated = db.updatePathByOldPath(oldPath, newPath);

        if (updated)
        {
            // .vfmeta sidecar is moved by FileOperationsServiceImpl before this notification
            auto guidOpt = db.getGUID(newPath);
            if (guidOpt)
            {
                events::assetdb::AssetPathChangedNotification notification;
                notification.guid = *guidOpt;
                notification.oldPath = oldPath;
                notification.newPath = newPath;
                events::EventDispatcher::instance().publish(notification);
            }
        }
    }

    void AssetDatabaseServiceImpl::onFileDeleted(const std::string& path)
    {
        auto& db = asset::AssetDatabase::instance();
        auto guidOpt = db.getGUID(path);
        if (guidOpt)
        {
            db.unregisterAsset(*guidOpt);
        }

        // The .vfmeta is handled alongside the asset by FileOperationsServiceImpl
    }

    void AssetDatabaseServiceImpl::onImportCompleted(const std::string& outputPath,
                                                      resource::AssetType type,
                                                      const std::string& sourcePath)
    {
        auto& db = asset::AssetDatabase::instance();

        // Check if already registered
        if (db.getGUID(outputPath).has_value())
        {
            return;
        }

        auto guid = db.registerAsset(outputPath, type, sourcePath);

        // Create .vfmeta sidecar
        asset::AssetMetadata metadata;
        metadata.guid = guid;
        metadata.type = type;
        metadata.importSourcePath = sourcePath;
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(outputPath);
        asset::AssetMetadataSerializer::save(metadata, metaPath);

        events::assetdb::AssetRegisteredNotification notification;
        notification.guid = guid;
        notification.path = outputPath;
        notification.type = type;
        events::EventDispatcher::instance().publish(notification);
    }

    void AssetDatabaseServiceImpl::onAssetSaved(const std::string& filePath)
    {
        auto& db = asset::AssetDatabase::instance();
        auto guidOpt = db.getGUID(filePath);
        if (!guidOpt) return;

        // Re-scan dependencies for this asset
        std::string projRoot = getProjectRoot();
        if (!projRoot.empty())
        {
            asset::DependencyScanner::scanAsset(*guidOpt, filePath, projRoot);
        }
    }

    bool AssetDatabaseServiceImpl::rebuildDatabase()
    {
        std::string projRoot = getProjectRoot();
        if (projRoot.empty()) return false;

        auto& db = asset::AssetDatabase::instance();

        // Rebuild from .vfmeta files
        if (!db.rebuildFromMetaFiles(projRoot)) return false;

        // Re-scan all dependencies
        asset::DependencyScanner::scanAll(projRoot);

        // Save the index
        db.saveIndex(projRoot);

        events::assetdb::AssetDatabaseRebuiltNotification notification;
        notification.assetCount = static_cast<uint32_t>(db.getAssetCount());
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    std::string AssetDatabaseServiceImpl::getProjectRoot() const
    {
        auto pathOpt = events::EventDispatcher::instance().query(events::project::GetProjectPathQuery{});
        if (pathOpt.has_value() && !pathOpt.value().empty())
        {
            return fs::path(pathOpt.value()).parent_path().string();
        }
        return {};
    }
}
