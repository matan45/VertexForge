#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../dragdrop/DragDropManager.hpp"
#include "events/EventTypes.hpp"
#include <string>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace windows
{
    // Notification for when a folder is selected in the Folder Structure window
    struct FolderSelectedNotification : events::INotification
    {
        std::string folderPath;
        std::string_view getName() const override { return "FolderSelected"; }
    };

    class FolderStructureWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        explicit FolderStructureWindow();
        ~FolderStructureWindow() override;

        void draw() override;

        // Set the root folder to display
        void setRootPath(const std::string& path);

        // Get the current root path
        std::string getRootPath() const;

        // Set the currently selected folder (for highlighting)
        void setSelectedFolder(const std::string& path);

    private:
        void drawFolderTree(const fs::path& path, int depth = 0);
        void handleDragDrop(const fs::path& folderPath);
        bool isExpanded(const fs::path& path) const;
        void setExpanded(const fs::path& path, bool expanded);

        fs::path rootPath = "C:\\matan";
        fs::path selectedFolder;
        std::unordered_set<std::string> expandedFolders;

        events::SubscriptionToken fileMovedToken;
        events::SubscriptionToken fileDeletedToken;
        events::SubscriptionToken folderCreatedToken;
    };
}
