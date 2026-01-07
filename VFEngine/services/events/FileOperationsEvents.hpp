#pragma once
#include "EventTypes.hpp"
#include "../data/FileOperationsTypes.hpp"
#include <string>
#include <vector>

namespace events::fileops {

    // ============================================
    // COMMANDS - File operations that change state
    // ============================================

    struct MoveFileCommand : ICommand<services::FileOperationResult> {
        std::string sourcePath;
        std::string destPath;

        std::string_view getName() const override { return "MoveFile"; }
    };

    struct CopyFileCommand : ICommand<services::FileOperationResult> {
        std::string sourcePath;
        std::string destPath;

        std::string_view getName() const override { return "CopyFile"; }
    };

    struct DeleteFileCommand : ICommand<services::FileOperationResult> {
        std::string path;

        std::string_view getName() const override { return "DeleteFile"; }
    };

    struct RenameFileCommand : ICommand<services::FileOperationResult> {
        std::string path;
        std::string newName;

        std::string_view getName() const override { return "RenameFile"; }
    };

    struct CreateFolderCommand : ICommand<services::FileOperationResult> {
        std::string parentPath;
        std::string folderName;

        std::string_view getName() const override { return "CreateFolder"; }
    };

    // Batch operations
    struct MoveFilesCommand : ICommand<services::FileOperationResult> {
        std::vector<std::string> sourcePaths;
        std::string destFolder;

        std::string_view getName() const override { return "MoveFiles"; }
    };

    struct CopyFilesCommand : ICommand<services::FileOperationResult> {
        std::vector<std::string> sourcePaths;
        std::string destFolder;

        std::string_view getName() const override { return "CopyFiles"; }
    };

    struct DeleteFilesCommand : ICommand<services::FileOperationResult> {
        std::vector<std::string> paths;

        std::string_view getName() const override { return "DeleteFiles"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct CanMoveToQuery : IQuery<bool> {
        std::string sourcePath;
        std::string destPath;

        std::string_view getName() const override { return "CanMoveTo"; }
    };

    struct GetConflictsQuery : IQuery<std::vector<std::string>> {
        std::string sourcePath;
        std::string destPath;

        std::string_view getName() const override { return "GetConflicts"; }
    };

    // ============================================
    // NOTIFICATIONS - Broadcast state changes
    // ============================================

    struct FileMovedNotification : INotification {
        std::string oldPath;
        std::string newPath;
        std::vector<std::string> updatedReferences;

        std::string_view getName() const override { return "FileMoved"; }
    };

    struct FileCopiedNotification : INotification {
        std::string sourcePath;
        std::string destPath;

        std::string_view getName() const override { return "FileCopied"; }
    };

    struct FileDeletedNotification : INotification {
        std::string path;

        std::string_view getName() const override { return "FileDeleted"; }
    };

    struct FileRenamedNotification : INotification {
        std::string oldPath;
        std::string newPath;
        std::vector<std::string> updatedReferences;

        std::string_view getName() const override { return "FileRenamed"; }
    };

    struct FolderCreatedNotification : INotification {
        std::string path;

        std::string_view getName() const override { return "FolderCreated"; }
    };

    struct FileOperationFailedNotification : INotification {
        std::string operation;
        std::string path;
        std::string errorMessage;

        std::string_view getName() const override { return "FileOperationFailed"; }
    };

}
