#include "ContentBrowser.hpp"
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "../../dragdrop/DragDropManager.hpp"
#include "Import.hpp"
#include "print/Log.hpp"
#include <IconsFontAwesome6.h>
#include <exception>
#include <imgui_internal.h>
#include <windows.h>
#include <shellapi.h>

namespace windows
{
    void ContentBrowser::drawContentPanel()
    {
        if (ImGui::Begin("Content Folder"))
        {
            drawBookmarkPanel();

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                handleKeyboardShortcuts();
            }

            drawToolbar();
            ImGui::Separator();

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

            updateResolvedFilter();

            bool projectMode = isProjectSearchActive();
            if (projectMode)
                updateProjectSearchResults();

            std::vector<Asset>& visibleAssets = projectMode ? projectResults : assets;
            for (auto& asset : visibleAssets)
            {
                asset.isSelected = selectedPaths.find(asset.path) != selectedPaths.end();
            }

            // Project-wide results carry no file stats (no disk walk), so only
            // the name sort is meaningful there.
            AssetFilter effectiveFilter = filter;
            if (projectMode)
                effectiveFilter.sortBy = SortField::Name;

            AssetClickResult clickResult = gridRenderer->draw(visibleAssets, effectiveFilter, resolvedFilter, thumbnailCache);
            if (projectMode)
                handleProjectResultClick(clickResult);
            else
                handleAssetClick(clickResult);

            // Click empty space in the grid to cancel the current selection.
            if (!clickResult.wasClicked
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && ImGui::IsWindowHovered()
                && !ImGui::IsAnyItemHovered())
            {
                selectedFile.clear();
                selectedType = AssetType::Other;
                clearSelection(); // isSelected is rebuilt from selectedPaths each frame
            }

            const std::string selectedPath = StringUtil::wstringToUtf8(selectedFile.wstring());
            const Asset* selectedAsset = nullptr;
            for (const auto& asset : visibleAssets)
            {
                if (asset.path == selectedPath)
                {
                    selectedAsset = &asset;
                    break;
                }
            }
            modals->drawContextMenu(selectedAsset);

            ImGui::Columns(1);
            handleDragDrop();
        }

        ImGui::End();
    }

    void ContentBrowser::handleAssetClick(const AssetClickResult& clickResult)
    {
        if (!clickResult.wasClicked && !clickResult.wasRightClicked && clickResult.pendingNavigation.empty())
            return;

        if (clickResult.wasClicked || clickResult.wasRightClicked)
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
                if (clickResult.wasRightClicked)
                {
                    const std::string& clickedPath = assets[static_cast<size_t>(clickedIndex)].path;
                    if (selectedPaths.find(clickedPath) == selectedPaths.end())
                    {
                        selectedPaths.clear();
                        selectedPaths.insert(clickedPath);
                    }
                    lastSelectedIndex = clickedIndex;
                }
                else
                {
                    bool ctrlHeld = ImGui::IsKeyDown(ImGuiMod_Ctrl);
                    bool shiftHeld = ImGui::IsKeyDown(ImGuiMod_Shift);
                    selectAsset(static_cast<size_t>(clickedIndex), ctrlHeld, shiftHeld);
                }

                for (auto& asset : assets)
                {
                    asset.isSelected = selectedPaths.find(asset.path) != selectedPaths.end();
                }
            }

            if (clickResult.wasClicked && clickResult.wasDoubleClicked)
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
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 regionMin = ImGui::GetWindowContentRegionMin();
        ImVec2 regionMax = ImGui::GetWindowContentRegionMax();
        ImRect dropRect(
            ImVec2(windowPos.x + regionMin.x, windowPos.y + regionMin.y),
            ImVec2(windowPos.x + regionMax.x, windowPos.y + regionMax.y)
        );

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

        // Reserve space for: Import + regenerate metadata + search/filter/bookmark controls.
        float reservedRight = 550.0f;
        float availableWidth = ImGui::GetContentRegionAvail().x - reservedRight;
        if (availableWidth < 100.0f) availableWidth = 100.0f;

        drawPathBar(availableWidth);

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FILE_IMPORT " Import"))
        {
            events::EventDispatcher::instance().publish(events::application::OpenImportDialogNotification{});
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Import assets");

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FILE_MEDICAL "##RegenMeta"))
        {
            try
            {
                const auto result = events::EventDispatcher::instance().execute(
                    events::assetdb::RegenerateMissingMetadataCommand{});

                std::string summary = "Scanned " + std::to_string(result.assetsScanned) +
                    " asset(s); regenerated " + std::to_string(result.metaFilesCreated) +
                    " .vfmeta file(s).";
                summary += result.failures.empty()
                    ? "\nNo failures."
                    : "\n" + std::to_string(result.failures.size()) + " failure(s):";

                modals->showResult("Regenerate Metadata", summary, result.failures);
                loadDirectory(currentPath);
            }
            catch (const std::exception& e)
            {
                vfLogError("Regenerate metadata failed: {}", e.what());
                modals->showError("Regenerate Metadata", e.what());
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Regenerate missing .vfmeta files");

        ImGui::SameLine();

        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        char searchBuffer[256];
        std::strncpy(searchBuffer, filter.searchQuery.c_str(), sizeof(searchBuffer) - 1);
        searchBuffer[sizeof(searchBuffer) - 1] = '\0';
        if (ImGui::InputText("##Search", searchBuffer, sizeof(searchBuffer)))
        {
            filter.searchQuery = std::string(searchBuffer);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Plain words match names; type: ext: guid: ref: add filters");

        ImGui::SameLine();
        bool globeActive = searchProjectWide;
        if (globeActive)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.46f, 0.78f, 1.0f));
        if (ImGui::Button(ICON_FA_GLOBE "##ProjectSearch"))
        {
            searchProjectWide = !searchProjectWide;
            projectResultsStale.store(true);
        }
        if (globeActive)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Search the entire project (asset database)");

        ImGui::SameLine();
        if (ImGui::Button("Filter"))
            ImGui::OpenPopup("AssetFilterPopup");
        drawFilterPopup();

        ImGui::SameLine();
        if (bookmarkManager && ImGui::Button(bookmarkManager->isBookmarked(currentPath.string()) ? "Unbookmark" : "Bookmark"))
        {
            if (bookmarkManager->isBookmarked(currentPath.string()))
            {
                auto& bm = bookmarkManager->getBookmarks();
                for (size_t i = 0; i < bm.size(); ++i)
                {
                    if (bm[i].path == currentPath.string())
                    {
                        bookmarkManager->removeBookmark(i);
                        break;
                    }
                }
            }
            else
            {
                bookmarkManager->addBookmark(currentPath.string());
            }
        }
    }

    void ContentBrowser::drawFilterPopup()
    {
        if (ImGui::BeginPopup("AssetFilterPopup"))
        {
            const char* currentLabel = filter.typeFilter ? assetTypeInfo(*filter.typeFilter).label : "All";
            if (ImGui::BeginCombo("Type", currentLabel))
            {
                if (ImGui::Selectable("All", !filter.typeFilter.has_value()))
                    filter.typeFilter.reset();
                for (const auto& info : assetTypeTable())
                {
                    if (ImGui::Selectable(info.label, filter.typeFilter == info.type))
                        filter.typeFilter = info.type;
                }
                ImGui::EndCombo();
            }

            const char* sortNames[] = {"Name", "Date", "Size", "Type"};
            int sortIdx = static_cast<int>(filter.sortBy);
            if (ImGui::Combo("Sort By", &sortIdx, sortNames, 4))
                filter.sortBy = static_cast<SortField>(sortIdx);

            ImGui::Checkbox("Ascending", &filter.sortAscending);

            ImGui::EndPopup();
        }
    }

    void ContentBrowser::drawBookmarkPanel()
    {
        if (!bookmarkManager) return;
        const auto& bookmarks = bookmarkManager->getBookmarks();
        if (bookmarks.empty()) return;

        ImGui::Text("Bookmarks");
        ImGui::Separator();
        for (size_t i = 0; i < bookmarks.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(bookmarks[i].name.c_str()))
                navigateTo(bookmarks[i].path);
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::Spacing();
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
