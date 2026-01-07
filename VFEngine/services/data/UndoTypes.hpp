#pragma once
#include "FileOperationsTypes.hpp"
#include <string>
#include <memory>
#include <map>
#include <vector>

namespace services
{
    // Base class for undoable commands
    class IUndoableCommand
    {
    public:
        virtual ~IUndoableCommand() = default;
        virtual void execute() = 0;
        virtual void undo() = 0;
        virtual std::string getDescription() const = 0;
    };

    // Stores backup data for file operations
    struct FileBackupData
    {
        std::string originalPath;
        std::string backupPath;  // Temp location for deleted files
        std::map<std::string, std::string> originalReferences;  // filepath -> original JSON content
    };

    // Undoable command for file move operations
    class MoveFileUndoCommand : public IUndoableCommand
    {
    public:
        MoveFileUndoCommand(std::string source, std::string dest,
                           std::vector<std::string> updatedRefs)
            : sourcePath(std::move(source))
            , destPath(std::move(dest))
            , updatedReferences(std::move(updatedRefs))
        {
        }

        void execute() override;  // Re-do: move from source to dest
        void undo() override;     // Move from dest back to source, restore refs
        std::string getDescription() const override { return "Move " + sourcePath; }

        std::string sourcePath;
        std::string destPath;
        std::vector<std::string> updatedReferences;
        std::map<std::string, std::string> originalRefContents;  // For undo
    };

    // Undoable command for file copy operations
    class CopyFileUndoCommand : public IUndoableCommand
    {
    public:
        CopyFileUndoCommand(std::string source, std::string dest)
            : sourcePath(std::move(source))
            , destPath(std::move(dest))
        {
        }

        void execute() override;  // Re-do: copy again
        void undo() override;     // Delete the copied file
        std::string getDescription() const override { return "Copy " + sourcePath; }

        std::string sourcePath;
        std::string destPath;
    };

    // Undoable command for file delete operations
    class DeleteFileUndoCommand : public IUndoableCommand
    {
    public:
        DeleteFileUndoCommand(std::string path, std::string backup)
            : originalPath(std::move(path))
            , backupPath(std::move(backup))
        {
        }

        void execute() override;  // Re-do: delete again
        void undo() override;     // Restore from backup
        std::string getDescription() const override { return "Delete " + originalPath; }

        std::string originalPath;
        std::string backupPath;
    };

    // Undoable command for file rename operations
    class RenameFileUndoCommand : public IUndoableCommand
    {
    public:
        RenameFileUndoCommand(std::string oldPath, std::string newPath,
                              std::vector<std::string> updatedRefs)
            : oldPath(std::move(oldPath))
            , newPath(std::move(newPath))
            , updatedReferences(std::move(updatedRefs))
        {
        }

        void execute() override;  // Re-do: rename again
        void undo() override;     // Rename back
        std::string getDescription() const override { return "Rename " + oldPath; }

        std::string oldPath;
        std::string newPath;
        std::vector<std::string> updatedReferences;
        std::map<std::string, std::string> originalRefContents;  // For undo
    };

    // Undoable command for folder creation
    class CreateFolderUndoCommand : public IUndoableCommand
    {
    public:
        explicit CreateFolderUndoCommand(std::string path)
            : folderPath(std::move(path))
        {
        }

        void execute() override;  // Re-do: create folder
        void undo() override;     // Delete folder
        std::string getDescription() const override { return "Create folder " + folderPath; }

        std::string folderPath;
    };

    // Composite command for batch operations
    class BatchUndoCommand : public IUndoableCommand
    {
    public:
        explicit BatchUndoCommand(std::string desc)
            : description(std::move(desc))
        {
        }

        void addCommand(std::unique_ptr<IUndoableCommand> cmd)
        {
            commands.push_back(std::move(cmd));
        }

        void execute() override
        {
            for (auto& cmd : commands)
            {
                cmd->execute();
            }
        }

        void undo() override
        {
            // Undo in reverse order
            for (auto it = commands.rbegin(); it != commands.rend(); ++it)
            {
                (*it)->undo();
            }
        }

        std::string getDescription() const override { return description; }

    private:
        std::string description;
        std::vector<std::unique_ptr<IUndoableCommand>> commands;
    };
}
