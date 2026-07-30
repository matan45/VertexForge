#pragma once
#include <cstddef>
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

        // Approximate heap bytes this command holds, for the undo service's byte budget.
        // Default 0 = negligible; only snapshot-shaped commands (terrain strokes, cave
        // strokes) override it. A depth-only cap cannot bound RAM when one entry ranges
        // from a few bytes (a file rename) to megabytes (a terrain paint stroke).
        virtual size_t getMemoryFootprint() const { return 0; }
    };

    // Snapshot of the undo service's history state, for the preferences UI and for
    // testing the trim behaviour end to end.
    struct UndoHistoryStats
    {
        size_t undoCount = 0;
        size_t redoCount = 0;
        size_t totalBytes = 0;  // across BOTH stacks
        size_t maxDepth = 0;    // 0 = unlimited
        size_t maxBytes = 0;    // 0 = unlimited
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


    // Adapts a shared_ptr<IUndoableCommand> so it can be stored in the
    // unique_ptr-based undo stacks (used by the PushUndoableCommand event,
    // which lets services that don't hold the undo service push commands).
    class SharedUndoCommand : public IUndoableCommand
    {
    private:
        std::shared_ptr<IUndoableCommand> inner;

    public:
        explicit SharedUndoCommand(std::shared_ptr<IUndoableCommand> c)
            : inner(std::move(c))
        {
        }

        void execute() override { if (inner) inner->execute(); }
        void undo() override { if (inner) inner->undo(); }
        std::string getDescription() const override
        {
            return inner ? inner->getDescription() : std::string();
        }

        // Must forward: PushUndoableCommand wraps EVERY event-pushed command in this
        // adapter, so without the forward the byte budget silently reads zero for all
        // of them.
        size_t getMemoryFootprint() const override
        {
            return inner ? inner->getMemoryFootprint() : 0;
        }
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

        size_t getMemoryFootprint() const override
        {
            size_t total = 0;
            for (const auto& cmd : commands)
            {
                if (cmd)
                    total += cmd->getMemoryFootprint();
            }
            return total;
        }
    };
}
