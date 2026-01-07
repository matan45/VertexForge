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

namespace fs = std::filesystem;

namespace windows
{
    class ContentBrowser : public controllers::imguiHandler::ImguiWindow
    {
    public:
        explicit ContentBrowser();
        ~ContentBrowser() override;

        void draw() override;

    private:
        void loadDirectory(const fs::path& path);
        void navigateTo(const fs::path& path);

        void drawToolbar();
        void drawContentPanel();

        // Keyboard shortcut handling
        void handleKeyboardShortcuts();

        // Multi-selection helpers
        void selectAsset(size_t index, bool ctrlHeld, bool shiftHeld);
        void clearSelection();
        std::vector<std::string> getSelectedPaths() const;
        void updateCutState();

        // Clipboard operations
        void performCut();
        void performCopy();
        bool performPaste();

        std::vector<Asset> assets;
        fs::path currentPath = "C:\\matan";//todo

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
        events::SubscriptionToken fileMovedToken;
        events::SubscriptionToken folderSelectedToken;

        std::unique_ptr<AssetGridRenderer> gridRenderer;
        std::unique_ptr<ContentBrowserModals> modals;
        std::unique_ptr<PreviewWindowManager> previewManager;
    };
}
