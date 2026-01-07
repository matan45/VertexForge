#include "ContentBrowserModals.hpp"
#include "imgui.h"
#include "string/StringUtil.hpp"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/FileOperationsEvents.hpp"
#include <material/MaterialAsset.hpp>

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

    void ContentBrowserModals::showOperationError(const services::FileOperationResult& result)
    {
        if (!result.success)
        {
            showError("File Operation Failed", result.errorMessage, result.conflicts);
        }
    }

    void ContentBrowserModals::showConflict(const std::string& sourcePath, const std::string& destPath,
                                            std::function<void(ConflictResolution, const std::string&)> callback)
    {
        conflictSourcePath = sourcePath;
        conflictDestPath = destPath;
        conflictCallback = std::move(callback);

        // Generate default new name
        fs::path source(sourcePath);
        std::string baseName = source.stem().string();
        std::string extension = source.extension().string();
        conflictNewName = baseName + "_copy" + extension;

        showConflictModal = true;
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

        if (showErrorModal)
        {
            ImGui::OpenPopup("Error##FileOpsError");
        }
        drawErrorModal();

        if (showConflictModal)
        {
            ImGui::OpenPopup("File Conflict##FileConflict");
        }
        drawConflictModal();
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
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Note: Files can be recovered via Undo (Ctrl+Z)");

            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                if (!selectedFile.empty())
                {
                    // Use FileOperationsService via events for undo support
                    events::fileops::DeleteFileCommand cmd;
                    cmd.path = StringUtil::wstringToUtf8(selectedFile.wstring());

                    auto& dispatcher = events::EventDispatcher::instance();
                    auto result = dispatcher.execute(cmd);

                    if (result.success)
                    {
                        if (refreshCallback) refreshCallback();
                    }
                    else
                    {
                        showError("Delete Failed", result.errorMessage);
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

    void ContentBrowserModals::drawErrorModal()
    {
        if (showErrorModal &&
            ImGui::BeginPopupModal("Error##FileOpsError", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            // Error icon and title
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", errorTitle.c_str());
            ImGui::Separator();

            // Main message
            ImGui::TextWrapped("%s", errorMessage.c_str());

            // Show details if available
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

    void ContentBrowserModals::drawConflictModal()
    {
        if (showConflictModal &&
            ImGui::BeginPopupModal("File Conflict##FileConflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "File Already Exists");
            ImGui::Separator();

            fs::path source(conflictSourcePath);
            fs::path dest(conflictDestPath);

            ImGui::Text("Source: %s", source.filename().string().c_str());
            ImGui::Text("Destination: %s", dest.string().c_str());
            ImGui::Spacing();
            ImGui::Text("A file with this name already exists at the destination.");
            ImGui::Text("What would you like to do?");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Rename option with input
            ImGui::Text("Rename to:");
            ImGui::SameLine();
            char buffer[256];
            std::strncpy(buffer, conflictNewName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText("##NewName", buffer, sizeof(buffer)))
            {
                conflictNewName = std::string(buffer);
            }
            ImGui::SameLine();
            if (ImGui::Button("Rename", ImVec2(80, 0)))
            {
                if (conflictCallback)
                {
                    conflictCallback(ConflictResolution::Rename, conflictNewName);
                }
                ImGui::CloseCurrentPopup();
                showConflictModal = false;
            }

            ImGui::Spacing();

            // Skip and Overwrite buttons
            if (ImGui::Button("Skip", ImVec2(120, 0)))
            {
                if (conflictCallback)
                {
                    conflictCallback(ConflictResolution::Skip, "");
                }
                ImGui::CloseCurrentPopup();
                showConflictModal = false;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Overwrite", ImVec2(120, 0)))
            {
                if (conflictCallback)
                {
                    conflictCallback(ConflictResolution::Overwrite, "");
                }
                ImGui::CloseCurrentPopup();
                showConflictModal = false;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                if (conflictCallback)
                {
                    conflictCallback(ConflictResolution::None, "");
                }
                ImGui::CloseCurrentPopup();
                showConflictModal = false;
            }

            ImGui::EndPopup();
        }
    }
}
