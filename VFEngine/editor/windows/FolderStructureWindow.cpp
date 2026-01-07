#include "FolderStructureWindow.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "print/EditorLogger.hpp"
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

namespace windows
{
    FolderStructureWindow::FolderStructureWindow()
    {
        if (fs::exists(rootPath) && fs::is_directory(rootPath))
        {
            expandedFolders.insert(rootPath.string());
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Subscribe to file operation notifications to refresh view
        fileMovedToken = dispatcher.subscribe<events::fileops::FileMovedNotification>(
            [this](const events::fileops::FileMovedNotification&) {
                // View will refresh automatically on next draw
            });

        fileDeletedToken = dispatcher.subscribe<events::fileops::FileDeletedNotification>(
            [this](const events::fileops::FileDeletedNotification&) {
                // View will refresh automatically on next draw
            });

        folderCreatedToken = dispatcher.subscribe<events::fileops::FolderCreatedNotification>(
            [this](const events::fileops::FolderCreatedNotification& notification) {
                // Auto-expand parent of new folder
                fs::path newFolder(notification.path);
                fs::path parent = newFolder.parent_path();
                setExpanded(parent, true);
            });
    }

    FolderStructureWindow::~FolderStructureWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (fileMovedToken.isValid())
        {
            dispatcher.unsubscribe(fileMovedToken);
        }
        if (fileDeletedToken.isValid())
        {
            dispatcher.unsubscribe(fileDeletedToken);
        }
        if (folderCreatedToken.isValid())
        {
            dispatcher.unsubscribe(folderCreatedToken);
        }
    }

    void FolderStructureWindow::draw()
    {
        if (ImGui::Begin("Folder Structure", nullptr, ImGuiWindowFlags_NoCollapse))
        {
            // Root folder header
            ImGui::Text(ICON_FA_FOLDER_OPEN " %s", StringUtil::wstringToUtf8(rootPath.filename().wstring()).c_str());
            ImGui::Separator();

            // Draw folder tree
            if (fs::exists(rootPath) && fs::is_directory(rootPath))
            {
                drawFolderTree(rootPath);
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Root path not found");
            }
        }
        ImGui::End();
    }

    void FolderStructureWindow::drawFolderTree(const fs::path& path, int depth)
    {
        std::error_code ec;

        for (auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec))
        {
            if (!entry.is_directory())
            {
                continue;
            }

            std::string folderName = StringUtil::wstringToUtf8(entry.path().filename().wstring());
            std::string folderPathStr = entry.path().string();
            bool isSelected = (selectedFolder == entry.path());
            bool hasChildren = false;

            // Check if folder has subfolders
            std::error_code childEc;
            for (auto& child : fs::directory_iterator(entry.path(), fs::directory_options::skip_permission_denied, childEc))
            {
                if (child.is_directory())
                {
                    hasChildren = true;
                    break;
                }
            }

            // Tree node flags
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (isSelected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (isExpanded(entry.path()))
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }

            // Push ID for unique node identification
            ImGui::PushID(folderPathStr.c_str());

            // Folder icon
            ImGui::Text(ICON_FA_FOLDER);
            ImGui::SameLine();

            // Tree node
            bool nodeOpen = ImGui::TreeNodeEx(folderName.c_str(), flags);

            // Track expanded state
            if (nodeOpen != isExpanded(entry.path()))
            {
                setExpanded(entry.path(), nodeOpen);
            }

            // Handle selection
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                selectedFolder = entry.path();
            }

            // Double-click to navigate
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                // Publish folder selection notification
                FolderSelectedNotification notification;
                notification.folderPath = folderPathStr;
                events::EventDispatcher::instance().publish(notification);
            }

            // Drag source for folders
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                DragDropManager::instance().beginDrag({folderPathStr});
                DragDropManager::instance().setDragPayload();
                DragDropManager::instance().drawDragPreview();

                ImGui::EndDragDropSource();
            }

            // Drop target for folders
            handleDragDrop(entry.path());

            ImGui::PopID();

            // Recurse into children
            if (nodeOpen)
            {
                drawFolderTree(entry.path(), depth + 1);
                ImGui::TreePop();
            }
        }
    }

    void FolderStructureWindow::handleDragDrop(const fs::path& folderPath)
    {
        if (ImGui::BeginDragDropTarget())
        {
            // Visual feedback
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImRect dropRect(min, max);

            bool isValid = DragDropManager::instance().isValidDropTarget(folderPath.string());
            DragDropManager::drawDropTargetHighlight(dropRect, isValid);

            // Accept the drop
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
            {
                if (isValid)
                {
                    auto result = DragDropManager::instance().acceptDrop(folderPath.string());
                    if (!result.success)
                    {
                        vfLogError("Drop failed: {}", result.errorMessage);
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }
    }

    bool FolderStructureWindow::isExpanded(const fs::path& path) const
    {
        return expandedFolders.find(path.string()) != expandedFolders.end();
    }

    void FolderStructureWindow::setExpanded(const fs::path& path, bool expanded)
    {
        if (expanded)
        {
            expandedFolders.insert(path.string());
        }
        else
        {
            expandedFolders.erase(path.string());
        }
    }

    void FolderStructureWindow::setRootPath(const std::string& path)
    {
        rootPath = path;
        expandedFolders.clear();
        expandedFolders.insert(path);
    }

    std::string FolderStructureWindow::getRootPath() const
    {
        return rootPath.string();
    }

    void FolderStructureWindow::setSelectedFolder(const std::string& path)
    {
        selectedFolder = path;
    }
}
