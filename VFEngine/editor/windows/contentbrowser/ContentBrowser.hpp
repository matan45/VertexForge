#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "ContentBrowserTypes.hpp"
#include "AssetGridRenderer.hpp"
#include "AssetThumbnailCache.hpp"
#include "ContentBrowserModals.hpp"
#include "PreviewWindowManager.hpp"
#include "BookmarkManager.hpp"
#include "events/EventTypes.hpp"

#include <string>
#include <filesystem>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <chrono>

namespace fs = std::filesystem;

namespace windows
{
    struct ClipboardItem;

    class ContentBrowser : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::vector<Asset> assets;
        fs::path currentPath;

        fs::path selectedFile;
        AssetType selectedType = AssetType::Other;
        AssetFilter filter;

        // Search-query resolution (type:/ext:/guid:/ref: tokens), recomputed
        // only when the query string changes.
        ResolvedAssetFilter resolvedFilter;
        std::string lastResolvedQuery;

        // Project-wide search (globe toggle): results come from the asset
        // database, no directory walk and no per-file stat.
        bool searchProjectWide = false;
        std::vector<Asset> projectResults;
        std::string pendingProjectQuery;
        std::chrono::steady_clock::time_point projectQueryEditTime{};
        bool projectResultsPending = false;
        std::atomic<bool> projectResultsStale{true};

        // Multi-selection state
        std::unordered_set<std::string> selectedPaths;
        int lastSelectedIndex = -1;

        bool showFileWindow = false;
        bool importLocationSet = false;

        // Path bar state
        std::string pathEditBuffer;
        bool isEditingPath = false;

        events::SubscriptionToken importStartedToken;
        events::SubscriptionToken importCompletedToken;
        events::SubscriptionToken assetSavedToken;
        events::SubscriptionToken fileMovedToken;
        events::SubscriptionToken fileDeletedToken;
        events::SubscriptionToken folderSelectedToken;
        events::SubscriptionToken projectLoadedToken;
        events::SubscriptionToken batchCompletedToken;
        events::SubscriptionToken openMeshPreviewToken;
        std::atomic<bool> pendingRefresh{false};
        std::atomic<bool> importInFlight{false};

        AssetThumbnailCache thumbnailCache;
        std::unique_ptr<AssetGridRenderer> gridRenderer;
        std::unique_ptr<ContentBrowserModals> modals;
        std::unique_ptr<PreviewWindowManager> previewManager;
        std::unique_ptr<BookmarkManager> bookmarkManager;
    public:
        explicit ContentBrowser();
        ~ContentBrowser() override;

        void draw() override;

    private:
        void loadDirectory(const fs::path& path);
        void navigateTo(const fs::path& path);

        void drawToolbar();
        void drawPathBar(float availableWidth);
        void drawContentPanel();
        void drawBookmarkPanel();
        void drawFilterPopup();
        void handleAssetClick(const AssetClickResult& clickResult);
        void handleProjectResultClick(const AssetClickResult& clickResult);
        void handleDoubleClick();
        void handleDragDrop();

        // Search helpers
        void updateResolvedFilter();
        void updateProjectSearchResults();
        bool isProjectSearchActive() const { return searchProjectWide && !filter.searchQuery.empty(); }

        // Keyboard shortcut handling
        void handleKeyboardShortcuts();

        // Multi-selection helpers
        void selectAsset(size_t index, bool ctrlHeld, bool shiftHeld);
        void clearSelection();
        std::vector<std::string> getSelectedPaths() const;
        void updateCutState();

        // Clipboard operations
        std::vector<ClipboardItem> buildClipboardItems() const;
        void performCut();
        void performCopy();
        bool performPaste();

        // Asset type detection. Cached per (path, size, mtime) so the header
        // reads for ambiguous .vf* extensions happen once per file lifetime,
        // not on every refresh notification.
        AssetType detectAssetType(const fs::directory_entry& entry,
                                  uint64_t fileSize, int64_t lastModified);

        struct CachedAssetType
        {
            uint64_t fileSize = 0;
            int64_t lastModified = 0;
            AssetType type = AssetType::Other;
        };
        std::unordered_map<std::string, CachedAssetType> typeCache;
    };
}
