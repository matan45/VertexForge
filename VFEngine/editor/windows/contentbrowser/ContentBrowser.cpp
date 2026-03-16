#include "ContentBrowser.hpp"
#include "../scene/FolderStructureWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/project/FileOperationsEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/WaterEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "../../clipboard/ClipboardManager.hpp"
#include "../../dragdrop/DragDropManager.hpp"
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
        modals->setClipboardCallbacks(
            [this]() { performCut(); },
            [this]() { performCopy(); },
            [this]() { return performPaste(); },
            []() { return ClipboardManager::instance().hasItems(); }
        );

        auto& dispatcher = events::EventDispatcher::instance();

        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (projectOpt && !projectOpt->workingDirectory.empty())
        {
            currentPath = projectOpt->workingDirectory;
        }
        else
        {
            currentPath = "C:\\";
        }

        if (fs::exists(currentPath) && fs::is_directory(currentPath))
        {
            loadDirectory(currentPath);
        }

        projectLoadedToken = dispatcher.subscribe<events::project::ProjectLoadedNotification>(
            [this](const events::project::ProjectLoadedNotification& notification)
            {
                if (!notification.project.workingDirectory.empty())
                {
                    navigateTo(notification.project.workingDirectory);
                }
            });

        importCompletedToken = dispatcher.subscribe<events::resource::ImportCompletedNotification>(
            [this](const events::resource::ImportCompletedNotification&)
            {
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                {
                    loadDirectory(currentPath);
                }
            });

        assetSavedToken = dispatcher.subscribe<events::resource::AssetSavedNotification>(
            [this](const events::resource::AssetSavedNotification&)
            {
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                {
                    loadDirectory(currentPath);
                }
            });

        fileMovedToken = dispatcher.subscribe<events::fileops::FileMovedNotification>(
            [this](const events::fileops::FileMovedNotification&)
            {
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                {
                    loadDirectory(currentPath);
                    clearSelection();
                }
            });

        fileDeletedToken = dispatcher.subscribe<events::fileops::FileDeletedNotification>(
            [this](const events::fileops::FileDeletedNotification&)
            {
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                {
                    loadDirectory(currentPath);
                    clearSelection();
                }
            });

        folderSelectedToken = dispatcher.subscribe<FolderSelectedNotification>(
            [this](const FolderSelectedNotification& notification)
            {
                navigateTo(notification.folderPath);
            });

        batchCompletedToken = dispatcher.subscribe<events::fileops::FileOpBatchCompletedNotification>(
            [this](const events::fileops::FileOpBatchCompletedNotification&)
            {
                pendingRefresh.store(true);
            });
    }

    ContentBrowser::~ContentBrowser()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (projectLoadedToken.isValid())
        {
            dispatcher.unsubscribe(projectLoadedToken);
        }
        if (importCompletedToken.isValid())
        {
            dispatcher.unsubscribe(importCompletedToken);
        }
        if (assetSavedToken.isValid())
        {
            dispatcher.unsubscribe(assetSavedToken);
        }
        if (fileMovedToken.isValid())
        {
            dispatcher.unsubscribe(fileMovedToken);
        }
        if (fileDeletedToken.isValid())
        {
            dispatcher.unsubscribe(fileDeletedToken);
        }
        if (folderSelectedToken.isValid())
        {
            dispatcher.unsubscribe(folderSelectedToken);
        }
        if (batchCompletedToken.isValid())
        {
            dispatcher.unsubscribe(batchCompletedToken);
        }
    }

    void ContentBrowser::draw()
    {
        if (pendingRefresh.exchange(false))
        {
            if (fs::exists(currentPath) && fs::is_directory(currentPath))
            {
                loadDirectory(currentPath);
                clearSelection();
            }
        }

        gridRenderer->ensureIconsLoaded();

        if (!importLocationSet)
        {
            controllers::Import::setLocation(currentPath.string());
            importLocationSet = true;
        }

        updateCutState();

        modals->processModals(currentPath, selectedFile);
        drawContentPanel();
    }

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
        {
            return;
        }

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
                if (selectedType == AssetType::Script)
                {
                    std::string filePath = StringUtil::wstringToUtf8(selectedFile.wstring());
                    std::string args = "\"" + filePath + "\"";
                    ShellExecuteA(nullptr, "open", "code", args.c_str(), nullptr, SW_SHOWNORMAL);
                }
                else if (selectedType == AssetType::Terrain)
                {
                    events::terrain::BeginTerrainLoadCommand cmd;
                    cmd.path = StringUtil::wstringToUtf8(selectedFile.wstring());
                    events::EventDispatcher::instance().execute(cmd);
                }
                else if (selectedType == AssetType::Water)
                {
                    events::water::LoadWaterCommand cmd;
                    cmd.path = StringUtil::wstringToUtf8(selectedFile.wstring());
                    events::EventDispatcher::instance().execute(cmd);
                }
                else if (selectedType == AssetType::Scene)
                {
                    events::scene::LoadSceneCommand cmd;
                    cmd.filePath = StringUtil::wstringToUtf8(selectedFile.wstring());
                    events::EventDispatcher::instance().execute(cmd);
                }
                else if (selectedType == AssetType::InputMapping)
                {
                    events::application::OpenInputMappingWindowNotification notif;
                    events::EventDispatcher::instance().publish(notif);
                }
                else if (selectedType != AssetType::Navmesh && selectedType != AssetType::PhysAnim)
                {
                    showFileWindow = true;
                }
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
        {
            ImGui::SetTooltip("Copy path to clipboard");
        }

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
        {
            ImGui::SetTooltip("Import assets");
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
            {
                ImGui::SetTooltip("Click to edit path");
            }

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

    AssetType ContentBrowser::detectAssetType(const fs::directory_entry& entry)
    {
        using enum windows::AssetType;

        std::error_code statusEc;
        if (entry.is_directory(statusEc) || statusEc)
        {
            return Other;
        }

        std::string extension = entry.path().extension().string();

        if (extension == ".vfMat") return Material;
        if (extension == ".vfFont") return Font;
        if (extension == ".vfproj") return Project;
        if (extension == ".vfMatInstance") return MaterialInstance;
        if (extension == ".vfAnimator") return Animator;
        if (extension == ".vfVFX") return VFX;
        if (extension == ".vfPrefab") return Prefab;
        if (extension == ".vfTerrainMat") return TerrainMaterial;
        if (extension == ".vfTerrain") return Terrain;
        if (extension == ".vfNavmesh") return Navmesh;
        if (extension == ".vfNavIndex") return Navmesh;
        if (extension == ".vfNavTile") return Navmesh;
        if (extension == ".vfPhysAnim") return PhysAnim;
        if (extension == ".vfWater") return Water;
        if (extension == ".vfBehaviorTree") return BehaviorTree;
        if (extension == ".mt") return Script;
        if (extension == ".vfplugin") return Plugin;
        if (extension == ".vfInputMapping") return InputMapping;

        bool isVfAsset = (extension == ".vfImage" || extension == ".vfHdr" ||
            extension == ".vfMesh" || extension == ".vfAudio" ||
            extension == ".vfAnim" || extension == ".vfScene");

        if (!isVfAsset)
        {
            return Other;
        }

        resource::FileType fileType = resource::ResourceManager::readHeaderFile(entry);

        if (fileType == resource::FileType::TEXTURE) return Texture;
        if (fileType == resource::FileType::SCENE) return Scene;
        if (fileType == resource::FileType::HDR) return HDR;
        if (fileType == resource::FileType::MESH) return Model;
        if (fileType == resource::FileType::AUDIO) return Audio;
        if (fileType == resource::FileType::ANIMATION) return Animation;

        return Other;
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
                std::string filename = entry.path().filename().string();
                if (filename.empty() || filename == "nul" || filename == "con" ||
                    filename == "prn" || filename == "aux" || filename == "." || filename == "..")
                {
                    continue;
                }

                // Hide .vfmeta sidecar files and asset database index from the content browser
                if (entry.path().extension() == ".vfmeta" || filename == "assetdb.json")
                {
                    continue;
                }

                Asset asset;
                asset.path = StringUtil::wstringToUtf8(entry.path().wstring());
                asset.name = StringUtil::wstringToUtf8(entry.path().filename().wstring());
                asset.type = detectAssetType(entry);
                assets.push_back(asset);
            }
            catch (const std::exception&)
            {
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
            // If clicking on already selected item, keep selection (allows multi-drag)
            // If clicking on unselected item, clear and select only that item
            if (selectedPaths.find(path) == selectedPaths.end())
            {
                selectedPaths.clear();
                selectedPaths.insert(path);
            }
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

    std::vector<ClipboardItem> ContentBrowser::buildClipboardItems() const
    {
        std::vector<ClipboardItem> items;
        for (const auto& path : getSelectedPaths())
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
        return items;
    }

    void ContentBrowser::performCut()
    {
        auto items = buildClipboardItems();
        if (!items.empty())
        {
            ClipboardManager::instance().cut(items);
        }
    }

    void ContentBrowser::performCopy()
    {
        auto items = buildClipboardItems();
        if (!items.empty())
        {
            ClipboardManager::instance().copy(items);
        }
    }

    bool ContentBrowser::performPaste()
    {
        auto& clipboard = ClipboardManager::instance();
        if (clipboard.hasItems())
        {
            clipboard.paste(currentPath.string());
            return true;
        }
        return false;
    }
}
