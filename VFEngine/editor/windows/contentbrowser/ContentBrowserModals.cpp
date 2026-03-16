#include "print/Log.hpp"
#include "ContentBrowserModals.hpp"
#include "imgui.h"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/project/FileOperationsEvents.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "../../fileops/AsyncFileOperations.hpp"
#include <material/MaterialAsset.hpp>
#include <animator/AnimatorAsset.hpp>
#include <vfx/VFXAsset.hpp>
#include <terrain/TerrainMaterialAsset.hpp>
#include <behaviortree/BehaviorTreeAsset.hpp>
#include <asset/AssetDatabase.hpp>

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
        {
            ImGui::OpenPopup("Create New Folder");
        }
        drawCreateFolderModal(currentPath);

        if (showCreateMaterialModal)
        {
            ImGui::OpenPopup("Create New Material");
        }
        drawCreateMaterialModal(currentPath);

        if (showCreateAnimatorModal)
        {
            ImGui::OpenPopup("Create New Animator");
        }
        drawCreateAnimatorModal(currentPath);

        if (showCreateVFXModal)
        {
            ImGui::OpenPopup("Create New VFX");
        }
        drawCreateVFXModal(currentPath);

        if (showCreateTerrainMaterialModal)
        {
            ImGui::OpenPopup("Create New Terrain Material");
        }
        drawCreateTerrainMaterialModal(currentPath);

        if (showCreateBehaviorTreeModal)
        {
            ImGui::OpenPopup("Create New Behavior Tree");
        }
        drawCreateBehaviorTreeModal(currentPath);

        if (showSavePrefabModal)
        {
            ImGui::OpenPopup("Save Prefab");
        }
        drawSavePrefabModal(currentPath);

        if (showRenameFileModal)
        {
            ImGui::OpenPopup("Rename File");
        }
        drawRenameModal(selectedFile);

        if (showDeleteConfirmModal)
        {
            ImGui::OpenPopup("Delete File?");
        }
        drawDeleteModal(selectedFile);

        if (showReferencesModal)
        {
            ImGui::OpenPopup("Asset References");
        }
        drawReferencesModal();

        if (showDependenciesModal)
        {
            ImGui::OpenPopup("Asset Dependencies");
        }
        drawDependenciesModal();

        if (showErrorModal)
        {
            ImGui::OpenPopup("Error##FileOpsError");
        }
        drawErrorModal();
    }

    void ContentBrowserModals::drawContextMenu(const fs::path& selectedFile)
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
                ImGui::EndMenu();
            }

            ImGui::Separator();

            bool hasSelection = !selectedFile.empty();

            // Cut, Copy, Paste
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
                showDeleteConfirmModal = true;
            }

            ImGui::Separator();

            // Asset Database actions
            if (hasSelection)
            {
                std::string selectedPath = StringUtil::wstringToUtf8(selectedFile.wstring());
                // Try to get GUID, auto-register if not tracked
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

    void ContentBrowserModals::drawCreateFolderModal(const fs::path& currentPath)
    {
        if (showCreateFolderModal &&
            ImGui::BeginPopupModal("Create New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newFolderName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Folder Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newFolderName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                createFolder(currentPath, newFolderName);
                ImGui::CloseCurrentPopup();
                showCreateFolderModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateFolderModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawCreateMaterialModal(const fs::path& currentPath)
    {
        if (showCreateMaterialModal &&
            ImGui::BeginPopupModal("Create New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newMaterialName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Material Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newMaterialName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newMaterialName.empty())
                {
                    std::string extension = ".vfMat";
                    fs::path newMaterialPath = currentPath / (newMaterialName + extension);

                    int counter = 1;
                    while (fs::exists(newMaterialPath))
                    {
                        newMaterialPath = currentPath / (newMaterialName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newMaterialPath.wstring());
                    auto defaultMat = material::MaterialAsset::createDefault(newMaterialName);
                    if (material::MaterialAsset::save(pathStr, defaultMat))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateMaterialModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateMaterialModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawCreateAnimatorModal(const fs::path& currentPath)
    {
        if (showCreateAnimatorModal &&
            ImGui::BeginPopupModal("Create New Animator", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newAnimatorName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Animator Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newAnimatorName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newAnimatorName.empty())
                {
                    std::string extension = ".vfAnimator";
                    fs::path newAnimatorPath = currentPath / (newAnimatorName + extension);

                    int counter = 1;
                    while (fs::exists(newAnimatorPath))
                    {
                        newAnimatorPath = currentPath / (newAnimatorName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newAnimatorPath.wstring());
                    auto defaultAnimator = animator::AnimatorAsset::createDefault(newAnimatorName);
                    if (animator::AnimatorAsset::save(pathStr, defaultAnimator))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateAnimatorModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateAnimatorModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawCreateVFXModal(const fs::path& currentPath)
    {
        if (showCreateVFXModal &&
            ImGui::BeginPopupModal("Create New VFX", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newVFXName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("VFX Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newVFXName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newVFXName.empty())
                {
                    std::string extension = ".vfVFX";
                    fs::path newVFXPath = currentPath / (newVFXName + extension);

                    int counter = 1;
                    while (fs::exists(newVFXPath))
                    {
                        newVFXPath = currentPath / (newVFXName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newVFXPath.wstring());
                    auto defaultVFX = vfx::VFXAsset::createDefault(newVFXName);
                    if (vfx::VFXAsset::save(pathStr, defaultVFX))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateVFXModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateVFXModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawCreateTerrainMaterialModal(const fs::path& currentPath)
    {
        if (showCreateTerrainMaterialModal &&
            ImGui::BeginPopupModal("Create New Terrain Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newTerrainMaterialName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Material Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newTerrainMaterialName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newTerrainMaterialName.empty())
                {
                    std::string extension = ".vfTerrainMat";
                    fs::path newPath = currentPath / (newTerrainMaterialName + extension);

                    int counter = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = currentPath / (newTerrainMaterialName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newPath.wstring());
                    auto defaultMat = terrain::TerrainMaterialAsset::createDefault(newTerrainMaterialName);
                    if (terrain::TerrainMaterialAsset::save(pathStr, defaultMat))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateTerrainMaterialModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateTerrainMaterialModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawCreateBehaviorTreeModal(const fs::path& currentPath)
    {
        if (showCreateBehaviorTreeModal &&
            ImGui::BeginPopupModal("Create New Behavior Tree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newBehaviorTreeName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Behavior Tree Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newBehaviorTreeName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newBehaviorTreeName.empty())
                {
                    std::string extension = ".vfBehaviorTree";
                    fs::path newPath = currentPath / (newBehaviorTreeName + extension);

                    int counter = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = currentPath / (newBehaviorTreeName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newPath.wstring());
                    auto defaultBT = behaviortree::BehaviorTreeAsset::createDefault(newBehaviorTreeName);
                    if (behaviortree::BehaviorTreeAsset::save(pathStr, defaultBT))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateBehaviorTreeModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateBehaviorTreeModal = false;
            }
            ImGui::EndPopup();
        }
    }

    void ContentBrowserModals::drawSavePrefabModal(const fs::path& currentPath)
    {
        if (showSavePrefabModal &&
            ImGui::BeginPopupModal("Save Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newPrefabName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Prefab Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newPrefabName = std::string(buffer);
            }

            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                if (!newPrefabName.empty() && pendingSavePrefabEntity.isValid())
                {
                    std::string extension = ".vfPrefab";
                    fs::path newPrefabPath = currentPath / (newPrefabName + extension);

                    int counter = 1;
                    while (fs::exists(newPrefabPath))
                    {
                        newPrefabPath = currentPath / (newPrefabName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newPrefabPath.wstring());
                    events::scene::SavePrefabCommand cmd;
                    cmd.entity = pendingSavePrefabEntity;
                    cmd.filePath = pathStr;
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (dispatcher.execute(cmd))
                    {
                        if (refreshCallback) refreshCallback();
                    }
                }
                newPrefabName.clear();
                pendingSavePrefabEntity = services::EntityHandle::invalid();
                ImGui::CloseCurrentPopup();
                showSavePrefabModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                newPrefabName.clear();
                pendingSavePrefabEntity = services::EntityHandle::invalid();
                ImGui::CloseCurrentPopup();
                showSavePrefabModal = false;
            }
            ImGui::EndPopup();
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

                    std::error_code ec;
                    if (!fs::exists(newPath))
                    {
                        fs::rename(selectedFile, newPath, ec);
                        if (!ec)
                        {
                            if (refreshCallback) refreshCallback();
                        }
                        else
                        {
                            vfLogError("Failed to rename file: {}", ec.message());
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
            ImGui::Text("Are you sure you want to delete:");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
                               StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());
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

    void ContentBrowserModals::drawReferencesModal()
    {
        if (showReferencesModal &&
            ImGui::BeginPopupModal("Asset References", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("References to: %s", referencesAssetPath.c_str());
            ImGui::Text("GUID: %s", referencesGuid.toString().c_str());
            ImGui::Separator();

            auto dependentGuids = asset::AssetDatabase::instance().getDependents(referencesGuid);

            if (dependentGuids.empty())
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No assets reference this file.");
            }
            else
            {
                ImGui::Text("%zu asset(s) reference this file:", dependentGuids.size());
                ImGui::BeginChild("ReferencesList", ImVec2(500, 200), true);
                for (const auto& depGuid : dependentGuids)
                {
                    auto pathOpt = asset::AssetDatabase::instance().getPath(depGuid);
                    if (pathOpt)
                    {
                        ImGui::BulletText("%s", pathOpt->c_str());
                    }
                }
                ImGui::EndChild();
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

            auto depGuids = asset::AssetDatabase::instance().getDependencies(dependenciesGuid);

            if (depGuids.empty())
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "This asset has no dependencies.");
            }
            else
            {
                ImGui::Text("This asset depends on %zu asset(s):", depGuids.size());
                ImGui::BeginChild("DependenciesList", ImVec2(500, 200), true);
                for (const auto& depGuid : depGuids)
                {
                    auto pathOpt = asset::AssetDatabase::instance().getPath(depGuid);
                    if (pathOpt)
                    {
                        ImGui::BulletText("%s", pathOpt->c_str());
                    }
                }
                ImGui::EndChild();
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
