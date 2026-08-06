#include "FileOperationsServiceImpl.hpp"
#include "../../data/UndoTypes.hpp"
#include "asset/AssetReferenceScanner.hpp"
#include "asset/AssetGUID.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/DependencyScanner.hpp"
#include "resource/ResourceManager.hpp"
#include "terrain/TerrainLayerSidecar.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/FileOperationsEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // Re-scan dependencies of files whose contents were rewritten by a
    // reference update, so the GUID graph and .vfmeta sidecars stay in
    // sync. Must run AFTER the AssetDatabase path entry is up to date
    // (i.e. after FileMovedNotification), otherwise path-based references
    // fail to resolve and valid edges get dropped.
    void rescanDependencies(const std::vector<std::string>& files, const std::string& projectRoot)
    {
        auto& db = asset::AssetDatabase::instance();
        for (const auto& file : files)
        {
            if (auto guid = db.getGUID(file))
            {
                asset::DependencyScanner::scanAsset(*guid, file, projectRoot);
            }
        }
    }

    // VK-1648. An asset's sidecars have to travel with it on EVERY path that moves it -- the
    // original move, its redo, and its undo. Splitting them is silent and expensive: a terrain
    // whose header still has HAS_EDIT_LAYER_SIDECAR set but no `.vfterrainlayers` beside it opens
    // flattened with layer editing disabled, and the artist's whole reserved-layer stack (every
    // road corridor) is unrecoverable. Undo used to move only the main file, so Ctrl+Z after a
    // move was itself the data loss.
    //
    // Best-effort per sidecar: the asset itself has already moved by the time this runs, and
    // failing the whole operation over a sidecar would leave a worse mess than warning about it.
    void moveAssetSidecars(const fs::path& from, const fs::path& to)
    {
        const std::pair<fs::path, fs::path> sidecars[] = {
            {asset::AssetMetadataSerializer::getMetaPath(from),
             asset::AssetMetadataSerializer::getMetaPath(to)},
            {terrain::terrainLayerSidecarPath(from), terrain::terrainLayerSidecarPath(to)},
        };

        for (const auto& [sidecarFrom, sidecarTo] : sidecars)
        {
            std::error_code ec;
            if (!fs::exists(sidecarFrom, ec) || ec)
                continue;

            fs::rename(sidecarFrom, sidecarTo, ec);
            if (ec)
                vfLogWarning("Moved {} but its sidecar {} did not follow: {}. The asset and its "
                             "sidecar are now split; move it by hand.",
                             from.string(), sidecarFrom.string(), ec.message());
        }
    }
}

namespace services
{
    FileOperationsServiceImpl::FileOperationsServiceImpl(std::shared_ptr<IUndoRedoService> undoRedoService)
        : undoRedoService(std::move(undoRedoService))
    {
    }

    std::string FileOperationsServiceImpl::getProjectRoot() const
    {
        auto pathOpt = events::EventDispatcher::instance().query(events::project::GetProjectPathQuery{});
        if (pathOpt.has_value() && !pathOpt.value().empty())
        {
            return fs::path(pathOpt.value()).parent_path().string();
        }
        return {};
    }

    void FileOperationsServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::fileops::MoveFileCommand>(
            [this](const events::fileops::MoveFileCommand& cmd)
            {
                return moveFile(cmd.sourcePath, cmd.destPath);
            });

        dispatcher.registerCommandHandler<events::fileops::CopyFileCommand>(
            [this](const events::fileops::CopyFileCommand& cmd)
            {
                return copyFile(cmd.sourcePath, cmd.destPath);
            });

        dispatcher.registerCommandHandler<events::fileops::DeleteFileCommand>(
            [this](const events::fileops::DeleteFileCommand& cmd)
            {
                return deleteFile(cmd.path);
            });
    }

    bool FileOperationsServiceImpl::ensureTrashFolder()
    {
        if (trashFolder.empty())
        {
            trashFolder = fs::temp_directory_path() / "VertexForge_Trash";
        }

        std::error_code ec;
        if (!fs::exists(trashFolder, ec))
        {
            fs::create_directories(trashFolder, ec);
            if (ec)
            {
                vfLogError("Failed to create trash folder: {}", ec.message());
                return false;
            }
        }
        return true;
    }

    std::string FileOperationsServiceImpl::generateTrashPath(const std::string& originalPath)
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm timeInfo{};
        localtime_s(&timeInfo, &time);

        std::stringstream ss;
        ss << std::put_time(&timeInfo, "%Y%m%d_%H%M%S")
            << "_" << std::setfill('0') << std::setw(3) << ms.count();

        fs::path original(originalPath);
        std::string filename = original.filename().string();
        std::string basePath = (trashFolder / (ss.str() + "_" + filename)).string();

        if (!fs::exists(basePath))
        {
            return basePath;
        }

        for (int counter = 1; counter < 1000; ++counter)
        {
            std::string uniquePath = (trashFolder / (ss.str() + "_" + std::to_string(counter) + "_" + filename)).
                string();
            if (!fs::exists(uniquePath))
            {
                return uniquePath;
            }
        }

        return (trashFolder / (ss.str() + "_" + std::to_string(std::rand()) + "_" + filename)).string();
    }

    bool FileOperationsServiceImpl::isSubPath(const fs::path& path, const fs::path& base) const
    {
        auto relativePath = fs::relative(path, base);
        if (relativePath.empty())
        {
            return false;
        }

        auto firstComponent = *relativePath.begin();

        return firstComponent != ".." && firstComponent != ".";
    }

    FileOperationResult FileOperationsServiceImpl::moveFile(const std::string& sourcePath, const std::string& destPath)
    {
        FileOperationResult result;

        fs::path source(sourcePath);
        fs::path dest(destPath);

        if (!fs::exists(source))
        {
            result.success = false;
            result.errorMessage = "Source file does not exist: " + sourcePath;
            return result;
        }

        if (fs::is_directory(dest))
        {
            dest = dest / source.filename();
        }

        if (fs::exists(dest))
        {
            result.success = false;
            result.errorMessage = "Destination already exists: " + dest.string();
            result.conflicts.push_back(dest.string());
            return result;
        }

        if (fs::is_directory(source) && isSubPath(dest, source))
        {
            result.success = false;
            result.errorMessage = "Cannot move a folder into itself";
            return result;
        }

        std::string projRoot = getProjectRoot();

        std::vector<std::pair<std::string, std::string>> originalContents;
        if (!projRoot.empty())
        {
            auto scanResult = asset::AssetReferenceScanner::findReferencingFiles(sourcePath, projRoot);
            originalContents = asset::AssetReferenceScanner::getOriginalContents(scanResult.referencingFiles);
        }

        std::error_code ec;
        fs::rename(source, dest, ec);

        if (ec)
        {
            result.success = false;
            result.errorMessage = "Failed to move file: " + ec.message();
            return result;
        }

        // The `.vfmeta` and (VK-1646) the terrain's `.vfterrainlayers` follow the asset. Nothing
        // inside either needs rewriting — the GUID and the generation id both still describe the
        // same asset and the same bytes; only the name they hang off changed.
        moveAssetSidecars(source, dest);

        if (!projRoot.empty())
        {
            auto updateResult = asset::AssetReferenceScanner::updateReferences(sourcePath, dest.string(), projRoot);
            result.updatedReferences = updateResult.updatedFiles;
        }

        // With GUID-keyed caches, file renames don't invalidate cache entries

        if (undoRedoService)
        {
            try
            {
                auto undoCmd = std::make_unique<MoveFileUndoCommand>(sourcePath, dest.string(),
                                                                     result.updatedReferences, projRoot);
                undoCmd->originalRefContents.insert(originalContents.begin(), originalContents.end());
                undoRedoService->pushCommand(std::move(undoCmd));
            }
            catch (const std::exception& e)
            {
                vfLogWarning("Move succeeded but undo registration failed: {}. Undo may not be available.", e.what());
            }
        }

        try
        {
            events::fileops::FileMovedNotification notification;
            notification.oldPath = sourcePath;
            notification.newPath = dest.string();
            notification.updatedReferences = result.updatedReferences;
            events::EventDispatcher::instance().publish(notification);
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Failed to publish file moved notification: {}", e.what());
        }

        // After the notification the AssetDatabase maps the new path, so the
        // rewritten referencing files re-scan to the same GUIDs
        if (!projRoot.empty() && !result.updatedReferences.empty())
        {
            rescanDependencies(result.updatedReferences, projRoot);
        }

        result.success = true;
        vfLogInfo("Moved {} to {}", sourcePath, dest.string());
        return result;
    }

    FileOperationResult FileOperationsServiceImpl::copyFile(const std::string& sourcePath, const std::string& destPath)
    {
        FileOperationResult result;

        fs::path source(sourcePath);
        fs::path dest(destPath);

        if (!fs::exists(source))
        {
            result.success = false;
            result.errorMessage = "Source file does not exist: " + sourcePath;
            return result;
        }

        if (fs::is_directory(dest))
        {
            dest = dest / source.filename();
        }

        if (fs::exists(dest))
        {
            result.success = false;
            result.errorMessage = "Destination already exists: " + dest.string();
            result.conflicts.push_back(dest.string());
            return result;
        }

        std::error_code ec;
        if (fs::is_directory(source))
        {
            fs::copy(source, dest, fs::copy_options::recursive, ec);
        }
        else
        {
            fs::copy_file(source, dest, ec);
        }

        if (ec)
        {
            result.success = false;
            result.errorMessage = "Failed to copy file: " + ec.message();
            return result;
        }

        // Create new .vfmeta with new GUID for the copy (copies are distinct assets)
        asset::AssetGUID copyGuid;
        auto sourceMeta = asset::AssetMetadataSerializer::getMetaPath(source);
        if (fs::exists(sourceMeta, ec))
        {
            auto originalMeta = asset::AssetMetadataSerializer::load(sourceMeta);
            if (originalMeta)
            {
                asset::AssetMetadata newMeta = *originalMeta;
                newMeta.guid = asset::AssetGUID::generate();
                copyGuid = newMeta.guid;
                auto destMeta = asset::AssetMetadataSerializer::getMetaPath(dest);
                asset::AssetMetadataSerializer::save(newMeta, destMeta);
            }
        }

        // VK-1646: duplicate a terrain and its authoring state comes along, otherwise the copy
        // opens with layer editing disabled and the artist quietly loses the stack.
        //
        // The bytes cannot just be copied, though: the line above minted a NEW GUID for the copy,
        // so a verbatim sidecar would name the original and read as an orphan. Its generation id
        // still holds — fs::copy_file reproduced the terrain byte for byte — so only the identity
        // is re-stamped.
        auto sourceLayers = terrain::terrainLayerSidecarPath(source);
        if (fs::exists(sourceLayers, ec))
        {
            const auto destLayers = terrain::terrainLayerSidecarPath(dest);
            fs::copy_file(sourceLayers, destLayers, ec);
            if (ec)
            {
                vfLogWarning("Copied {} but its edit-layer sidecar did not: {}. The copy will open "
                             "with layer editing disabled.", sourcePath, ec.message());
                ec.clear();
            }
            else
            {
                terrain::TerrainLayerSidecarMeta layerMeta;
                if (terrain::peekTerrainLayerSidecar(destLayers, layerMeta) ==
                        terrain::TerrainLayerSidecarStatus::Ok &&
                    !terrain::rebindTerrainLayerSidecar(destLayers, copyGuid.getValue(),
                                                        layerMeta.generationId))
                {
                    vfLogWarning("Copied {} but could not re-point its edit-layer sidecar at the "
                                 "copy's GUID; the copy will treat it as an orphan.", sourcePath);
                }
            }
        }

        if (undoRedoService)
        {
            try
            {
                auto undoCmd = std::make_unique<CopyFileUndoCommand>(sourcePath, dest.string());
                undoRedoService->pushCommand(std::move(undoCmd));
            }
            catch (const std::exception& e)
            {
                vfLogWarning("Copy succeeded but undo registration failed: {}. Undo may not be available.", e.what());
            }
        }

        result.success = true;
        vfLogInfo("Copied {} to {}", sourcePath, dest.string());
        return result;
    }

    FileOperationResult FileOperationsServiceImpl::deleteFile(const std::string& path)
    {
        FileOperationResult result;

        fs::path filePath(path);

        if (!fs::exists(filePath))
        {
            result.success = false;
            result.errorMessage = "File does not exist: " + path;
            return result;
        }

        if (!ensureTrashFolder())
        {
            result.success = false;
            result.errorMessage = "Failed to create trash folder for undo";
            return result;
        }

        // Capture referencing assets before the database entry disappears,
        // so undo can restore their dependency-graph edges
        std::string projRoot = getProjectRoot();
        std::vector<std::string> dependentPaths;
        {
            auto& db = asset::AssetDatabase::instance();
            if (auto guidOpt = db.getGUID(path))
            {
                for (const auto& dependent : db.getDependents(*guidOpt))
                {
                    if (auto depPath = db.getPath(dependent))
                    {
                        dependentPaths.push_back(*depPath);
                    }
                }
            }
        }

        std::string trashPath = generateTrashPath(path);

        std::error_code ec;
        fs::rename(filePath, trashPath, ec);

        if (ec)
        {
            if (fs::is_directory(filePath))
            {
                fs::copy(filePath, trashPath, fs::copy_options::recursive, ec);
            }
            else
            {
                fs::copy_file(filePath, trashPath, ec);
            }

            if (!ec)
            {
                fs::remove_all(filePath, ec);
            }
        }

        if (ec)
        {
            result.success = false;
            result.errorMessage = "Failed to delete file: " + ec.message();
            return result;
        }

        // Also move .vfmeta to trash, remembering where so undo can restore
        // the asset's GUID
        std::string metaOriginalPath;
        std::string metaBackupPath;
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(filePath);
        if (fs::exists(metaPath, ec))
        {
            std::string metaTrashPath = generateTrashPath(metaPath.string());
            fs::rename(metaPath, metaTrashPath, ec);
            if (!ec)
            {
                metaOriginalPath = metaPath.string();
                metaBackupPath = metaTrashPath;
            }
        }

        // VK-1646: the terrain's edit-layer sidecar goes to the trash alongside it, so undo can
        // bring back the layer stack and not just the flattened heights.
        std::string layersOriginalPath;
        std::string layersBackupPath;
        auto layersPath = terrain::terrainLayerSidecarPath(filePath);
        if (fs::exists(layersPath, ec))
        {
            std::string layersTrashPath = generateTrashPath(layersPath.string());
            fs::rename(layersPath, layersTrashPath, ec);
            if (!ec)
            {
                layersOriginalPath = layersPath.string();
                layersBackupPath = layersTrashPath;
            }
        }
        ec.clear();

        // Remove stale ResourceManager cache entries
        // With GUID-keyed caches, deletions don't need cache cleanup

        if (undoRedoService)
        {
            try
            {
                auto undoCmd = std::make_unique<DeleteFileUndoCommand>(path, trashPath);
                undoCmd->metaOriginalPath = metaOriginalPath;
                undoCmd->metaBackupPath = metaBackupPath;
                undoCmd->layersOriginalPath = layersOriginalPath;
                undoCmd->layersBackupPath = layersBackupPath;
                undoCmd->dependentPaths = std::move(dependentPaths);
                undoCmd->projectRoot = projRoot;
                undoRedoService->pushCommand(std::move(undoCmd));
            }
            catch (const std::exception& e)
            {
                vfLogWarning("Delete succeeded but undo registration failed: {}. Undo may not be available.", e.what());
            }
        }

        try
        {
            events::fileops::FileDeletedNotification notification;
            notification.path = path;
            events::EventDispatcher::instance().publish(notification);
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Failed to publish file deleted notification: {}", e.what());
        }

        result.success = true;
        vfLogInfo("Deleted {} (backed up to {})", path, trashPath);
        return result;
    }

    // ============================================
    // Undo Command Implementations
    // ============================================

    void MoveFileUndoCommand::execute()
    {
        std::error_code ec;
        fs::rename(sourcePath, destPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to redo move: " + ec.message());
        }

        // Same sidecar contract as moveFile() — see moveAssetSidecars().
        moveAssetSidecars(sourcePath, destPath);

        if (!projectRoot.empty())
        {
            asset::AssetReferenceScanner::updateReferences(sourcePath, destPath, projectRoot);
        }

        // With GUID-keyed caches, no migration needed

        // Keep the AssetDatabase path entry in sync on redo (and refresh UI)
        events::fileops::FileMovedNotification notification;
        notification.oldPath = sourcePath;
        notification.newPath = destPath;
        notification.updatedReferences = updatedReferences;
        events::EventDispatcher::instance().publish(notification);

        if (!projectRoot.empty())
        {
            rescanDependencies(updatedReferences, projectRoot);
        }
    }

    void MoveFileUndoCommand::undo()
    {
        std::error_code ec;
        fs::rename(destPath, sourcePath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo move: " + ec.message());
        }

        // The sidecars come back too. Without this, Ctrl+Z after moving a .vfTerrain left the
        // layer sidecar at the destination and the terrain opened flattened — see
        // moveAssetSidecars().
        moveAssetSidecars(destPath, sourcePath);

        asset::AssetReferenceScanner::restoreOriginalContents(
            std::vector<std::pair<std::string, std::string>>(
                originalRefContents.begin(), originalRefContents.end()));

        // With GUID-keyed caches, no migration needed

        // Notify UI to refresh
        events::fileops::FileMovedNotification notification;
        notification.oldPath = destPath;
        notification.newPath = sourcePath;
        events::EventDispatcher::instance().publish(notification);

        // Database maps the original path again — re-scan the restored
        // referencing files so graph and .vfmeta deps stay in sync
        if (!projectRoot.empty())
        {
            rescanDependencies(updatedReferences, projectRoot);
        }
    }

    void CopyFileUndoCommand::execute()
    {
        std::error_code ec;
        fs::path source(sourcePath);
        if (fs::is_directory(source))
        {
            fs::copy(sourcePath, destPath, fs::copy_options::recursive, ec);
        }
        else
        {
            fs::copy_file(sourcePath, destPath, ec);
        }
        if (ec)
        {
            throw std::runtime_error("Failed to redo copy: " + ec.message());
        }
    }

    void CopyFileUndoCommand::undo()
    {
        std::error_code ec;
        fs::remove_all(destPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo copy: " + ec.message());
        }

        // Notify UI to refresh
        events::fileops::FileDeletedNotification notification;
        notification.path = destPath;
        events::EventDispatcher::instance().publish(notification);
    }

    void DeleteFileUndoCommand::execute()
    {
        std::error_code ec;
        fs::rename(originalPath, backupPath, ec);

        if (ec)
        {
            fs::path original(originalPath);
            if (fs::is_directory(original))
            {
                fs::copy(originalPath, backupPath, fs::copy_options::recursive, ec);
            }
            else
            {
                fs::copy_file(originalPath, backupPath, ec);
            }

            if (!ec)
            {
                fs::remove_all(originalPath, ec);
            }
        }

        if (ec)
        {
            throw std::runtime_error("Failed to redo delete: " + ec.message());
        }

        // With GUID-keyed caches, deletions don't need cache cleanup

        // Re-trash the .vfmeta sidecar as the original delete did
        if (!metaOriginalPath.empty())
        {
            fs::rename(metaOriginalPath, metaBackupPath, ec);
        }

        // ...and the edit-layer sidecar with it (VK-1646)
        if (!layersOriginalPath.empty())
        {
            fs::rename(layersOriginalPath, layersBackupPath, ec);
        }

        // Unregister from the AssetDatabase, as the original delete did
        events::fileops::FileDeletedNotification notification;
        notification.path = originalPath;
        events::EventDispatcher::instance().publish(notification);
    }

    void DeleteFileUndoCommand::undo()
    {
        std::error_code ec;
        fs::rename(backupPath, originalPath, ec);
        if (ec)
        {
            fs::path backup(backupPath);
            if (fs::is_directory(backup))
            {
                fs::copy(backupPath, originalPath, fs::copy_options::recursive, ec);
            }
            else
            {
                fs::copy_file(backupPath, originalPath, ec);
            }

            if (ec)
            {
                throw std::runtime_error("Failed to restore deleted file: " + ec.message());
            }
        }

        // Restore the .vfmeta sidecar so the asset keeps its GUID
        if (!metaBackupPath.empty())
        {
            fs::rename(metaBackupPath, metaOriginalPath, ec);
        }

        // ...and the edit-layer sidecar, so the restored terrain keeps its layer stack (VK-1646)
        if (!layersBackupPath.empty())
        {
            fs::rename(layersBackupPath, layersOriginalPath, ec);
        }

        // Re-register the asset (reuses the GUID from the restored .vfmeta)
        // and re-scan its own dependencies
        events::resource::AssetSavedNotification savedNotification;
        savedNotification.filePath = originalPath;
        events::EventDispatcher::instance().publish(savedNotification);

        // Referencing assets lost their edges when this GUID was
        // unregistered — re-scan them to restore the graph
        if (!projectRoot.empty())
        {
            rescanDependencies(dependentPaths, projectRoot);
        }

        // Notify UI to refresh
        events::fileops::FileMovedNotification notification;
        notification.oldPath = backupPath;
        notification.newPath = originalPath;
        events::EventDispatcher::instance().publish(notification);
    }
}
