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

namespace fs = std::filesystem;

namespace windows
{
    class ContentBrowser : public controllers::imguiHandler::ImguiWindow
    {
    public:
        ContentBrowser();
        ~ContentBrowser() override;

        void draw() override;

    private:
        void loadDirectory(const fs::path& path);
        void navigateTo(const fs::path& path);

        void drawToolbar();
        void drawFolderStructurePanel();
        void drawFolderTree(const fs::path& path);
        void drawContentPanel();

        std::vector<Asset> assets;
        fs::path currentPath = "C:\\matan";

        fs::path selectedFile;
        AssetType selectedType = AssetType::Other;
        std::string searchQuery;

        bool showFileWindow = false;
        bool importLocationSet = false;

        events::SubscriptionToken importCompletedToken;

        std::unique_ptr<AssetGridRenderer> gridRenderer;
        std::unique_ptr<ContentBrowserModals> modals;
        std::unique_ptr<PreviewWindowManager> previewManager;
    };
}
