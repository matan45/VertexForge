#include "ClipboardManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
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
        cutPathsSet.clear();  // No cut paths when copying
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

        // Collect all source paths
        std::vector<std::string> sourcePaths;
        for (const auto& item : items)
        {
            sourcePaths.push_back(item.path);
        }

        if (operation == ClipboardOperation::Cut)
        {
            // Move files
            events::fileops::MoveFilesCommand cmd;
            cmd.sourcePaths = sourcePaths;
            cmd.destFolder = targetFolder;

            result = dispatcher.execute(cmd);

            // Clear clipboard after successful cut-paste
            if (result.success)
            {
                clear();
            }
        }
        else if (operation == ClipboardOperation::Copy)
        {
            // Copy files
            events::fileops::CopyFilesCommand cmd;
            cmd.sourcePaths = sourcePaths;
            cmd.destFolder = targetFolder;

            result = dispatcher.execute(cmd);
            // Don't clear clipboard after copy - allow multiple pastes
        }
        else
        {
            result.success = false;
            result.errorMessage = "Invalid clipboard operation";
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

    bool ClipboardManager::isCopy() const
    {
        return operation == ClipboardOperation::Copy;
    }

    const std::vector<ClipboardItem>& ClipboardManager::getItems() const
    {
        return items;
    }

    bool ClipboardManager::isPathCut(const std::string& path) const
    {
        return cutPathsSet.find(path) != cutPathsSet.end();
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
