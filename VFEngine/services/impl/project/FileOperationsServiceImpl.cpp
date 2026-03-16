#include "FileOperationsServiceImpl.hpp"
#include "../../data/UndoTypes.hpp"
#include "asset/AssetReferenceScanner.hpp"
#include "asset/AssetGUID.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "resource/ResourceManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/FileOperationsEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

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

        // Move .vfmeta sidecar if it exists
        auto sourceMeta = asset::AssetMetadataSerializer::getMetaPath(source);
        if (fs::exists(sourceMeta, ec))
        {
            auto destMeta = asset::AssetMetadataSerializer::getMetaPath(dest);
            fs::rename(sourceMeta, destMeta, ec);
        }

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
        auto sourceMeta = asset::AssetMetadataSerializer::getMetaPath(source);
        if (fs::exists(sourceMeta, ec))
        {
            auto originalMeta = asset::AssetMetadataSerializer::load(sourceMeta);
            if (originalMeta)
            {
                asset::AssetMetadata newMeta = *originalMeta;
                newMeta.guid = asset::AssetGUID::generate();
                auto destMeta = asset::AssetMetadataSerializer::getMetaPath(dest);
                asset::AssetMetadataSerializer::save(newMeta, destMeta);
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

        // Also move .vfmeta to trash
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(filePath);
        if (fs::exists(metaPath, ec))
        {
            std::string metaTrashPath = generateTrashPath(metaPath.string());
            fs::rename(metaPath, metaTrashPath, ec);
        }

        // Remove stale ResourceManager cache entries
        // With GUID-keyed caches, deletions don't need cache cleanup

        if (undoRedoService)
        {
            try
            {
                auto undoCmd = std::make_unique<DeleteFileUndoCommand>(path, trashPath);
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

        if (!projectRoot.empty())
        {
            asset::AssetReferenceScanner::updateReferences(sourcePath, destPath, projectRoot);
        }

        // With GUID-keyed caches, no migration needed
    }

    void MoveFileUndoCommand::undo()
    {
        std::error_code ec;
        fs::rename(destPath, sourcePath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo move: " + ec.message());
        }

        asset::AssetReferenceScanner::restoreOriginalContents(
            std::vector<std::pair<std::string, std::string>>(
                originalRefContents.begin(), originalRefContents.end()));

        // With GUID-keyed caches, no migration needed

        // Notify UI to refresh
        events::fileops::FileMovedNotification notification;
        notification.oldPath = destPath;
        notification.newPath = sourcePath;
        events::EventDispatcher::instance().publish(notification);
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
    }

    void DeleteFileUndoCommand::undo()
    {
        std::error_code ec;
        fs::rename(backupPath, originalPath, ec);
        if (!ec)
        {
            // Notify UI to refresh
            events::fileops::FileMovedNotification notification;
            notification.oldPath = backupPath;
            notification.newPath = originalPath;
            events::EventDispatcher::instance().publish(notification);
            return;
        }

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

        // Notify UI to refresh
        events::fileops::FileMovedNotification notification;
        notification.oldPath = backupPath;
        notification.newPath = originalPath;
        events::EventDispatcher::instance().publish(notification);
    }
}
