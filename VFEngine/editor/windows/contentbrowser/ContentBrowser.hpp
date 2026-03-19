#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "ContentBrowserTypes.hpp"
#include "AssetGridRenderer.hpp"
#include "ContentBrowserModals.hpp"
#include "PreviewWindowManager.hpp"
#include "events/EventTypes.hpp"

#include <string>
#include <filesystem>
#include <memory>
#include <vector>
#include <unordered_set>
#include <atomic>

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
        std::string searchQuery;

        // Multi-selection state
        std::unordered_set<std::string> selectedPaths;
        int lastSelectedIndex = -1;

        bool showFileWindow = false;
        bool importLocationSet = false;

        // Path bar state
        std::string pathEditBuffer;
        bool isEditingPath = false;

        events::SubscriptionToken importCompletedToken;
        events::SubscriptionToken assetSavedToken;
        events::SubscriptionToken fileMovedToken;
        events::SubscriptionToken fileDeletedToken;
        events::SubscriptionToken folderSelectedToken;
        events::SubscriptionToken projectLoadedToken;
        events::SubscriptionToken batchCompletedToken;
        std::atomic<bool> pendingRefresh{false};

        std::unique_ptr<AssetGridRenderer> gridRenderer;
        std::unique_ptr<ContentBrowserModals> modals;
        std::unique_ptr<PreviewWindowManager> previewManager;
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
        void handleAssetClick(const AssetClickResult& clickResult);
        void handleDoubleClick();
        void handleDragDrop();

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

        // Asset type detection
        static AssetType detectAssetType(const fs::directory_entry& entry);
    };
}
