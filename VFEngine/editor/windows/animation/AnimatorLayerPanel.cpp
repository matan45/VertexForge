#include "AnimatorLayerPanel.hpp"
#include "imgui.h"
#include <algorithm>

namespace windows::animation
{
    void AnimatorLayerPanel::draw(animator::AnimatorData* animatorData, uint32_t& selectedLayerIndex, bool& isDirty)
    {
        if (!animatorData)
            return;

        if (ImGui::Button("Add Layer"))
        {
            animator::AnimationLayerData newLayer;
            newLayer.name = "Layer " + std::to_string(animatorData->layers.size() + 1);
            newLayer.weight = 1.0f;
            newLayer.blendMode = animator::LayerBlendMode::Override;
            newLayer.sourceMode = animator::LayerSourceMode::StateMachine;

            animator::AnimatorState idleState;
            idleState.id = newLayer.graph.nextStateId++;
            idleState.name = "Idle";
            idleState.loop = true;
            idleState.position = glm::vec2(250.0f, 100.0f);
            newLayer.graph.states.push_back(std::move(idleState));
            newLayer.graph.defaultStateId = 1;

            animatorData->layers.push_back(std::move(newLayer));
            isDirty = true;
        }

        ImGui::SameLine();

        bool canRemove = animatorData->layers.size() > 1 && selectedLayerIndex < animatorData->layers.size();
        if (!canRemove) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Layer"))
        {
            animatorData->layers.erase(animatorData->layers.begin() + selectedLayerIndex);
            if (selectedLayerIndex >= animatorData->layers.size())
                selectedLayerIndex = static_cast<uint32_t>(animatorData->layers.size()) - 1;
            isDirty = true;
        }
        if (!canRemove) ImGui::EndDisabled();

        ImGui::Separator();

        for (uint32_t i = 0; i < static_cast<uint32_t>(animatorData->layers.size()); ++i)
        {
            drawLayerEntry(animatorData->layers[i], i, selectedLayerIndex, isDirty);
        }
    }

    void AnimatorLayerPanel::drawLayerEntry(animator::AnimationLayerData& layer, uint32_t index,
                                             uint32_t& selectedLayerIndex, bool& isDirty)
    {
        ImGui::PushID(static_cast<int>(index));

        bool isSelected = (index == selectedLayerIndex);
        std::string label = layer.name + (index == 0 ? " (Base)" : "");

        if (ImGui::Selectable(label.c_str(), isSelected))
        {
            selectedLayerIndex = index;
        }

        if (isSelected)
        {
            ImGui::Indent();

            // Name
            char nameBuf[128];
            std::strncpy(nameBuf, layer.name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                layer.name = nameBuf;
                isDirty = true;
            }

            // Weight
            if (ImGui::SliderFloat("Weight", &layer.weight, 0.0f, 1.0f))
            {
                isDirty = true;
            }

            // Blend Mode
            const char* blendModes[] = {"Override", "Additive"};
            int blendMode = static_cast<int>(layer.blendMode);
            if (ImGui::Combo("Blend", &blendMode, blendModes, 2))
            {
                layer.blendMode = static_cast<animator::LayerBlendMode>(blendMode);
                isDirty = true;
            }

            // Additive reference pose (only shown for Additive blend mode)
            if (layer.blendMode == animator::LayerBlendMode::Additive)
            {
                const char* refPoses[] = {"BindPose", "FirstFrame", "SpecificFrame"};
                int refPose = static_cast<int>(layer.additiveRefPose);
                if (ImGui::Combo("Ref", &refPose, refPoses, 3))
                {
                    layer.additiveRefPose = static_cast<animator::AdditiveReferencePose>(refPose);
                    isDirty = true;
                }

                if (layer.additiveRefPose == animator::AdditiveReferencePose::SpecificFrame)
                {
                    if (ImGui::DragFloat("Frame", &layer.additiveRefFrame, 0.01f, 0.0f, 100.0f))
                    {
                        isDirty = true;
                    }
                }
            }

            // Source Mode
            const char* sourceModes[] = {"StateMachine", "DirectClip"};
            int sourceMode = static_cast<int>(layer.sourceMode);
            if (ImGui::Combo("Source", &sourceMode, sourceModes, 2))
            {
                layer.sourceMode = static_cast<animator::LayerSourceMode>(sourceMode);
                isDirty = true;
            }

            if (layer.sourceMode == animator::LayerSourceMode::DirectClip)
            {
                char clipBuf[256];
                std::strncpy(clipBuf, layer.directClipPath.c_str(), sizeof(clipBuf) - 1);
                clipBuf[sizeof(clipBuf) - 1] = '\0';
                if (ImGui::InputText("Clip Path", clipBuf, sizeof(clipBuf)))
                {
                    layer.directClipPath = clipBuf;
                    isDirty = true;
                }

                if (ImGui::Checkbox("Loop", &layer.directClipLoop))
                    isDirty = true;

                if (ImGui::DragFloat("Speed", &layer.directClipSpeed, 0.01f, 0.0f, 10.0f))
                    isDirty = true;
            }

            // Bone Mask name
            {
                char maskBuf[128];
                std::strncpy(maskBuf, layer.boneMaskName.c_str(), sizeof(maskBuf) - 1);
                maskBuf[sizeof(maskBuf) - 1] = '\0';
                if (ImGui::InputText("Mask", maskBuf, sizeof(maskBuf)))
                {
                    layer.boneMaskName = maskBuf;
                    isDirty = true;
                }
            }

            ImGui::Unindent();
        }

        ImGui::PopID();
    }
}
