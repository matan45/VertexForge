#include "ContentBrowserModals.hpp"
#include "print/Log.hpp"
#include "imgui.h"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include <material/MaterialAsset.hpp>
#include <animator/AnimatorAsset.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <terrain/TerrainMaterialAsset.hpp>
#include <behaviortree/BehaviorTreeAsset.hpp>
#include <ui/UIThemeSerialization.hpp>

namespace windows
{
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

    void ContentBrowserModals::drawCreateVFXSequenceModal(const fs::path& currentPath)
    {
        if (showCreateVFXSequenceModal &&
            ImGui::BeginPopupModal("Create New VFX Sequence", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newVFXSequenceName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Sequence Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newVFXSequenceName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newVFXSequenceName.empty())
                {
                    std::string extension = ".vfVFXSequence";
                    fs::path newPath = currentPath / (newVFXSequenceName + extension);

                    int counter = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = currentPath / (newVFXSequenceName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newPath.wstring());
                    auto defaultSeq = vfx::VFXSequenceAsset::createDefault(newVFXSequenceName);
                    if (vfx::VFXSequenceAsset::save(defaultSeq, pathStr))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateVFXSequenceModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateVFXSequenceModal = false;
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

    void ContentBrowserModals::drawCreateThemeModal(const fs::path& currentPath)
    {
        if (showCreateThemeModal &&
            ImGui::BeginPopupModal("Create New UI Theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char buffer[256];
            std::strncpy(buffer, newThemeName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Theme Name", buffer, IM_ARRAYSIZE(buffer)))
            {
                newThemeName = std::string(buffer);
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!newThemeName.empty())
                {
                    std::string extension = ".vfTheme";
                    fs::path newPath = currentPath / (newThemeName + extension);

                    int counter = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = currentPath / (newThemeName + "_" + std::to_string(counter) + extension);
                        counter++;
                    }

                    std::string pathStr = StringUtil::wstringToUtf8(newPath.wstring());

                    // Starter theme with one example style so the format is discoverable
                    utilities::ui::UITheme defaultTheme;
                    utilities::ui::UIThemeStyle exampleStyle;
                    exampleStyle.colors["labelColor"] = {1.0f, 1.0f, 1.0f, 1.0f};
                    exampleStyle.floats["fontSize"] = 16.0f;
                    defaultTheme.styles["Default"] = exampleStyle;

                    if (utilities::ui::UIThemeSerialization::saveToFile(defaultTheme, pathStr))
                    {
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        events::EventDispatcher::instance().publish(assetNotif);
                        if (refreshCallback) refreshCallback();
                    }
                }
                ImGui::CloseCurrentPopup();
                showCreateThemeModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                showCreateThemeModal = false;
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
                        // Register with the AssetDatabase immediately so the
                        // prefab gets its .vfmeta + GUID without a rescan
                        events::resource::AssetSavedNotification assetNotif;
                        assetNotif.filePath = pathStr;
                        dispatcher.publish(assetNotif);
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
}
