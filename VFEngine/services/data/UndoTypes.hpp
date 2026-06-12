#pragma once
#include <string>
#include <memory>
#include <map>
#include <vector>

namespace services
{
    class IUndoableCommand
    {
    public:
        virtual ~IUndoableCommand() = default;
        virtual void execute() = 0;
        virtual void undo() = 0;
        virtual std::string getDescription() const = 0;
    };

    class MoveFileUndoCommand : public IUndoableCommand
    {
    public:
        MoveFileUndoCommand(std::string source, std::string dest,
                            std::vector<std::string> updatedRefs,
                            std::string projRoot)
            : sourcePath(std::move(source))
              , destPath(std::move(dest))
              , updatedReferences(std::move(updatedRefs))
              , projectRoot(std::move(projRoot))
        {
        }

        void execute() override;
        void undo() override;
        std::string getDescription() const override { return "Move " + sourcePath; }

        std::string sourcePath;
        std::string destPath;
        std::vector<std::string> updatedReferences;
        std::string projectRoot;
        std::map<std::string, std::string> originalRefContents;
    };


    class CopyFileUndoCommand : public IUndoableCommand
    {
    public:
        CopyFileUndoCommand(std::string source, std::string dest)
            : sourcePath(std::move(source))
              , destPath(std::move(dest))
        {
        }

        void execute() override;
        void undo() override;
        std::string getDescription() const override { return "Copy " + sourcePath; }

        std::string sourcePath;
        std::string destPath;
    };


    class DeleteFileUndoCommand : public IUndoableCommand
    {
    public:
        DeleteFileUndoCommand(std::string path, std::string backup)
            : originalPath(std::move(path))
              , backupPath(std::move(backup))
        {
        }

        void execute() override;
        void undo() override;
        std::string getDescription() const override { return "Delete " + originalPath; }

        std::string originalPath;
        std::string backupPath;

        // .vfmeta sidecar backup so undo restores the asset's GUID
        std::string metaOriginalPath;
        std::string metaBackupPath;

        // Assets that referenced this one at delete time; re-scanned on undo
        // to restore their dependency-graph edges
        std::vector<std::string> dependentPaths;
        std::string projectRoot;
    };


    class BatchUndoCommand : public IUndoableCommand
    {
    private:
        std::string description;
        std::vector<std::unique_ptr<IUndoableCommand>> commands;

    public:
        explicit BatchUndoCommand(std::string desc)
            : description(std::move(desc))
        {
        }

        void addCommand(std::unique_ptr<IUndoableCommand> cmd)
        {
            commands.push_back(std::move(cmd));
        }

        bool hasCommands() const
        {
            return !commands.empty();
        }

        size_t getCommandCount() const
        {
            return commands.size();
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
            for (auto it = commands.rbegin(); it != commands.rend(); ++it)
            {
                (*it)->undo();
            }
        }

        std::string getDescription() const override { return description; }
    };
}
