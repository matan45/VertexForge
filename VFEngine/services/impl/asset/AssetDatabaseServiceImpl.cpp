#include "AssetDatabaseServiceImpl.hpp"
#include "../../events/asset/AssetDatabaseEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/project/FileOperationsEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetTypeRegistry.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/AssetDatabaseMigrator.hpp"
#include "asset/DependencyScanner.hpp"
#include "print/Log.hpp"
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace services
{
    namespace
    {
        std::string pluginTypeIdForAsset(const std::string& filePath, resource::AssetType type)
        {
            if (type != resource::AssetType::PluginAsset)
                return {};

            asset::AssetTypeRecord rec;
            const std::string extension = fs::path(filePath).extension().string();
            if (asset::AssetTypeRegistry::instance().findByExtension(extension, rec))
                return rec.typeId;

            return {};
        }

        void stampImportTimestamp(asset::AssetMetadata& metadata)
        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &time);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            metadata.importTimestamp = oss.str();
        }
    }

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
                stampImportTimestamp(metadata);
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

        dispatcher.registerCommandHandler<events::assetdb::RegenerateMissingMetadataCommand>(
            [this](const events::assetdb::RegenerateMissingMetadataCommand&)
            {
                return regenerateMissingMetadata();
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

        dispatcher.registerQueryHandler<events::assetdb::GetAssetTypeQuery>(
            [](const events::assetdb::GetAssetTypeQuery& q) -> std::optional<resource::AssetType>
            {
                auto& db = asset::AssetDatabase::instance();
                auto guid = db.getGUID(db.resolveAssetPath(q.path));
                if (!guid) return std::nullopt;
                auto entry = db.getEntry(*guid);
                if (!entry || entry->type == resource::AssetType::COUNT) return std::nullopt;
                return entry->type;
            });

        auto toEntryData = [](const std::vector<asset::AssetDatabaseEntry>& entries)
        {
            std::vector<events::assetdb::AssetEntryData> result;
            result.reserve(entries.size());
            for (const auto& entry : entries)
            {
                result.push_back({entry.guid, entry.path, entry.type});
            }
            return result;
        };

        dispatcher.registerQueryHandler<events::assetdb::GetAllAssetsQuery>(
            [toEntryData](const events::assetdb::GetAllAssetsQuery&)
            {
                return toEntryData(asset::AssetDatabase::instance().getAllAssets());
            });

        dispatcher.registerQueryHandler<events::assetdb::GetAssetsByTypeQuery>(
            [toEntryData](const events::assetdb::GetAssetsByTypeQuery& q)
            {
                return toEntryData(asset::AssetDatabase::instance().getAssetsByType(q.type));
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

        // Check if already registered in the in-memory database
        if (db.getGUID(outputPath).has_value())
        {
            return;
        }

        // The import lib may have already created the .vfmeta sidecar with
        // importSource and importTimestamp. Load it to reuse the same GUID.
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(outputPath);
        auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);

        asset::AssetGUID guid;
        if (existingMeta.has_value())
        {
            guid = existingMeta->guid;
            db.registerAssetWithGUID(guid, outputPath, type, sourcePath);
        }
        else
        {
            guid = db.registerAsset(outputPath, type, sourcePath);

            // Create .vfmeta sidecar (fallback if import lib didn't create one)
            asset::AssetMetadata metadata;
            metadata.guid = guid;
            metadata.type = type;
            metadata.importSourcePath = sourcePath;
            stampImportTimestamp(metadata);
            asset::AssetMetadataSerializer::save(metadata, metaPath);
        }

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

        if (!guidOpt)
        {
            // New asset created in editor (material, animator, VFX, etc.) — register it
            resource::AssetType type = asset::AssetDatabaseMigrator::detectAssetTypeFromPath(filePath);
            if (type != resource::AssetType::COUNT)
            {
                // Check if .vfmeta already exists to preserve GUID and importSource/importTimestamp
                auto metaPath = asset::AssetMetadataSerializer::getMetaPath(filePath);
                auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);

                asset::AssetGUID guid;
                if (existingMeta.has_value())
                {
                    guid = existingMeta->guid;
                    const resource::AssetType effectiveType =
                        existingMeta->type != resource::AssetType::COUNT ? existingMeta->type : type;
                    std::string pluginTypeId = existingMeta->pluginTypeId;
                    const std::string detectedPluginTypeId = pluginTypeIdForAsset(filePath, effectiveType);
                    if (pluginTypeId.empty())
                        pluginTypeId = detectedPluginTypeId;

                    db.registerAssetWithGUID(guid, filePath, effectiveType,
                                             existingMeta->importSourcePath, pluginTypeId);

                    if (existingMeta->type == resource::AssetType::COUNT ||
                        existingMeta->pluginTypeId != pluginTypeId)
                    {
                        existingMeta->type = effectiveType;
                        existingMeta->pluginTypeId = pluginTypeId;
                        existingMeta->formatVersion = asset::AssetMetadata::kCurrentFormatVersion;
                        asset::AssetMetadataSerializer::save(*existingMeta, metaPath);
                    }
                }
                else
                {
                    const std::string pluginTypeId = pluginTypeIdForAsset(filePath, type);
                    guid = db.registerAsset(filePath, type, "", pluginTypeId);

                    asset::AssetMetadata metadata;
                    metadata.guid = guid;
                    metadata.type = type;
                    metadata.pluginTypeId = pluginTypeId;
                    stampImportTimestamp(metadata);
                    asset::AssetMetadataSerializer::save(metadata, metaPath);
                }

                guidOpt = guid;

                events::assetdb::AssetRegisteredNotification notification;
                notification.guid = guid;
                notification.path = filePath;
                notification.type = type;
                events::EventDispatcher::instance().publish(notification);
            }
        }

        if (!guidOpt) return;

        {
            auto metaPath = asset::AssetMetadataSerializer::getMetaPath(filePath);
            auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);
            if (existingMeta.has_value())
            {
                const resource::AssetType detectedType =
                    asset::AssetDatabaseMigrator::detectAssetTypeFromPath(filePath);
                const resource::AssetType effectiveType =
                    existingMeta->type != resource::AssetType::COUNT ? existingMeta->type : detectedType;
                if (effectiveType == resource::AssetType::PluginAsset && existingMeta->pluginTypeId.empty())
                {
                    const std::string pluginTypeId = pluginTypeIdForAsset(filePath, effectiveType);
                    if (!pluginTypeId.empty())
                    {
                        existingMeta->type = effectiveType;
                        existingMeta->pluginTypeId = pluginTypeId;
                        existingMeta->formatVersion = asset::AssetMetadata::kCurrentFormatVersion;
                        asset::AssetMetadataSerializer::save(*existingMeta, metaPath);
                        db.registerAssetWithGUID(existingMeta->guid, filePath, effectiveType,
                                                 existingMeta->importSourcePath, pluginTypeId);
                    }
                }
            }
            else
            {
                auto entry = db.getEntry(*guidOpt);
                if (entry.has_value() &&
                    entry->type == resource::AssetType::PluginAsset &&
                    entry->pluginTypeId.empty())
                {
                    const std::string pluginTypeId = pluginTypeIdForAsset(filePath, entry->type);
                    if (!pluginTypeId.empty())
                        db.registerAssetWithGUID(entry->guid, filePath, entry->type,
                                                 entry->importSource, pluginTypeId);
                }
            }
        }

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

    ::events::assetdb::RegenerateMetadataResult AssetDatabaseServiceImpl::regenerateMissingMetadata()
    {
        ::events::assetdb::RegenerateMetadataResult result;
        const std::string projRoot = getProjectRoot();
        if (projRoot.empty())
        {
            result.failures.push_back("No project is loaded");
            vfLogError("RegenerateMissingMetadata: no project root available");
            return result;
        }

        const auto migrationResult = asset::AssetDatabaseMigrator::migrateProject(projRoot);
        result.assetsScanned = migrationResult.assetsScanned;
        result.metaFilesCreated = migrationResult.metaFilesCreated;
        result.failures = migrationResult.errors;

        for (const auto& failure : result.failures)
        {
            vfLogError("RegenerateMissingMetadata: {}", failure);
        }

        auto& db = asset::AssetDatabase::instance();
        asset::DependencyScanner::scanAll(projRoot);
        if (!db.saveIndex(projRoot))
        {
            result.failures.push_back("Failed to save asset database index");
            vfLogError("RegenerateMissingMetadata: failed to save asset database index");
        }

        events::assetdb::AssetDatabaseRebuiltNotification notification;
        notification.assetCount = static_cast<uint32_t>(db.getAssetCount());
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("RegenerateMissingMetadata: scanned {}, regenerated {}, failures {}",
                  result.assetsScanned, result.metaFilesCreated, result.failures.size());
        return result;
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
