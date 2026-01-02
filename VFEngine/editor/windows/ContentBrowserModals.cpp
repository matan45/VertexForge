#include "ContentBrowserModals.hpp"
#include "imgui.h"
#include "string/StringUtil.hpp"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <material/MaterialAsset.hpp>

namespace windows
{
    ContentBrowserModals::ContentBrowserModals(RefreshCallback onRefresh)
        : refreshCallback(std::move(onRefresh))
    {
    }

    void ContentBrowserModals::triggerSavePrefabModal(const services::EntityHandle& entity)
    {
        pendingSavePrefabEntity = entity;
        showSavePrefabModal = true;
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
                ImGui::EndMenu();
            }
            bool hasSelection = !selectedFile.empty();
            if (ImGui::MenuItem("Delete", nullptr, false, hasSelection))
            {
                showDeleteConfirmModal = true;
            }
            if (ImGui::MenuItem("Rename", nullptr, false, hasSelection))
            {
                renameFileName = StringUtil::wstringToUtf8(selectedFile.stem().wstring());
                showRenameFileModal = true;
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
            ImGui::Text("This action cannot be undone!");

            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                if (!selectedFile.empty())
                {
                    std::error_code ec;
                    if (fs::is_directory(selectedFile))
                    {
                        fs::remove_all(selectedFile, ec);
                    }
                    else
                    {
                        fs::remove(selectedFile, ec);
                    }

                    if (!ec)
                    {
                        if (refreshCallback) refreshCallback();
                    }
                    else
                    {
                        vfLogError("Failed to delete file: {}", ec.message());
                    }
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
}
