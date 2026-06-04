#pragma once
#include "../../interfaces/project/IFileOperationsService.hpp"
#include "../../interfaces/editor/IUndoRedoService.hpp"
#include "../../events/project/FileOperationsEvents.hpp"
#include <memory>
#include <filesystem>

namespace services
{
    class FileOperationsServiceImpl : public IFileOperationsService
    {
    private:
        std::shared_ptr<IUndoRedoService> undoRedoService;
        std::filesystem::path trashFolder;  // For storing deleted files for undo

    public:
        explicit FileOperationsServiceImpl(std::shared_ptr<IUndoRedoService> undoRedoService);
        ~FileOperationsServiceImpl() override = default;

        void registerEventHandlers() override;

        // File Operations
        FileOperationResult moveFile(const std::string& sourcePath, const std::string& destPath) override;
        FileOperationResult copyFile(const std::string& sourcePath, const std::string& destPath) override;
        FileOperationResult deleteFile(const std::string& path) override;

    private:
        std::string getProjectRoot() const;
        bool ensureTrashFolder();
        std::string generateTrashPath(const std::string& originalPath);
        bool isSubPath(const std::filesystem::path& path, const std::filesystem::path& base) const;
    };
}
