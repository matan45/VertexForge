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

    void ClipboardManager::drainPendingClear()
    {
        if (pendingClear.exchange(false))
        {
            items.clear();
            cutPathsSet.clear();
            operation = ClipboardOperation::None;
        }
    }

    void ClipboardManager::cut(const std::vector<ClipboardItem>& itemsToCut)
    {
        drainPendingClear();
        items = itemsToCut;
        operation = ClipboardOperation::Cut;
        updateCutPathsSet();
        vfLogInfo("Cut {} items to clipboard", items.size());
    }

    void ClipboardManager::copy(const std::vector<ClipboardItem>& itemsToCopy)
    {
        drainPendingClear();
        items = itemsToCopy;
        operation = ClipboardOperation::Copy;
        cutPathsSet.clear();
        vfLogInfo("Copied {} items to clipboard", items.size());
    }

    void ClipboardManager::clear()
    {
        pendingClear.store(false);
        items.clear();
        cutPathsSet.clear();
        operation = ClipboardOperation::None;
    }

    void ClipboardManager::paste(const std::string& targetFolder)
    {
        drainPendingClear();

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
            [this]() { pendingClear.store(true); }
        );
    }

    bool ClipboardManager::hasItems() const
    {
        if (pendingClear.load()) return false;
        return !items.empty();
    }

    bool ClipboardManager::isCut() const
    {
        if (pendingClear.load()) return false;
        return operation == ClipboardOperation::Cut;
    }

    const std::unordered_set<std::string>& ClipboardManager::getCutPaths() const
    {
        static const std::unordered_set<std::string> empty;
        if (pendingClear.load()) return empty;
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
