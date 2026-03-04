#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventTypes.hpp"
#include <string>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace windows
{
    struct FolderSelectedNotification : events::INotification
    {
        std::string folderPath;
        std::string_view getName() const override { return "FolderSelected"; }
    };

    class FolderStructureWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        fs::path rootPath;
        fs::path selectedFolder;
        std::unordered_set<std::string> expandedFolders;

        events::SubscriptionToken folderCreatedToken;
        events::SubscriptionToken projectLoadedToken;

    public:
        explicit FolderStructureWindow();
        ~FolderStructureWindow() override;

        void draw() override;

    private:
        void drawFolderTree(const fs::path& path, int depth = 0);
        void handleDragDrop(const fs::path& folderPath);
        bool isExpanded(const fs::path& path) const;
        void setExpanded(const fs::path& path, bool expanded);
        void updateRootPath(const std::string& workingDirectory);
    };
}
