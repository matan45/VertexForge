#pragma once
#include "EventTypes.hpp"
#include "../data/FileOperationsTypes.hpp"
#include <string>
#include <vector>

namespace events::fileops {

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

    // ============================================
    // NOTIFICATIONS - Broadcast state changes
    // ============================================

    struct FileMovedNotification : INotification {
        std::string oldPath;
        std::string newPath;
        std::vector<std::string> updatedReferences;

        std::string_view getName() const override { return "FileMoved"; }
    };

    struct FileDeletedNotification : INotification {
        std::string path;

        std::string_view getName() const override { return "FileDeleted"; }
    };

    struct FolderCreatedNotification : INotification {
        std::string path;

        std::string_view getName() const override { return "FolderCreated"; }
    };

}
