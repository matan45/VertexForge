#include "BoneMaskEditorPanel.hpp"
#include "imgui.h"
#include <algorithm>

namespace windows::animation
{
    void BoneMaskEditorPanel::draw(animator::AnimatorData* animatorData, bool& isDirty)
    {
        if (!animatorData)
            return;

        drawMaskList(animatorData, isDirty);

        if (selectedMaskIndex >= 0 && selectedMaskIndex < static_cast<int>(animatorData->boneMasks.size()))
        {
            ImGui::Separator();
            ImGui::Text("Bones in '%s':", animatorData->boneMasks[selectedMaskIndex].name.c_str());
            drawBoneTree(animatorData->boneMasks[selectedMaskIndex], isDirty);
        }
    }

    void BoneMaskEditorPanel::drawMaskList(animator::AnimatorData* animatorData, bool& isDirty)
    {
        // Add mask
        char nameBuf[128] = {};
        std::strncpy(nameBuf, newMaskName.c_str(), sizeof(nameBuf) - 1);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputText("##NewMaskName", nameBuf, sizeof(nameBuf)))
        {
            newMaskName = nameBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Mask") && !newMaskName.empty())
        {
            animator::BoneMaskDefinition mask;
            mask.name = newMaskName;
            animatorData->boneMasks.push_back(std::move(mask));
            selectedMaskIndex = static_cast<int>(animatorData->boneMasks.size()) - 1;
            newMaskName.clear();
            isDirty = true;
        }

        // Mask list
        for (int i = 0; i < static_cast<int>(animatorData->boneMasks.size()); ++i)
        {
            ImGui::PushID(i);
            bool isSelected = (i == selectedMaskIndex);

            if (ImGui::Selectable(animatorData->boneMasks[i].name.c_str(), isSelected))
            {
                selectedMaskIndex = i;
            }

            // Context menu for delete
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Delete"))
                {
                    animatorData->boneMasks.erase(animatorData->boneMasks.begin() + i);
                    if (selectedMaskIndex >= static_cast<int>(animatorData->boneMasks.size()))
                        selectedMaskIndex = static_cast<int>(animatorData->boneMasks.size()) - 1;
                    isDirty = true;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    return;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    void BoneMaskEditorPanel::drawBoneTree(animator::BoneMaskDefinition& mask, bool& isDirty)
    {
        // Simple list of bone names with add/remove
        char boneBuf[128] = {};
        std::strncpy(boneBuf, newBoneName.c_str(), sizeof(boneBuf) - 1);
        boneBuf[sizeof(boneBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputText("##BoneName", boneBuf, sizeof(boneBuf)))
        {
            newBoneName = boneBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Bone") && !newBoneName.empty())
        {
            std::string boneName = newBoneName;
            auto it = std::find(mask.includedBoneNames.begin(), mask.includedBoneNames.end(), boneName);
            if (it == mask.includedBoneNames.end())
            {
                mask.includedBoneNames.push_back(boneName);
                newBoneName.clear();
                isDirty = true;
            }
        }

        // Display included bones
        int removeIdx = -1;
        for (int i = 0; i < static_cast<int>(mask.includedBoneNames.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::BulletText("%s", mask.includedBoneNames[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                removeIdx = i;
                isDirty = true;
            }
            ImGui::PopID();
        }

        if (removeIdx >= 0)
        {
            mask.includedBoneNames.erase(mask.includedBoneNames.begin() + removeIdx);
        }
    }
}
