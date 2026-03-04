#include "FolderStructureWindow.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "events/ProjectEvents.hpp"
#include "../dragdrop/DragDropManager.hpp"
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

namespace windows
{
    FolderStructureWindow::FolderStructureWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (projectOpt && !projectOpt->workingDirectory.empty())
        {
            rootPath = projectOpt->workingDirectory;
        }
        else
        {
            rootPath = "C:\\";
        }

        if (fs::exists(rootPath) && fs::is_directory(rootPath))
        {
            expandedFolders.insert(rootPath.string());
        }

        projectLoadedToken = dispatcher.subscribe<events::project::ProjectLoadedNotification>(
            [this](const events::project::ProjectLoadedNotification& notification)
            {
                updateRootPath(notification.project.workingDirectory);
            });

        folderCreatedToken = dispatcher.subscribe<events::fileops::FolderCreatedNotification>(
            [this](const events::fileops::FolderCreatedNotification& notification)
            {
                fs::path newFolder(notification.path);
                fs::path parent = newFolder.parent_path();
                setExpanded(parent, true);
            });
    }

    FolderStructureWindow::~FolderStructureWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (projectLoadedToken.isValid())
        {
            dispatcher.unsubscribe(projectLoadedToken);
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
            if (fs::exists(rootPath) && fs::is_directory(rootPath))
            {
                ImGui::Text(ICON_FA_FOLDER_OPEN " %s", StringUtil::wstringToUtf8(rootPath.filename().wstring()).c_str());
                ImGui::Separator();
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

            std::error_code childEc;
            for (auto& child : fs::directory_iterator(entry.path(), fs::directory_options::skip_permission_denied,
                                                      childEc))
            {
                if (child.is_directory())
                {
                    hasChildren = true;
                    break;
                }
            }

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

            ImGui::PushID(folderPathStr.c_str());

            ImGui::Text(ICON_FA_FOLDER);
            ImGui::SameLine();

            bool nodeOpen = ImGui::TreeNodeEx(folderName.c_str(), flags);

            if (nodeOpen != isExpanded(entry.path()))
            {
                setExpanded(entry.path(), nodeOpen);
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                selectedFolder = entry.path();
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                FolderSelectedNotification notification;
                notification.folderPath = folderPathStr;
                events::EventDispatcher::instance().publish(notification);
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                DragDropManager::instance().beginDrag({folderPathStr});
                DragDropManager::instance().setDragPayload();
                DragDropManager::instance().drawDragPreview();

                ImGui::EndDragDropSource();
            }

            handleDragDrop(entry.path());

            ImGui::PopID();

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
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImRect dropRect(min, max);

            bool isValid = DragDropManager::instance().isValidDropTarget(folderPath.string());
            DragDropManager::drawDropTargetHighlight(dropRect, isValid);

            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER);
            if (payload && isValid)
            {
                DragDropManager::instance().acceptDrop(folderPath.string());
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

    void FolderStructureWindow::updateRootPath(const std::string& workingDirectory)
    {
        if (!workingDirectory.empty() && fs::exists(workingDirectory) && fs::is_directory(workingDirectory))
        {
            rootPath = workingDirectory;
            expandedFolders.clear();
            expandedFolders.insert(rootPath.string());
            selectedFolder.clear();
        }
    }
}
