#include "ClipboardManager.hpp"
#include "../fileops/AsyncFileOperations.hpp"
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

    void ClipboardManager::paste(const std::string& targetFolder)
    {
        if (!hasItems() || AsyncFileOperations::isBusy())
        {
            return;
        }

        if (operation != ClipboardOperation::Cut && operation != ClipboardOperation::Copy)
        {
            return;
        }

        auto currentOp = operation;
        auto currentItems = items;

        AsyncFileOperations::pasteAsync(
            std::move(currentItems), currentOp, targetFolder,
            [this]() { clear(); }
        );
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
