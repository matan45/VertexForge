#pragma once
#include "../windows/ContentBrowserTypes.hpp"
#include "data/FileOperationsTypes.hpp"
#include <string>
#include <vector>
#include <unordered_set>

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

    // Singleton manager for clipboard operations in the Content Browser
    class ClipboardManager
    {
    public:
        static ClipboardManager& instance();

        // Prevent copying
        ClipboardManager(const ClipboardManager&) = delete;
        ClipboardManager& operator=(const ClipboardManager&) = delete;

        // ============================================
        // Clipboard Operations
        // ============================================

        // Cut selected items (will be moved on paste)
        void cut(const std::vector<ClipboardItem>& items);

        // Copy selected items (will be copied on paste)
        void copy(const std::vector<ClipboardItem>& items);

        // Clear the clipboard
        void clear();

        // Paste items to target folder
        // Uses FileOperationsService via events
        services::FileOperationResult paste(const std::string& targetFolder);

        // ============================================
        // State Queries
        // ============================================

        // Check if clipboard has items
        bool hasItems() const;

        // Check if current operation is cut
        bool isCut() const;

        // Check if current operation is copy
        bool isCopy() const;

        // Get clipboard items
        const std::vector<ClipboardItem>& getItems() const;

        // Check if a specific path is in the clipboard for cut
        // (used for visual dimming)
        bool isPathCut(const std::string& path) const;

        // Get all cut paths as a set for efficient lookup
        const std::unordered_set<std::string>& getCutPaths() const;

    private:
        ClipboardManager() = default;
        ~ClipboardManager() = default;

        std::vector<ClipboardItem> items;
        std::unordered_set<std::string> cutPathsSet;  // For O(1) lookup
        ClipboardOperation operation = ClipboardOperation::None;

        void updateCutPathsSet();
    };
}
