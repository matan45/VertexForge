#include "print/Log.hpp"
#include "ContentBrowserModals.hpp"
#include "imgui.h"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/project/FileOperationsEvents.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "../scene/FolderStructureWindow.hpp"
#include "../../fileops/AsyncFileOperations.hpp"

namespace
{
    std::string toClipboardPath(const fs::path& path)
    {
        std::error_code ec;
        fs::path fullPath = fs::weakly_canonical(path, ec);
        if (ec)
        {
            ec.clear();
            fullPath = fs::absolute(path, ec);
            if (ec)
                fullPath = path;
        }

        return StringUtil::wstringToUtf8(fullPath.wstring());
    }
}

namespace windows
{
    ContentBrowserModals::ContentBrowserModals(RefreshCallback onRefresh)
        : refreshCallback(std::move(onRefresh))
    {
    }

    void ContentBrowserModals::setClipboardCallbacks(ClipboardCallback onCut, ClipboardCallback onCopy,
                                                     PasteCallback onPaste, std::function<bool()> hasClipboardItems)
    {
        cutCallback = std::move(onCut);
        copyCallback = std::move(onCopy);
        pasteCallback = std::move(onPaste);
        hasClipboardItemsCallback = std::move(hasClipboardItems);
    }

    void ContentBrowserModals::triggerSavePrefabModal(const services::EntityHandle& entity)
    {
        pendingSavePrefabEntity = entity;
        showSavePrefabModal = true;
    }

    void ContentBrowserModals::triggerDeleteModal()
    {
        showDeleteConfirmModal = true;
        deleteDependentsChecked = false;
        deleteDependents.clear();
    }

    void ContentBrowserModals::showError(const std::string& title, const std::string& message,
                                         const std::vector<std::string>& details)
    {
        errorTitle = title;
        errorMessage = message;
        errorDetails = details;
        showErrorModal = true;
    }

    void ContentBrowserModals::processModals(const fs::path& currentPath, const fs::path& selectedFile)
    {
        if (showCreateFolderModal)
            ImGui::OpenPopup("Create New Folder");
        drawCreateFolderModal(currentPath);

        if (showCreateMaterialModal)
            ImGui::OpenPopup("Create New Material");
        drawCreateMaterialModal(currentPath);

        if (showCreateAnimatorModal)
            ImGui::OpenPopup("Create New Animator");
        drawCreateAnimatorModal(currentPath);

        if (showCreateVFXModal)
            ImGui::OpenPopup("Create New VFX");
        drawCreateVFXModal(currentPath);

        if (showCreateTerrainMaterialModal)
            ImGui::OpenPopup("Create New Terrain Material");
        drawCreateTerrainMaterialModal(currentPath);

        if (showCreateBehaviorTreeModal)
            ImGui::OpenPopup("Create New Behavior Tree");
        drawCreateBehaviorTreeModal(currentPath);

        if (showCreateThemeModal)
            ImGui::OpenPopup("Create New UI Theme");
        drawCreateThemeModal(currentPath);

        if (showSavePrefabModal)
            ImGui::OpenPopup("Save Prefab");
        drawSavePrefabModal(currentPath);

        if (showRenameFileModal)
            ImGui::OpenPopup("Rename File");
        drawRenameModal(selectedFile);

        if (showDeleteConfirmModal)
            ImGui::OpenPopup("Delete File?");
        drawDeleteModal(selectedFile);

        if (showReferencesModal)
            ImGui::OpenPopup("Asset References");
        drawReferencesModal();

        if (showDependenciesModal)
            ImGui::OpenPopup("Asset Dependencies");
        drawDependenciesModal();

        if (showErrorModal)
            ImGui::OpenPopup("Error##FileOpsError");
        drawErrorModal();
    }

    void ContentBrowserModals::drawContextMenu(const Asset* selectedAsset)
    {
        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem("Create New Folder"))
            {
                showCreateFolderModal = true;
                newFolderName.clear();
            }
            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Material"))
                {
                    showCreateMaterialModal = true;
                    newMaterialName.clear();
                }
                if (ImGui::MenuItem("Animator"))
                {
                    showCreateAnimatorModal = true;
                    newAnimatorName.clear();
                }
                if (ImGui::MenuItem("VFX"))
                {
                    showCreateVFXModal = true;
                    newVFXName.clear();
                }
                if (ImGui::MenuItem("Terrain Material"))
                {
                    showCreateTerrainMaterialModal = true;
                    newTerrainMaterialName.clear();
                }
                if (ImGui::MenuItem("Behavior Tree"))
                {
                    showCreateBehaviorTreeModal = true;
                    newBehaviorTreeName.clear();
                }
                if (ImGui::MenuItem("UI Theme"))
                {
                    showCreateThemeModal = true;
                    newThemeName.clear();
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            bool hasSelection = selectedAsset != nullptr && !selectedAsset->path.empty();
            fs::path selectedFile = hasSelection ? fs::path(selectedAsset->path) : fs::path{};

            if (hasSelection)
            {
                std::string sizeLabel = "Size: ";
                if (selectedAsset->isDirectory)
                    sizeLabel += "Folder";
                else if (selectedAsset->fileSizeKnown)
                    sizeLabel += formatFileSize(selectedAsset->fileSize);
                else
                    sizeLabel += "Unavailable";

                ImGui::BeginDisabled();
                ImGui::MenuItem(sizeLabel.c_str());
                ImGui::EndDisabled();

                if (ImGui::MenuItem("Copy Full Path"))
                {
                    const std::string fullPath = toClipboardPath(selectedFile);
                    ImGui::SetClipboardText(fullPath.c_str());
                }

                ImGui::Separator();
            }

            if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection))
            {
                if (cutCallback) cutCallback();
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
            {
                if (copyCallback) copyCallback();
            }
            bool canPaste = hasClipboardItemsCallback && hasClipboardItemsCallback();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste))
            {
                if (pasteCallback) pasteCallback();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Rename", nullptr, false, hasSelection))
            {
                renameFileName = StringUtil::wstringToUtf8(selectedFile.stem().wstring());
                showRenameFileModal = true;
            }
            if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
            {
                triggerDeleteModal();
            }

            ImGui::Separator();

            if (hasSelection)
            {
                std::string selectedPath = StringUtil::wstringToUtf8(selectedFile.wstring());
                auto ref = asset::AssetRef::fromPath(selectedPath);

                if (ref.isValid())
                {
                    if (ImGui::MenuItem("Copy GUID"))
                    {
                        ImGui::SetClipboardText(ref.getGUID().toString().c_str());
                    }

                    if (ImGui::MenuItem("Find References"))
                    {
                        showReferencesModal = true;
                        referencesGuid = ref.getGUID();
                        referencesAssetPath = selectedPath;
                    }

                    if (ImGui::MenuItem("Show Dependencies"))
                    {
                        showDependenciesModal = true;
                        dependenciesGuid = ref.getGUID();
                        dependenciesAssetPath = selectedPath;
                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::createFolder(const fs::path& currentPath, const std::string& folderName)
    {
        fs::path newFolderPath = currentPath / folderName;
        try
        {
            if (!fs::exists(newFolderPath))
            {
                fs::create_directory(newFolderPath);
                if (refreshCallback) refreshCallback();
            }
            else
            {
                vfLogWarning("Folder already exists.");
            }
        }
        catch (const fs::filesystem_error& e)
        {
            ImGui::Text("Failed to create folder: %s", e.what());
        }
    }

    void ContentBrowserModals::drawRenameModal(const fs::path& selectedFile)
    {
        if (showRenameFileModal &&
            ImGui::BeginPopupModal("Rename File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Renaming: %s", StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());
            ImGui::Separator();

            char buffer[256];
            std::strncpy(buffer, renameFileName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("New Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                renameFileName = std::string(buffer);
            }

            if (ImGui::Button("Rename", ImVec2(120, 0)))
            {
                if (!renameFileName.empty() && !selectedFile.empty())
                {
                    fs::path newPath = selectedFile.parent_path() / (renameFileName + selectedFile.extension().
                        string());

                    if (!fs::exists(newPath))
                    {
                        events::fileops::MoveFileCommand cmd;
                        cmd.sourcePath = StringUtil::wstringToUtf8(selectedFile.wstring());
                        cmd.destPath = StringUtil::wstringToUtf8(newPath.wstring());
                        auto result = events::EventDispatcher::instance().execute(cmd);
                        if (result.success)
                        {
                            if (refreshCallback) refreshCallback();
                        }
                        else
                        {
                            vfLogError("Failed to rename file: {}", result.errorMessage);
                        }
                    }
                    else
                    {
                        vfLogError("A file with that name already exists");
                    }
                }
                ImGui::CloseCurrentPopup();
                showRenameFileModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showRenameFileModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawDeleteModal(const fs::path& selectedFile)
    {
        if (showDeleteConfirmModal &&
            ImGui::BeginPopupModal("Delete File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!deleteDependentsChecked)
            {
                deleteDependentsChecked = true;
                deleteDependents.clear();

                // Read-only GUID lookup (AssetRef::fromPath would register
                // the file we are about to delete)
                auto& dispatcher = events::EventDispatcher::instance();
                events::assetdb::GetAssetGUIDQuery guidQuery;
                guidQuery.path = StringUtil::wstringToUtf8(selectedFile.wstring());
                if (auto guidOpt = dispatcher.query(guidQuery))
                {
                    events::assetdb::GetAssetDependentsQuery depsQuery;
                    depsQuery.guid = *guidOpt;
                    deleteDependents = dispatcher.query(depsQuery);
                }
            }

            ImGui::Text("Are you sure you want to delete:");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
                               StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());

            if (!deleteDependents.empty())
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                   "Warning: %zu asset(s) reference this file:", deleteDependents.size());
                ImGui::BeginChild("DeleteDependentsList", ImVec2(500, 150), true);
                bool navigated = drawAssetGuidList(deleteDependents);
                ImGui::EndChild();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                   "Deleting it will leave those references broken.");
                if (navigated)
                {
                    ImGui::CloseCurrentPopup();
                    showDeleteConfirmModal = false;
                    ImGui::EndPopup();
                    return;
                }
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Note: Files can be recovered via Undo (Ctrl+Z)");

            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                if (!selectedFile.empty() && !AsyncFileOperations::isBusy())
                {
                    std::string path = StringUtil::wstringToUtf8(selectedFile.wstring());
                    AsyncFileOperations::deleteAsync(path);
                }
                ImGui::CloseCurrentPopup();
                showDeleteConfirmModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showDeleteConfirmModal = false;
            }
            ImGui::EndPopup();
        }
    }

    bool ContentBrowserModals::drawAssetGuidList(const std::vector<asset::AssetGUID>& guids)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool navigated = false;

        for (const auto& guid : guids)
        {
            events::assetdb::GetAssetPathQuery pathQuery;
            pathQuery.guid = guid;
            auto pathOpt = dispatcher.query(pathQuery);

            if (!pathOpt)
            {
                // Stale graph entry (asset deleted/unregistered) — show it
                // instead of silently skipping so the count stays honest
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "<unresolved> %s",
                                   guid.toString().c_str());
                continue;
            }

            std::string label = *pathOpt + "##" + guid.toString();
            ImGui::Selectable(label.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Double-click to show in Content Browser");
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    FolderSelectedNotification notification;
                    notification.folderPath = fs::path(*pathOpt).parent_path().string();
                    dispatcher.publish(notification);
                    navigated = true;
                }
            }
        }

        return navigated;
    }

    void ContentBrowserModals::drawReferencesModal()
    {
        if (showReferencesModal &&
            ImGui::BeginPopupModal("Asset References", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("References to: %s", referencesAssetPath.c_str());
            ImGui::Text("GUID: %s", referencesGuid.toString().c_str());
            ImGui::Separator();

            auto& dispatcher = events::EventDispatcher::instance();
            events::assetdb::GetAssetDependentsQuery depsQuery;
            depsQuery.guid = referencesGuid;
            auto dependentGuids = dispatcher.query(depsQuery);

            if (dependentGuids.empty())
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No assets reference this file.");
            }
            else
            {
                ImGui::Text("%zu asset(s) reference this file:", dependentGuids.size());
                ImGui::BeginChild("ReferencesList", ImVec2(500, 200), true);
                bool navigated = drawAssetGuidList(dependentGuids);
                ImGui::EndChild();
                if (navigated)
                {
                    ImGui::CloseCurrentPopup();
                    showReferencesModal = false;
                }
            }

            ImGui::Spacing();
            float buttonWidth = 120.0f;
            float windowWidth = ImGui::GetWindowWidth();
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("Close", ImVec2(buttonWidth, 0)))
            {
                ImGui::CloseCurrentPopup();
                showReferencesModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawDependenciesModal()
    {
        if (showDependenciesModal &&
            ImGui::BeginPopupModal("Asset Dependencies", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Dependencies of: %s", dependenciesAssetPath.c_str());
            ImGui::Text("GUID: %s", dependenciesGuid.toString().c_str());
            ImGui::Separator();

            auto& dispatcher = events::EventDispatcher::instance();
            events::assetdb::GetAssetDependenciesQuery depsQuery;
            depsQuery.guid = dependenciesGuid;
            auto depGuids = dispatcher.query(depsQuery);

            if (depGuids.empty())
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "This asset has no dependencies.");
            }
            else
            {
                ImGui::Text("This asset depends on %zu asset(s):", depGuids.size());
                ImGui::BeginChild("DependenciesList", ImVec2(500, 200), true);
                bool navigated = drawAssetGuidList(depGuids);
                ImGui::EndChild();
                if (navigated)
                {
                    ImGui::CloseCurrentPopup();
                    showDependenciesModal = false;
                }
            }

            ImGui::Spacing();
            float buttonWidth = 120.0f;
            float windowWidth = ImGui::GetWindowWidth();
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("Close", ImVec2(buttonWidth, 0)))
            {
                ImGui::CloseCurrentPopup();
                showDependenciesModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawErrorModal()
    {
        if (showErrorModal &&
            ImGui::BeginPopupModal("Error##FileOpsError", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", errorTitle.c_str());
            ImGui::Separator();

            ImGui::TextWrapped("%s", errorMessage.c_str());

            if (!errorDetails.empty())
            {
                ImGui::Spacing();
                ImGui::Text("Details:");
                ImGui::BeginChild("ErrorDetails", ImVec2(400, 100), true);
                for (const auto& detail : errorDetails)
                {
                    ImGui::BulletText("%s", detail.c_str());
                }
                ImGui::EndChild();
            }

            ImGui::Spacing();
            float buttonWidth = 120.0f;
            float windowWidth = ImGui::GetWindowWidth();
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
            {
                ImGui::CloseCurrentPopup();
                showErrorModal = false;
                errorTitle.clear();
                errorMessage.clear();
                errorDetails.clear();
            }
            ImGui::EndPopup();
        }
    }
}
