#include "FileOperationsServiceImpl.hpp"
#include "../data/UndoTypes.hpp"
#include "asset/AssetReferenceScanner.hpp"
#include "print/EditorLogger.hpp"
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

        dispatcher.registerCommandHandler<events::fileops::RenameFileCommand>(
            [this](const events::fileops::RenameFileCommand& cmd)
            {
                return renameFile(cmd.path, cmd.newName);
            });

        dispatcher.registerCommandHandler<events::fileops::CreateFolderCommand>(
            [this](const events::fileops::CreateFolderCommand& cmd)
            {
                return createFolder(cmd.parentPath, cmd.folderName);
            });

        dispatcher.registerCommandHandler<events::fileops::MoveFilesCommand>(
            [this](const events::fileops::MoveFilesCommand& cmd)
            {
                return moveFiles(cmd.sourcePaths, cmd.destFolder);
            });

        dispatcher.registerCommandHandler<events::fileops::CopyFilesCommand>(
            [this](const events::fileops::CopyFilesCommand& cmd)
            {
                return copyFiles(cmd.sourcePaths, cmd.destFolder);
            });

        dispatcher.registerCommandHandler<events::fileops::DeleteFilesCommand>(
            [this](const events::fileops::DeleteFilesCommand& cmd)
            {
                return deleteFiles(cmd.paths);
            });

        dispatcher.registerQueryHandler<events::fileops::CanMoveToQuery>(
            [this](const events::fileops::CanMoveToQuery& query)
            {
                return canMoveTo(query.sourcePath, query.destPath);
            });

        dispatcher.registerQueryHandler<events::fileops::GetConflictsQuery>(
            [this](const events::fileops::GetConflictsQuery& query)
            {
                return getConflicts(query.sourcePath, query.destPath);
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

        // Ensure uniqueness by adding counter if path already exists
        if (!fs::exists(basePath))
        {
            return basePath;
        }

        // Rare case: collision even with milliseconds, add counter
        for (int counter = 1; counter < 1000; ++counter)
        {
            std::string uniquePath = (trashFolder / (ss.str() + "_" + std::to_string(counter) + "_" + filename)).string();
            if (!fs::exists(uniquePath))
            {
                return uniquePath;
            }
        }

        // Extremely rare: 1000 collisions, use random suffix
        return (trashFolder / (ss.str() + "_" + std::to_string(std::rand()) + "_" + filename)).string();
    }

    bool FileOperationsServiceImpl::isSubPath(const fs::path& path, const fs::path& base) const
    {
        auto relativePath = fs::relative(path, base);
        if (relativePath.empty())
        {
            return false;
        }

        // Get the first component of the relative path
        auto firstComponent = *relativePath.begin();

        // If relative path starts with ".." it means path is outside base
        // If relative path is exactly "." it means path equals base (not a subpath)
        // Hidden files like ".hidden" ARE valid subpaths
        return firstComponent != ".." && firstComponent != ".";
    }

    bool FileOperationsServiceImpl::isValidFileName(const std::string& name)
    {
        // Empty names are invalid
        if (name.empty())
        {
            return false;
        }

        // Check for path traversal attempts
        if (name.find("..") != std::string::npos)
        {
            return false;
        }

        // Check for path separators (both Unix and Windows style)
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        {
            return false;
        }

        // Check for other invalid characters on Windows
        // These characters are not allowed in file names: < > : " | ? *
        const std::string invalidChars = "<>:\"|?*";
        for (char c : invalidChars)
        {
            if (name.find(c) != std::string::npos)
            {
                return false;
            }
        }

        // Check for reserved names on Windows (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
        std::string upperName = name;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

        // Remove extension for comparison
        size_t dotPos = upperName.find('.');
        std::string baseName = (dotPos != std::string::npos) ? upperName.substr(0, dotPos) : upperName;

        const std::vector<std::string> reservedNames = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };

        for (const auto& reserved : reservedNames)
        {
            if (baseName == reserved)
            {
                return false;
            }
        }

        return true;
    }

    FileOperationResult FileOperationsServiceImpl::moveFile(const std::string& sourcePath, const std::string& destPath)
    {
        FileOperationResult result;

        fs::path source(sourcePath);
        fs::path dest(destPath);

        // Validate source exists
        if (!fs::exists(source))
        {
            result.success = false;
            result.errorMessage = "Source file does not exist: " + sourcePath;
            return result;
        }

        // If dest is a directory, move into it
        if (fs::is_directory(dest))
        {
            dest = dest / source.filename();
        }

        // Check for conflicts
        if (fs::exists(dest))
        {
            result.success = false;
            result.errorMessage = "Destination already exists: " + dest.string();
            result.conflicts.push_back(dest.string());
            return result;
        }

        // Cannot move a folder into itself
        if (fs::is_directory(source) && isSubPath(dest, source))
        {
            result.success = false;
            result.errorMessage = "Cannot move a folder into itself";
            return result;
        }

        // Get original contents of referencing files for undo
        std::vector<std::pair<std::string, std::string>> originalContents;
        if (!projectRoot.empty())
        {
            auto scanResult = asset::AssetReferenceScanner::findReferencingFiles(sourcePath, projectRoot);
            originalContents = asset::AssetReferenceScanner::getOriginalContents(scanResult.referencingFiles);
        }

        // Perform the move
        std::error_code ec;
        fs::rename(source, dest, ec);

        if (ec)
        {
            result.success = false;
            result.errorMessage = "Failed to move file: " + ec.message();
            return result;
        }

        // Update references
        if (!projectRoot.empty())
        {
            auto updateResult = asset::AssetReferenceScanner::updateReferences(sourcePath, dest.string(), projectRoot);
            result.updatedReferences = updateResult.updatedFiles;
        }

        // Create undo command with exception safety
        if (undoRedoService)
        {
            try
            {
                auto undoCmd = std::make_unique<MoveFileUndoCommand>(sourcePath, dest.string(), result.updatedReferences, projectRoot);
                undoCmd->originalRefContents.insert(originalContents.begin(), originalContents.end());
                undoRedoService->pushCommand(std::move(undoCmd));
            }
            catch (const std::exception& e)
            {
                // File operation succeeded but undo registration failed
                // Log warning but don't fail the operation
                vfLogWarning("Move succeeded but undo registration failed: {}. Undo may not be available.", e.what());
            }
        }

        // Publish notification (also with exception safety)
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

        // Validate source exists
        if (!fs::exists(source))
        {
            result.success = false;
            result.errorMessage = "Source file does not exist: " + sourcePath;
            return result;
        }

        // If dest is a directory, copy into it
        if (fs::is_directory(dest))
        {
            dest = dest / source.filename();
        }

        // Check for conflicts
        if (fs::exists(dest))
        {
            result.success = false;
            result.errorMessage = "Destination already exists: " + dest.string();
            result.conflicts.push_back(dest.string());
            return result;
        }

        // Perform the copy
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

        // Create undo command with exception safety
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

        // Publish notification with exception safety
        try
        {
            events::fileops::FileCopiedNotification notification;
            notification.sourcePath = sourcePath;
            notification.destPath = dest.string();
            events::EventDispatcher::instance().publish(notification);
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Failed to publish file copied notification: {}", e.what());
        }

        result.success = true;
        vfLogInfo("Copied {} to {}", sourcePath, dest.string());
        return result;
    }

    FileOperationResult FileOperationsServiceImpl::deleteFile(const std::string& path)
    {
        FileOperationResult result;

        fs::path filePath(path);

        // Validate exists
        if (!fs::exists(filePath))
        {
            result.success = false;
            result.errorMessage = "File does not exist: " + path;
            return result;
        }

        // Ensure trash folder exists for undo
        if (!ensureTrashFolder())
        {
            result.success = false;
            result.errorMessage = "Failed to create trash folder for undo";
            return result;
        }

        // Move to trash instead of deleting
        std::string trashPath = generateTrashPath(path);

        std::error_code ec;
        fs::rename(filePath, trashPath, ec);

        if (ec)
        {
            // Try copy + delete if rename fails (cross-filesystem)
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

        // Create undo command with exception safety
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

        // Publish notification with exception safety
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

    FileOperationResult FileOperationsServiceImpl::renameFile(const std::string& path, const std::string& newName)
    {
        // Validate new name to prevent path traversal attacks
        if (!isValidFileName(newName))
        {
            FileOperationResult result;
            result.success = false;
            result.errorMessage = "Invalid file name: " + newName;
            vfLogWarning("Rejected invalid file name: {}", newName);
            return result;
        }

        fs::path filePath(path);
        fs::path newPath = filePath.parent_path() / newName;

        return moveFile(path, newPath.string());
    }

    FileOperationResult FileOperationsServiceImpl::createFolder(const std::string& parentPath, const std::string& folderName)
    {
        FileOperationResult result;

        // Validate folder name to prevent path traversal attacks
        if (!isValidFileName(folderName))
        {
            result.success = false;
            result.errorMessage = "Invalid folder name: " + folderName;
            vfLogWarning("Rejected invalid folder name: {}", folderName);
            return result;
        }

        fs::path newFolder = fs::path(parentPath) / folderName;

        // Check if already exists
        if (fs::exists(newFolder))
        {
            result.success = false;
            result.errorMessage = "Folder already exists: " + newFolder.string();
            result.conflicts.push_back(newFolder.string());
            return result;
        }

        std::error_code ec;
        fs::create_directory(newFolder, ec);

        if (ec)
        {
            result.success = false;
            result.errorMessage = "Failed to create folder: " + ec.message();
            return result;
        }

        // Create undo command with exception safety
        if (undoRedoService)
        {
            try
            {
                auto undoCmd = std::make_unique<CreateFolderUndoCommand>(newFolder.string());
                undoRedoService->pushCommand(std::move(undoCmd));
            }
            catch (const std::exception& e)
            {
                vfLogWarning("Create folder succeeded but undo registration failed: {}. Undo may not be available.", e.what());
            }
        }

        // Publish notification with exception safety
        try
        {
            events::fileops::FolderCreatedNotification notification;
            notification.path = newFolder.string();
            events::EventDispatcher::instance().publish(notification);
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Failed to publish folder created notification: {}", e.what());
        }

        result.success = true;
        vfLogInfo("Created folder: {}", newFolder.string());
        return result;
    }

    FileOperationResult FileOperationsServiceImpl::moveFiles(const std::vector<std::string>& sourcePaths, const std::string& destFolder)
    {
        FileOperationResult result;
        result.success = true;

        // Begin batch operation for undo grouping
        if (undoRedoService && sourcePaths.size() > 1)
        {
            undoRedoService->beginBatch("Move " + std::to_string(sourcePaths.size()) + " items");
        }

        for (const auto& source : sourcePaths)
        {
            auto moveResult = moveFile(source, destFolder);
            if (!moveResult.success)
            {
                result.success = false;
                result.errorMessage += moveResult.errorMessage + "\n";
                result.conflicts.insert(result.conflicts.end(),
                    moveResult.conflicts.begin(), moveResult.conflicts.end());
            }
            result.updatedReferences.insert(result.updatedReferences.end(),
                moveResult.updatedReferences.begin(), moveResult.updatedReferences.end());
        }

        // End batch operation
        if (undoRedoService && sourcePaths.size() > 1)
        {
            undoRedoService->endBatch();
        }

        return result;
    }

    FileOperationResult FileOperationsServiceImpl::copyFiles(const std::vector<std::string>& sourcePaths, const std::string& destFolder)
    {
        FileOperationResult result;
        result.success = true;

        // Begin batch operation for undo grouping
        if (undoRedoService && sourcePaths.size() > 1)
        {
            undoRedoService->beginBatch("Copy " + std::to_string(sourcePaths.size()) + " items");
        }

        for (const auto& source : sourcePaths)
        {
            auto copyResult = copyFile(source, destFolder);
            if (!copyResult.success)
            {
                result.success = false;
                result.errorMessage += copyResult.errorMessage + "\n";
                result.conflicts.insert(result.conflicts.end(),
                    copyResult.conflicts.begin(), copyResult.conflicts.end());
            }
        }

        // End batch operation
        if (undoRedoService && sourcePaths.size() > 1)
        {
            undoRedoService->endBatch();
        }

        return result;
    }

    FileOperationResult FileOperationsServiceImpl::deleteFiles(const std::vector<std::string>& paths)
    {
        FileOperationResult result;
        result.success = true;

        // Begin batch operation for undo grouping
        if (undoRedoService && paths.size() > 1)
        {
            undoRedoService->beginBatch("Delete " + std::to_string(paths.size()) + " items");
        }

        for (const auto& path : paths)
        {
            auto deleteResult = deleteFile(path);
            if (!deleteResult.success)
            {
                result.success = false;
                result.errorMessage += deleteResult.errorMessage + "\n";
            }
        }

        // End batch operation
        if (undoRedoService && paths.size() > 1)
        {
            undoRedoService->endBatch();
        }

        return result;
    }

    bool FileOperationsServiceImpl::canMoveTo(const std::string& sourcePath, const std::string& destPath) const
    {
        fs::path source(sourcePath);
        fs::path dest(destPath);

        // Source must exist
        if (!fs::exists(source))
        {
            return false;
        }

        // If dest is a directory, check the file path within it
        if (fs::is_directory(dest))
        {
            dest = dest / source.filename();
        }

        // Dest must not already exist
        if (fs::exists(dest))
        {
            return false;
        }

        // Cannot move a folder into itself
        if (fs::is_directory(source) && isSubPath(dest, source))
        {
            return false;
        }

        return true;
    }

    std::vector<std::string> FileOperationsServiceImpl::getConflicts(const std::string& sourcePath, const std::string& destPath) const
    {
        std::vector<std::string> conflicts;

        fs::path source(sourcePath);
        fs::path dest(destPath);

        // If dest is a directory, check the file path within it
        if (fs::is_directory(dest))
        {
            dest = dest / source.filename();
        }

        if (fs::exists(dest))
        {
            conflicts.push_back(dest.string());
        }

        return conflicts;
    }

    void FileOperationsServiceImpl::setProjectRoot(const std::string& root)
    {
        projectRoot = root;
        vfLogInfo("File operations project root set to: {}", root);
    }

    std::string FileOperationsServiceImpl::getProjectRoot() const
    {
        return projectRoot;
    }

    // ============================================
    // Undo Command Implementations
    // ============================================

    void MoveFileUndoCommand::execute()
    {
        // Re-do: move from source to dest
        std::error_code ec;
        fs::rename(sourcePath, destPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to redo move: " + ec.message());
        }

        // Update references after redo
        if (!projectRoot.empty())
        {
            asset::AssetReferenceScanner::updateReferences(sourcePath, destPath, projectRoot);
        }
    }

    void MoveFileUndoCommand::undo()
    {
        // Undo: move from dest back to source
        std::error_code ec;
        fs::rename(destPath, sourcePath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo move: " + ec.message());
        }

        // Restore original references
        asset::AssetReferenceScanner::restoreOriginalContents(
            std::vector<std::pair<std::string, std::string>>(
                originalRefContents.begin(), originalRefContents.end()));
    }

    void CopyFileUndoCommand::execute()
    {
        // Re-do: copy again
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
        // Undo: delete the copied file
        std::error_code ec;
        fs::remove_all(destPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo copy: " + ec.message());
        }
    }

    void DeleteFileUndoCommand::execute()
    {
        // Re-do: move file back to backup (not permanent delete, to allow undo again)
        std::error_code ec;
        fs::rename(originalPath, backupPath, ec);

        if (ec)
        {
            // Try copy + delete if rename fails (cross-filesystem)
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
    }

    void DeleteFileUndoCommand::undo()
    {
        // Undo: restore from backup
        std::error_code ec;
        fs::rename(backupPath, originalPath, ec);
        if (!ec)
        {
            return;
        }

        // Try copy if rename fails
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

    void CreateFolderUndoCommand::execute()
    {
        // Re-do: create folder again
        std::error_code ec;
        fs::create_directory(folderPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to redo create folder: " + ec.message());
        }
    }

    void CreateFolderUndoCommand::undo()
    {
        // Undo: delete the folder
        std::error_code ec;
        fs::remove(folderPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo create folder: " + ec.message());
        }
    }

    void RenameFileUndoCommand::execute()
    {
        // Re-do: rename again
        std::error_code ec;
        fs::rename(oldPath, newPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to redo rename: " + ec.message());
        }

        // Update references after redo
        if (!projectRoot.empty())
        {
            asset::AssetReferenceScanner::updateReferences(oldPath, newPath, projectRoot);
        }
    }

    void RenameFileUndoCommand::undo()
    {
        // Undo: rename back
        std::error_code ec;
        fs::rename(newPath, oldPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to undo rename: " + ec.message());
        }

        // Restore original references
        asset::AssetReferenceScanner::restoreOriginalContents(
            std::vector<std::pair<std::string, std::string>>(
                originalRefContents.begin(), originalRefContents.end()));
    }
}
