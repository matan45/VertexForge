#include "ContentBrowser.hpp"
#include "FolderStructureWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ResourceEvents.hpp"
#include "events/FileOperationsEvents.hpp"
#include "events/UndoRedoEvents.hpp"
#include "../clipboard/ClipboardManager.hpp"
#include "../dragdrop/DragDropManager.hpp"
#include "Import.hpp"
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <windows.h>
#include <shellapi.h>

namespace windows
{
    ContentBrowser::ContentBrowser()
        : gridRenderer(std::make_unique<AssetGridRenderer>())
        , modals(std::make_unique<ContentBrowserModals>([this]() { loadDirectory(currentPath); }))
        , previewManager(std::make_unique<PreviewWindowManager>())
    {
        // Set up clipboard callbacks for context menu
        modals->setClipboardCallbacks(
            [this]() { performCut(); },
            [this]() { performCopy(); },
            [this]() { return performPaste(); },
            []() { return ClipboardManager::instance().hasItems(); }
        );

        if (fs::exists(currentPath) && fs::is_directory(currentPath)) {
            loadDirectory(currentPath);
        }

        auto& dispatcher = events::EventDispatcher::instance();

        importCompletedToken = dispatcher.subscribe<events::resource::ImportCompletedNotification>(
            [this](const events::resource::ImportCompletedNotification&) {
                if (fs::exists(currentPath) && fs::is_directory(currentPath)) {
                    loadDirectory(currentPath);
                }
            });

        // Refresh view when files are moved/deleted
        fileMovedToken = dispatcher.subscribe<events::fileops::FileMovedNotification>(
            [this](const events::fileops::FileMovedNotification&) {
                if (fs::exists(currentPath) && fs::is_directory(currentPath)) {
                    loadDirectory(currentPath);
                    clearSelection();
                }
            });

        // Navigate when folder is selected from Folder Structure window
        folderSelectedToken = dispatcher.subscribe<FolderSelectedNotification>(
            [this](const FolderSelectedNotification& notification) {
                navigateTo(notification.folderPath);
            });
    }

    ContentBrowser::~ContentBrowser()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (importCompletedToken.isValid()) {
            dispatcher.unsubscribe(importCompletedToken);
        }
        if (fileMovedToken.isValid()) {
            dispatcher.unsubscribe(fileMovedToken);
        }
        if (folderSelectedToken.isValid()) {
            dispatcher.unsubscribe(folderSelectedToken);
        }
    }

    void ContentBrowser::draw()
    {
        gridRenderer->ensureIconsLoaded();

        if (!importLocationSet) {
            controllers::Import::setLocation(currentPath.string());
            importLocationSet = true;
        }

        // Update cut state for visual feedback
        updateCutState();

        modals->processModals(currentPath, selectedFile);
        drawContentPanel();
    }

    void ContentBrowser::drawContentPanel()
    {
        if (ImGui::Begin("Content Folder"))
        {
            // Handle keyboard shortcuts when window is focused
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

            ImVec2 dropZoneStart = ImGui::GetCursorScreenPos();
            ImVec2 contentSize = ImGui::GetWindowSize();
            ImRect dropRect(dropZoneStart, ImVec2(dropZoneStart.x + contentSize.x, dropZoneStart.y + contentSize.y));

            // Update selection state in assets before drawing
            for (auto& asset : assets)
            {
                asset.isSelected = selectedPaths.find(asset.path) != selectedPaths.end();
            }

            AssetClickResult clickResult = gridRenderer->draw(assets, selectedFile, searchQuery);

            if (clickResult.wasClicked)
            {
                selectedFile = clickResult.clickedPath;
                selectedType = clickResult.clickedType;

                // Find the clicked asset index
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
                }

                if (clickResult.wasDoubleClicked)
                {
                    if (selectedType == AssetType::Script)
                    {
                        // Open .mt files with VS Code
                        std::string filePath = StringUtil::wstringToUtf8(selectedFile.wstring());
                        std::string args = "\"" + filePath + "\"";
                        ShellExecuteA(nullptr, "open", "code", args.c_str(), nullptr, SW_SHOWNORMAL);
                    }
                    else
                    {
                        showFileWindow = true;
                    }
                }
            }

            if (!clickResult.pendingNavigation.empty())
            {
                navigateTo(clickResult.pendingNavigation);
            }

            ImGui::Columns(1);

            // Handle drag-drop for scene entities (prefabs)
            if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("ContentFolderDropZone")))
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_ENTITY"))
                {
                    services::EntityHandle entity = *(services::EntityHandle*)payload->Data;
                    modals->triggerSavePrefabModal(entity);
                }

                // Handle content browser drag-drop
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
                {
                    bool isValid = DragDropManager::instance().isValidDropTarget(currentPath.string());
                    if (isValid)
                    {
                        auto result = DragDropManager::instance().acceptDrop(currentPath.string());
                        if (result.success)
                        {
                            loadDirectory(currentPath);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::End();
    }

    void ContentBrowser::drawToolbar()
    {
        if (ImGui::Button(ICON_FA_ARROW_LEFT))
        {
            auto parentPath = currentPath.parent_path();
            navigateTo(parentPath);
        }

        ImGui::SameLine();

        // Copy path button
        if (ImGui::Button(ICON_FA_COPY "##CopyPath"))
        {
            ImGui::SetClipboardText(StringUtil::wstringToUtf8(currentPath.wstring()).c_str());
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Copy path to clipboard");
        }

        ImGui::SameLine();

        // Editable path bar
        float availableWidth = ImGui::GetContentRegionAvail().x - 300.0f; // Reserve space for search
        if (availableWidth < 200.0f) availableWidth = 200.0f;

        if (!isEditingPath)
        {
            // Display mode - clickable text
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
            {
                ImGui::SetTooltip("Click to edit path");
            }

            ImGui::PopStyleColor(2);
        }
        else
        {
            // Edit mode - input text
            ImGui::SetNextItemWidth(availableWidth);

            // Auto-focus the input field when entering edit mode
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
                // Enter pressed - navigate to the path
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

            // Cancel editing on Escape or when clicking elsewhere
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()))
            {
                isEditingPath = false;
            }
        }

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

    void ContentBrowser::loadDirectory(const fs::path& path)
    {
        assets.clear();

        std::error_code ec;
        for (auto& entry : fs::directory_iterator(path, ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            try
            {
                // Skip problematic entries (reserved Windows names, etc.)
                std::string filename = entry.path().filename().string();
                if (filename.empty() || filename == "nul" || filename == "con" ||
                    filename == "prn" || filename == "aux" || filename == "." || filename == "..")
                {
                    continue;
                }

                using enum windows::AssetType;
                Asset asset;
                asset.path = StringUtil::wstringToUtf8(entry.path().wstring());
                asset.name = StringUtil::wstringToUtf8(entry.path().filename().wstring());

                std::error_code statusEc;
                bool isDir = entry.is_directory(statusEc);
                if (statusEc)
                {
                    // Can't determine file type, skip
                    continue;
                }

                if (isDir)
                {
                    asset.type = Other;
                }
                else
                {
                    std::string extension = entry.path().extension().string();
                    if (extension == ".vfMat")
                    {
                        asset.type = Material;
                    }
                    else if (extension == ".vfMatInstance")
                    {
                        asset.type = MaterialInstance;
                    }
                    else if (extension == ".vfPrefab")
                    {
                        asset.type = Prefab;
                    }
                    else if (extension == ".mt")
                    {
                        asset.type = Script;
                    }
                    else
                    {
                        // Only read header for VFEngine asset files
                        // These have custom binary format with file type in header
                        bool isVfAsset = (extension == ".vfImage" || extension == ".vfHdr" ||
                                          extension == ".vfMesh" || extension == ".vfAudio" ||
                                          extension == ".vfAnim" || extension == ".vfScene");

                        if (isVfAsset)
                        {
                            resource::FileType ext = resource::ResourceManager::readHeaderFile(entry);

                            if (ext == resource::FileType::TEXTURE)
                            {
                                asset.type = Texture;
                            }
                            else if (ext == resource::FileType::SCENE)
                            {
                                asset.type = Scene;
                            }
                            else if (ext == resource::FileType::HDR)
                            {
                                asset.type = HDR;
                            }
                            else if (ext == resource::FileType::MESH)
                            {
                                asset.type = Model;
                            }
                            else if (ext == resource::FileType::AUDIO)
                            {
                                asset.type = Audio;
                            }
                            else if (ext == resource::FileType::ANIMATION)
                            {
                                asset.type = Animation;
                            }
                            else
                            {
                                asset.type = Other;
                            }
                        }
                        else
                        {
                            asset.type = Other;
                        }
                    }
                }
                assets.push_back(asset);
            }
            catch (const std::exception&)
            {
                // Skip entries that cause exceptions
                continue;
            }
        }
    }

    void ContentBrowser::navigateTo(const fs::path& path)
    {
        if (fs::exists(path) && fs::is_directory(path))
        {
            currentPath = path;
            controllers::Import::setLocation(currentPath.string());
            loadDirectory(currentPath);
            selectedFile.clear();
            selectedType = AssetType::Other;
            clearSelection();
            isEditingPath = false;
        }
    }

    void ContentBrowser::handleKeyboardShortcuts()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (ImGui::IsKeyDown(ImGuiMod_Ctrl))
        {
            // Ctrl+C - Copy
            if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                performCopy();
            }

            // Ctrl+X - Cut
            if (ImGui::IsKeyPressed(ImGuiKey_X, false))
            {
                performCut();
            }

            // Ctrl+V - Paste
            if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            {
                performPaste();
            }

            // Ctrl+Z - Undo
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                dispatcher.execute(events::undoredo::UndoCommand{});
            }

            // Ctrl+Y - Redo
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            {
                dispatcher.execute(events::undoredo::RedoCommand{});
            }

            // Ctrl+A - Select All
            if (ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                for (const auto& asset : assets)
                {
                    selectedPaths.insert(asset.path);
                }
            }
        }

        // Escape - Cancel cut operation
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            auto& clipboard = ClipboardManager::instance();
            if (clipboard.isCut())
            {
                clipboard.clear();
                updateCutState();
            }
        }

        // Delete key
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            auto paths = getSelectedPaths();
            if (!paths.empty())
            {
                // Trigger delete confirmation through modals
                if (!selectedFile.empty())
                {
                    modals->triggerDeleteModal();
                }
            }
        }
    }

    void ContentBrowser::selectAsset(size_t index, bool ctrlHeld, bool shiftHeld)
    {
        if (index >= assets.size())
        {
            return;
        }

        const std::string& path = assets[index].path;

        if (shiftHeld && lastSelectedIndex >= 0)
        {
            // Range selection
            size_t start = std::min(static_cast<size_t>(lastSelectedIndex), index);
            size_t end = std::max(static_cast<size_t>(lastSelectedIndex), index);

            if (!ctrlHeld)
            {
                selectedPaths.clear();
            }

            for (size_t i = start; i <= end; ++i)
            {
                selectedPaths.insert(assets[i].path);
            }
        }
        else if (ctrlHeld)
        {
            // Toggle selection
            if (selectedPaths.find(path) != selectedPaths.end())
            {
                selectedPaths.erase(path);
            }
            else
            {
                selectedPaths.insert(path);
            }
        }
        else
        {
            // Single selection
            selectedPaths.clear();
            selectedPaths.insert(path);
        }

        lastSelectedIndex = static_cast<int>(index);
    }

    void ContentBrowser::clearSelection()
    {
        selectedPaths.clear();
        lastSelectedIndex = -1;
    }

    std::vector<std::string> ContentBrowser::getSelectedPaths() const
    {
        return std::vector<std::string>(selectedPaths.begin(), selectedPaths.end());
    }

    void ContentBrowser::updateCutState()
    {
        auto& clipboard = ClipboardManager::instance();
        const auto& cutPaths = clipboard.getCutPaths();

        for (auto& asset : assets)
        {
            asset.isCut = cutPaths.find(asset.path) != cutPaths.end();
        }
    }

    void ContentBrowser::performCut()
    {
        auto paths = getSelectedPaths();
        if (!paths.empty())
        {
            std::vector<ClipboardItem> items;
            for (const auto& path : paths)
            {
                ClipboardItem item;
                item.path = path;
                item.isDirectory = fs::is_directory(path);
                for (const auto& asset : assets)
                {
                    if (asset.path == path)
                    {
                        item.assetType = asset.type;
                        break;
                    }
                }
                items.push_back(item);
            }
            ClipboardManager::instance().cut(items);
        }
    }

    void ContentBrowser::performCopy()
    {
        auto paths = getSelectedPaths();
        if (!paths.empty())
        {
            std::vector<ClipboardItem> items;
            for (const auto& path : paths)
            {
                ClipboardItem item;
                item.path = path;
                item.isDirectory = fs::is_directory(path);
                for (const auto& asset : assets)
                {
                    if (asset.path == path)
                    {
                        item.assetType = asset.type;
                        break;
                    }
                }
                items.push_back(item);
            }
            ClipboardManager::instance().copy(items);
        }
    }

    bool ContentBrowser::performPaste()
    {
        auto& clipboard = ClipboardManager::instance();
        if (clipboard.hasItems())
        {
            auto result = clipboard.paste(currentPath.string());
            if (result.success)
            {
                loadDirectory(currentPath);
                return true;
            }
            else
            {
                modals->showError("Paste Failed", result.errorMessage, result.conflicts);
            }
        }
        return false;
    }
}
