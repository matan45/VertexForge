#pragma once
#include "../interfaces/IFileOperationsService.hpp"
#include "../interfaces/IUndoRedoService.hpp"
#include "../events/FileOperationsEvents.hpp"
#include <memory>
#include <filesystem>

namespace services
{
    class FileOperationsServiceImpl : public IFileOperationsService
    {
    private:
        std::shared_ptr<IUndoRedoService> undoRedoService;
        std::string projectRoot; //TODO get this from project file
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
        bool ensureTrashFolder();
        std::string generateTrashPath(const std::string& originalPath);
        bool isSubPath(const std::filesystem::path& path, const std::filesystem::path& base) const;
    };
}
