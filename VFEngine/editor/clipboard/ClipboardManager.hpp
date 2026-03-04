#pragma once
#include "../windows/contentbrowser/ContentBrowserTypes.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <atomic>

namespace windows
{
    enum class ClipboardOperation
    {
        None,
        Cut,
        Copy
    };

    struct ClipboardItem
    {
        std::string path;
        bool isDirectory = false;
        AssetType assetType = AssetType::Other;
    };


    class ClipboardManager
    {
    private:
        std::vector<ClipboardItem> items;
        std::unordered_set<std::string> cutPathsSet;
        ClipboardOperation operation = ClipboardOperation::None;
        mutable std::atomic<bool> pendingClear{false};

    public:
        static ClipboardManager& instance();

        ClipboardManager(const ClipboardManager&) = delete;
        ClipboardManager& operator=(const ClipboardManager&) = delete;


        void cut(const std::vector<ClipboardItem>& items);

        void copy(const std::vector<ClipboardItem>& items);

        void clear();

        void paste(const std::string& targetFolder);

        bool hasItems() const;

        bool isCut() const;

        const std::unordered_set<std::string>& getCutPaths() const;

    private:
        ClipboardManager() = default;
        ~ClipboardManager() = default;

        void updateCutPathsSet();
        void drainPendingClear();
    };
}
