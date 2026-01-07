#pragma once
#include "../interfaces/IFileOperationsService.hpp"
#include "../interfaces/IUndoRedoService.hpp"
#include "../events/FileOperationsEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include <memory>
#include <filesystem>

namespace services
{
    class FileOperationsServiceImpl : public IFileOperationsService
    {
    public:
        explicit FileOperationsServiceImpl(std::shared_ptr<IUndoRedoService> undoRedoService);
        ~FileOperationsServiceImpl() override = default;

        void registerEventHandlers() override;

        // File Operations
        FileOperationResult moveFile(const std::string& sourcePath, const std::string& destPath) override;
        FileOperationResult copyFile(const std::string& sourcePath, const std::string& destPath) override;
        FileOperationResult deleteFile(const std::string& path) override;
        FileOperationResult renameFile(const std::string& path, const std::string& newName) override;
        FileOperationResult createFolder(const std::string& parentPath, const std::string& folderName) override;

        // Batch Operations
        FileOperationResult moveFiles(const std::vector<std::string>& sourcePaths, const std::string& destFolder) override;
        FileOperationResult copyFiles(const std::vector<std::string>& sourcePaths, const std::string& destFolder) override;
        FileOperationResult deleteFiles(const std::vector<std::string>& paths) override;

        // Validation
        bool canMoveTo(const std::string& sourcePath, const std::string& destPath) const override;
        std::vector<std::string> getConflicts(const std::string& sourcePath, const std::string& destPath) const override;

        // Configuration
        void setProjectRoot(const std::string& projectRoot) override;
        std::string getProjectRoot() const override;

    private:
        std::shared_ptr<IUndoRedoService> undoRedoService;
        std::string projectRoot;
        std::filesystem::path trashFolder;  // For storing deleted files for undo

        // Helper functions
        bool ensureTrashFolder();
        std::string generateTrashPath(const std::string& originalPath);
        bool isSubPath(const std::filesystem::path& path, const std::filesystem::path& base) const;
    };
}
