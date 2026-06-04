#pragma once
#include "../EventTypes.hpp"
#include "../../data/FileOperationsTypes.hpp"
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

    // ============================================
    // BATCH OPERATION NOTIFICATIONS - Async progress
    // ============================================

    struct FileOpBatchStartedNotification : INotification {
        uint32_t totalOperations = 0;
        std::string operationType; // "Moving", "Copying", "Deleting"

        std::string_view getName() const override { return "FileOpBatchStarted"; }
    };

    struct FileOpBatchProgressNotification : INotification {
        std::string currentFile;
        uint32_t completed = 0;
        uint32_t total = 0;

        std::string_view getName() const override { return "FileOpBatchProgress"; }
    };

    struct FileOpBatchCompletedNotification : INotification {
        bool success = true;
        std::string errorMessage;
        std::vector<std::string> conflicts;

        std::string_view getName() const override { return "FileOpBatchCompleted"; }
    };

}
