#include "ClipboardManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "events/UndoRedoEvents.hpp"
#include "print/EditorLogger.hpp"

namespace windows
{
    ClipboardManager& ClipboardManager::instance()
    {
        static ClipboardManager instance;
        return instance;
    }

    void ClipboardManager::cut(const std::vector<ClipboardItem>& itemsToCut)
    {
        items = itemsToCut;
        operation = ClipboardOperation::Cut;
        updateCutPathsSet();
        vfLogInfo("Cut {} items to clipboard", items.size());
    }

    void ClipboardManager::copy(const std::vector<ClipboardItem>& itemsToCopy)
    {
        items = itemsToCopy;
        operation = ClipboardOperation::Copy;
        cutPathsSet.clear();
        vfLogInfo("Copied {} items to clipboard", items.size());
    }

    void ClipboardManager::clear()
    {
        items.clear();
        cutPathsSet.clear();
        operation = ClipboardOperation::None;
    }

    services::FileOperationResult ClipboardManager::paste(const std::string& targetFolder)
    {
        services::FileOperationResult result;

        if (!hasItems())
        {
            result.success = false;
            result.errorMessage = "Clipboard is empty";
            return result;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        if (operation != ClipboardOperation::Cut && operation != ClipboardOperation::Copy)
        {
            result.success = false;
            result.errorMessage = "Invalid clipboard operation";
            return result;
        }

        // Begin batch for grouped undo
        if (items.size() > 1)
        {
            events::undoredo::BeginBatchCommand batchCmd;
            batchCmd.description = (operation == ClipboardOperation::Cut ? "Move " : "Copy ") +
                                   std::to_string(items.size()) + " items";
            dispatcher.execute(batchCmd);
        }

        result.success = true;
        for (const auto& item : items)
        {
            services::FileOperationResult opResult;
            if (operation == ClipboardOperation::Cut)
            {
                events::fileops::MoveFileCommand cmd;
                cmd.sourcePath = item.path;
                cmd.destPath = targetFolder;
                opResult = dispatcher.execute(cmd);
            }
            else
            {
                events::fileops::CopyFileCommand cmd;
                cmd.sourcePath = item.path;
                cmd.destPath = targetFolder;
                opResult = dispatcher.execute(cmd);
            }

            if (!opResult.success)
            {
                result.success = false;
                result.errorMessage += opResult.errorMessage + "\n";
            }
            result.updatedReferences.insert(result.updatedReferences.end(),
                opResult.updatedReferences.begin(), opResult.updatedReferences.end());
        }

        // End batch
        if (items.size() > 1)
        {
            dispatcher.execute(events::undoredo::EndBatchCommand{});
        }

        if (operation == ClipboardOperation::Cut && result.success)
        {
            clear();
        }

        return result;
    }

    bool ClipboardManager::hasItems() const
    {
        return !items.empty();
    }

    bool ClipboardManager::isCut() const
    {
        return operation == ClipboardOperation::Cut;
    }

    const std::unordered_set<std::string>& ClipboardManager::getCutPaths() const
    {
        return cutPathsSet;
    }

    void ClipboardManager::updateCutPathsSet()
    {
        cutPathsSet.clear();
        if (operation == ClipboardOperation::Cut)
        {
            for (const auto& item : items)
            {
                cutPathsSet.insert(item.path);
            }
        }
    }
}
