#include "AsyncFileOperations.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "events/UndoRedoEvents.hpp"
#include "threading/JobSystem.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    std::atomic<bool> AsyncFileOperations::busy{false};

    bool AsyncFileOperations::isBusy()
    {
        return busy.load();
    }

    void AsyncFileOperations::pasteAsync(std::vector<ClipboardItem> items, ClipboardOperation operation,
                                         const std::string& targetFolder, std::function<void()> onCutComplete)
    {
        if (busy.exchange(true))
        {
            vfLogWarning("File operation already in progress");
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        events::fileops::FileOpBatchStartedNotification startNotif;
        startNotif.totalOperations = static_cast<uint32_t>(items.size());
        startNotif.operationType = (operation == ClipboardOperation::Cut) ? "Moving" : "Copying";
        dispatcher.publish(startNotif);

        threading::JobSystem::instance().submit(
            [items = std::move(items), operation, targetFolder, onCutComplete = std::move(onCutComplete)]()
            {
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t total = static_cast<uint32_t>(items.size());

                if (total > 1)
                {
                    events::undoredo::BeginBatchCommand batchCmd;
                    batchCmd.description = (operation == ClipboardOperation::Cut ? "Move " : "Copy ") +
                                           std::to_string(total) + " items";
                    dispatcher.execute(batchCmd);
                }

                bool allSuccess = true;
                std::string errors;
                std::vector<std::string> conflicts;

                for (uint32_t i = 0; i < total; ++i)
                {
                    services::FileOperationResult opResult;
                    if (operation == ClipboardOperation::Cut)
                    {
                        events::fileops::MoveFileCommand cmd;
                        cmd.sourcePath = items[i].path;
                        cmd.destPath = targetFolder;
                        opResult = dispatcher.execute(cmd);
                    }
                    else
                    {
                        events::fileops::CopyFileCommand cmd;
                        cmd.sourcePath = items[i].path;
                        cmd.destPath = targetFolder;
                        opResult = dispatcher.execute(cmd);
                    }

                    if (!opResult.success)
                    {
                        allSuccess = false;
                        errors += opResult.errorMessage + "\n";
                        conflicts.insert(conflicts.end(),
                            opResult.conflicts.begin(), opResult.conflicts.end());
                    }

                    events::fileops::FileOpBatchProgressNotification progressNotif;
                    progressNotif.currentFile = fs::path(items[i].path).filename().string();
                    progressNotif.completed = i + 1;
                    progressNotif.total = total;
                    dispatcher.publish(progressNotif);
                }

                if (total > 1)
                {
                    dispatcher.execute(events::undoredo::EndBatchCommand{});
                }

                if (operation == ClipboardOperation::Cut && allSuccess && onCutComplete)
                {
                    onCutComplete();
                }

                events::fileops::FileOpBatchCompletedNotification completeNotif;
                completeNotif.success = allSuccess;
                completeNotif.errorMessage = errors;
                completeNotif.conflicts = std::move(conflicts);
                dispatcher.publish(completeNotif);

                busy.store(false);
            },
            threading::JobPriority::LOW
        );
    }

    void AsyncFileOperations::dropAsync(std::vector<std::string> paths, bool isMove,
                                        const std::string& targetFolder)
    {
        if (busy.exchange(true))
        {
            vfLogWarning("File operation already in progress");
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        events::fileops::FileOpBatchStartedNotification startNotif;
        startNotif.totalOperations = static_cast<uint32_t>(paths.size());
        startNotif.operationType = isMove ? "Moving" : "Copying";
        dispatcher.publish(startNotif);

        threading::JobSystem::instance().submit(
            [paths = std::move(paths), isMove, targetFolder]()
            {
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t total = static_cast<uint32_t>(paths.size());

                if (total > 1)
                {
                    events::undoredo::BeginBatchCommand batchCmd;
                    batchCmd.description = (isMove ? "Move " : "Copy ") +
                                           std::to_string(total) + " items";
                    dispatcher.execute(batchCmd);
                }

                bool allSuccess = true;
                std::string errors;
                std::vector<std::string> conflicts;

                for (uint32_t i = 0; i < total; ++i)
                {
                    services::FileOperationResult opResult;
                    if (isMove)
                    {
                        events::fileops::MoveFileCommand cmd;
                        cmd.sourcePath = paths[i];
                        cmd.destPath = targetFolder;
                        opResult = dispatcher.execute(cmd);
                    }
                    else
                    {
                        events::fileops::CopyFileCommand cmd;
                        cmd.sourcePath = paths[i];
                        cmd.destPath = targetFolder;
                        opResult = dispatcher.execute(cmd);
                    }

                    if (!opResult.success)
                    {
                        allSuccess = false;
                        errors += opResult.errorMessage + "\n";
                        conflicts.insert(conflicts.end(),
                            opResult.conflicts.begin(), opResult.conflicts.end());
                    }

                    events::fileops::FileOpBatchProgressNotification progressNotif;
                    progressNotif.currentFile = fs::path(paths[i]).filename().string();
                    progressNotif.completed = i + 1;
                    progressNotif.total = total;
                    dispatcher.publish(progressNotif);
                }

                if (total > 1)
                {
                    dispatcher.execute(events::undoredo::EndBatchCommand{});
                }

                events::fileops::FileOpBatchCompletedNotification completeNotif;
                completeNotif.success = allSuccess;
                completeNotif.errorMessage = errors;
                completeNotif.conflicts = std::move(conflicts);
                dispatcher.publish(completeNotif);

                busy.store(false);
            },
            threading::JobPriority::LOW
        );
    }

    void AsyncFileOperations::deleteAsync(const std::string& path)
    {
        if (busy.exchange(true))
        {
            vfLogWarning("File operation already in progress");
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        events::fileops::FileOpBatchStartedNotification startNotif;
        startNotif.totalOperations = 1;
        startNotif.operationType = "Deleting";
        dispatcher.publish(startNotif);

        threading::JobSystem::instance().submit(
            [path]()
            {
                auto& dispatcher = events::EventDispatcher::instance();

                events::fileops::DeleteFileCommand cmd;
                cmd.path = path;
                auto opResult = dispatcher.execute(cmd);

                events::fileops::FileOpBatchProgressNotification progressNotif;
                progressNotif.currentFile = fs::path(path).filename().string();
                progressNotif.completed = 1;
                progressNotif.total = 1;
                dispatcher.publish(progressNotif);

                events::fileops::FileOpBatchCompletedNotification completeNotif;
                completeNotif.success = opResult.success;
                completeNotif.errorMessage = opResult.errorMessage;
                completeNotif.conflicts = opResult.conflicts;
                dispatcher.publish(completeNotif);

                busy.store(false);
            },
            threading::JobPriority::LOW
        );
    }
}
