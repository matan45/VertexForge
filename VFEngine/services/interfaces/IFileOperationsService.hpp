#pragma once
#include "../data/FileOperationsTypes.hpp"
#include <string>
#include <vector>

namespace services
{
    // File operations service interface - manages file operations with reference tracking
    class IFileOperationsService
    {
    public:
        virtual ~IFileOperationsService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // ============================================
        // File Operations
        // ============================================

        // Move a file or folder to a new location
        // Updates all references in project files
        virtual FileOperationResult moveFile(const std::string& sourcePath, const std::string& destPath) = 0;

        // Copy a file or folder to a new location
        virtual FileOperationResult copyFile(const std::string& sourcePath, const std::string& destPath) = 0;

        // Delete a file or folder (moves to trash/backup for undo)
        virtual FileOperationResult deleteFile(const std::string& path) = 0;

        // Rename a file or folder
        // Updates all references in project files
        virtual FileOperationResult renameFile(const std::string& path, const std::string& newName) = 0;

        // Create a new folder
        virtual FileOperationResult createFolder(const std::string& parentPath, const std::string& folderName) = 0;

        // ============================================
        // Batch Operations
        // ============================================

        // Move multiple files/folders to a destination
        virtual FileOperationResult moveFiles(const std::vector<std::string>& sourcePaths, const std::string& destFolder) = 0;

        // Copy multiple files/folders to a destination
        virtual FileOperationResult copyFiles(const std::vector<std::string>& sourcePaths, const std::string& destFolder) = 0;

        // Delete multiple files/folders
        virtual FileOperationResult deleteFiles(const std::vector<std::string>& paths) = 0;

        // ============================================
        // Validation
        // ============================================

        // Check if a move operation is valid
        virtual bool canMoveTo(const std::string& sourcePath, const std::string& destPath) const = 0;

        // Get list of conflicts for a move operation
        virtual std::vector<std::string> getConflicts(const std::string& sourcePath, const std::string& destPath) const = 0;

        // ============================================
        // Configuration
        // ============================================

        // Set the project root directory for reference scanning
        virtual void setProjectRoot(const std::string& projectRoot) = 0;

        // Get the current project root directory
        virtual std::string getProjectRoot() const = 0;
    };
}
