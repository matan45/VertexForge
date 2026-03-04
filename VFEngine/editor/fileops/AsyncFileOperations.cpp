#include "print/Log.hpp"
#include "AsyncFileOperations.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "events/UndoRedoEvents.hpp"
#include "threading/JobSystem.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    std::atomic<bool> AsyncFileOperations::busy{false};

    bool AsyncFileOperations::isBusy()
    {
        return busy.load();
    }

    void AsyncFileOperations::executeBatch(std::vector<std::string> sourcePaths, bool isMove,
                                           const std::string& targetFolder, std::function<void()> onComplete)
    {
        if (busy.exchange(true))
        {
            vfLogWarning("File operation already in progress");
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        events::fileops::FileOpBatchStartedNotification startNotif;
        startNotif.totalOperations = static_cast<uint32_t>(sourcePaths.size());
        startNotif.operationType = isMove ? "Moving" : "Copying";
        dispatcher.publish(startNotif);

        threading::JobSystem::instance().submit(
            [sourcePaths = std::move(sourcePaths), isMove, targetFolder,
             onComplete = std::move(onComplete)]()
            {
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t total = static_cast<uint32_t>(sourcePaths.size());

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
                        cmd.sourcePath = sourcePaths[i];
                        cmd.destPath = targetFolder;
                        opResult = dispatcher.execute(cmd);
                    }
                    else
                    {
                        events::fileops::CopyFileCommand cmd;
                        cmd.sourcePath = sourcePaths[i];
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
                    progressNotif.currentFile = fs::path(sourcePaths[i]).filename().string();
                    progressNotif.completed = i + 1;
                    progressNotif.total = total;
                    dispatcher.publish(progressNotif);
                }

                if (total > 1)
                {
                    dispatcher.execute(events::undoredo::EndBatchCommand{});
                }

                if (allSuccess && onComplete)
                {
                    onComplete();
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

    void AsyncFileOperations::pasteAsync(std::vector<ClipboardItem> items, ClipboardOperation operation,
                                         const std::string& targetFolder, std::function<void()> onCutComplete)
    {
        std::vector<std::string> paths;
        paths.reserve(items.size());
        for (auto& item : items)
        {
            paths.push_back(std::move(item.path));
        }

        bool isMove = (operation == ClipboardOperation::Cut);
        executeBatch(std::move(paths), isMove, targetFolder, isMove ? std::move(onCutComplete) : nullptr);
    }

    void AsyncFileOperations::dropAsync(std::vector<std::string> paths, bool isMove,
                                        const std::string& targetFolder)
    {
        executeBatch(std::move(paths), isMove, targetFolder, nullptr);
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
