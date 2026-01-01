#include "ContentBrowser.hpp"
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ResourceEvents.hpp"
#include "Import.hpp"
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

namespace windows
{
    ContentBrowser::ContentBrowser()
        : gridRenderer(std::make_unique<AssetGridRenderer>())
        , modals(std::make_unique<ContentBrowserModals>([this]() { loadDirectory(currentPath); }))
        , previewManager(std::make_unique<PreviewWindowManager>())
    {
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
    }

    ContentBrowser::~ContentBrowser()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (importCompletedToken.isValid()) {
            dispatcher.unsubscribe(importCompletedToken);
        }
    }

    void ContentBrowser::draw()
    {
        gridRenderer->ensureIconsLoaded();

        if (!importLocationSet) {
            controllers::Import::setLocation(currentPath.string());
            importLocationSet = true;
        }

        modals->processModals(currentPath, selectedFile);
        drawFolderStructurePanel();
        drawContentPanel();
    }

    void ContentBrowser::drawFolderStructurePanel()
    {
        if (ImGui::Begin("Folder Structure", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
        {
            drawFolderTree(currentPath);
        }
        ImGui::End();
    }

    void ContentBrowser::drawContentPanel()
    {
        if (ImGui::Begin("Content Folder"))
        {
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

            AssetClickResult clickResult = gridRenderer->draw(assets, selectedFile, searchQuery);

            if (clickResult.wasClicked)
            {
                selectedFile = clickResult.clickedPath;
                selectedType = clickResult.clickedType;

                if (clickResult.wasDoubleClicked)
                {
                    showFileWindow = true;
                }
            }

            if (!clickResult.pendingNavigation.empty())
            {
                navigateTo(clickResult.pendingNavigation);
            }

            ImGui::Columns(1);

            if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("ContentFolderDropZone")))
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SCENE_ENTITY"))
                {
                    services::EntityHandle entity = *(services::EntityHandle*)payload->Data;
                    modals->triggerSavePrefabModal(entity);
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

        ImGui::Text("Current Path: %s", StringUtil::wstringToUtf8(currentPath.wstring()).c_str());

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
            using enum windows::AssetType;
            Asset asset;
            asset.path = StringUtil::wstringToUtf8(entry.path().wstring());
            asset.name = StringUtil::wstringToUtf8(entry.path().filename().wstring());
            if (entry.is_directory())
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
                else
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
                    else if (ext == resource::FileType::SHADER)
                    {
                        asset.type = Shader;
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
            }
            assets.push_back(asset);
        }
    }

    void ContentBrowser::drawFolderTree(const fs::path& path)
    {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(path, ec))
        {
            if (entry.is_directory())
            {
                ImGui::Text(ICON_FA_FOLDER "");
                ImGui::SameLine();
                if (ImGui::TreeNode(StringUtil::wstringToUtf8(entry.path().filename().wstring()).c_str()))
                {
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        navigateTo(entry.path());
                    }

                    drawFolderTree(entry.path());

                    ImGui::TreePop();
                }
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
        }
    }
}
