#include "ContentBrowser.hpp"
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/WaterEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "../../dragdrop/DragDropManager.hpp"
#include "Import.hpp"
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <windows.h>
#include <shellapi.h>

namespace windows
{
    void ContentBrowser::drawContentPanel()
    {
        if (ImGui::Begin("Content Folder"))
        {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                handleKeyboardShortcuts();
            }

            drawToolbar();
            ImGui::Separator();
            modals->drawContextMenu(selectedFile);

            if (showFileWindow)
            {
                ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

                std::string windowTitle = "File: " + StringUtil::wstringToUtf8(selectedFile.filename().wstring());
                if (ImGui::Begin(windowTitle.c_str(), &showFileWindow))
                {
                    ImGui::Text("File Name: %s", StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());
                    ImGui::Text("File Path: %s", StringUtil::wstringToUtf8(selectedFile.wstring()).c_str());
                    ImGui::Separator();

                    if (previewManager->openPreview(selectedFile, selectedType))
                    {
                        showFileWindow = false;
                    }
                }
                ImGui::End();
            }

            for (auto& asset : assets)
            {
                asset.isSelected = selectedPaths.find(asset.path) != selectedPaths.end();
            }

            AssetClickResult clickResult = gridRenderer->draw(assets, searchQuery);
            handleAssetClick(clickResult);

            ImGui::Columns(1);
            handleDragDrop();
        }

        ImGui::End();
    }

    void ContentBrowser::handleAssetClick(const AssetClickResult& clickResult)
    {
        if (!clickResult.wasClicked && clickResult.pendingNavigation.empty())
            return;

        if (clickResult.wasClicked)
        {
            selectedFile = clickResult.clickedPath;
            selectedType = clickResult.clickedType;

            int clickedIndex = -1;
            for (size_t i = 0; i < assets.size(); ++i)
            {
                if (assets[i].path == StringUtil::wstringToUtf8(clickResult.clickedPath.wstring()))
                {
                    clickedIndex = static_cast<int>(i);
                    break;
                }
            }

            if (clickedIndex >= 0)
            {
                bool ctrlHeld = ImGui::IsKeyDown(ImGuiMod_Ctrl);
                bool shiftHeld = ImGui::IsKeyDown(ImGuiMod_Shift);
                selectAsset(static_cast<size_t>(clickedIndex), ctrlHeld, shiftHeld);

                for (auto& asset : assets)
                {
                    asset.isSelected = selectedPaths.find(asset.path) != selectedPaths.end();
                }
            }

            if (clickResult.wasDoubleClicked)
            {
                handleDoubleClick();
            }
        }

        if (!clickResult.pendingNavigation.empty())
        {
            navigateTo(clickResult.pendingNavigation);
        }
    }

    void ContentBrowser::handleDragDrop()
    {
        ImVec2 dropZoneStart = ImGui::GetCursorScreenPos();
        ImVec2 contentSize = ImGui::GetWindowSize();
        ImRect dropRect(dropZoneStart, ImVec2(dropZoneStart.x + contentSize.x, dropZoneStart.y + contentSize.y));

        if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("ContentFolderDropZone")))
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_ENTITY"))
            {
                services::EntityHandle entity = *(services::EntityHandle*)payload->Data;
                modals->triggerSavePrefabModal(entity);
            }

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
            {
                bool isValid = DragDropManager::instance().isValidDropTarget(currentPath.string());
                if (isValid)
                {
                    DragDropManager::instance().acceptDrop(currentPath.string());
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void ContentBrowser::drawToolbar()
    {
        if (ImGui::Button(ICON_FA_ARROW_LEFT))
        {
            auto parentPath = currentPath.parent_path();
            navigateTo(parentPath);
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_COPY "##CopyPath"))
        {
            ImGui::SetClipboardText(StringUtil::wstringToUtf8(currentPath.wstring()).c_str());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy path to clipboard");

        ImGui::SameLine();

        float availableWidth = ImGui::GetContentRegionAvail().x - 300.0f;
        if (availableWidth < 200.0f) availableWidth = 200.0f;

        drawPathBar(availableWidth);

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FILE_IMPORT " Import"))
        {
            events::EventDispatcher::instance().publish(events::application::OpenImportDialogNotification{});
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Import assets");

        ImGui::SameLine();

        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        char searchBuffer[256];
        std::strncpy(searchBuffer, searchQuery.c_str(), sizeof(searchBuffer) - 1);
        searchBuffer[sizeof(searchBuffer) - 1] = '\0';
        if (ImGui::InputText("##Search", searchBuffer, sizeof(searchBuffer)))
        {
            searchQuery = std::string(searchBuffer);
        }
    }

    void ContentBrowser::drawPathBar(float availableWidth)
    {
        if (!isEditingPath)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

            std::string pathStr = StringUtil::wstringToUtf8(currentPath.wstring());
            ImGui::SetNextItemWidth(availableWidth);
            if (ImGui::Button(pathStr.c_str(), ImVec2(availableWidth, 0)))
            {
                isEditingPath = true;
                pathEditBuffer = pathStr;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to edit path");

            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::SetNextItemWidth(availableWidth);

            if (ImGui::IsWindowAppearing() || pathEditBuffer.empty())
            {
                pathEditBuffer = StringUtil::wstringToUtf8(currentPath.wstring());
            }

            char pathBuffer[1024];
            std::strncpy(pathBuffer, pathEditBuffer.c_str(), sizeof(pathBuffer) - 1);
            pathBuffer[sizeof(pathBuffer) - 1] = '\0';

            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##PathEdit", pathBuffer, sizeof(pathBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                fs::path newPath(pathBuffer);
                if (fs::exists(newPath) && fs::is_directory(newPath))
                {
                    navigateTo(newPath);
                }
                isEditingPath = false;
            }
            else
            {
                pathEditBuffer = pathBuffer;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()))
            {
                isEditingPath = false;
            }
        }
    }
}
