#include "AnimatorPropertiesPanel.hpp"
#include "nfd/FileDialog.hpp"
#include "imgui.h"
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

namespace windows::animation
{
    void AnimatorPropertiesPanel::drawParametersPanel(animator::AnimatorData* animatorData,
                                                       bool& isDirty,
                                                       bool& showAddParameterPopup)
    {
        if (!animatorData)
            return;

        if (ImGui::Button("Add Parameter"))
        {
            showAddParameterPopup = true;
        }

        ImGui::Separator();

        int indexToRemove = -1;
        for (size_t i = 0; i < animatorData->graph.parameters.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            auto& param = animatorData->graph.parameters[i];
            drawParameterEditor(param, i, isDirty);

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                indexToRemove = static_cast<int>(i);
            }

            ImGui::PopID();
        }

        if (indexToRemove >= 0)
        {
            animatorData->graph.parameters.erase(
                animatorData->graph.parameters.begin() + indexToRemove);
            isDirty = true;
        }
    }

    void AnimatorPropertiesPanel::drawParameterEditor(animator::AnimatorParameter& param, size_t index, bool& isDirty)
    {
        const char* typeNames[] = {"Float", "Int", "Bool", "Trigger"};
        ImGui::Text("%s (%s)", param.name.c_str(), typeNames[static_cast<int>(param.type)]);

        switch (param.type)
        {
        case animator::AnimatorParameterType::Float:
            {
                float val = std::get<float>(param.defaultValue);
                if (ImGui::DragFloat("##value", &val, 0.01f))
                {
                    param.defaultValue = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Int:
            {
                int32_t val = std::get<int32_t>(param.defaultValue);
                if (ImGui::DragInt("##value", &val))
                {
                    param.defaultValue = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Bool:
        case animator::AnimatorParameterType::Trigger:
            {
                bool val = std::get<bool>(param.defaultValue);
                if (ImGui::Checkbox("##value", &val))
                {
                    param.defaultValue = val;
                    isDirty = true;
                }
            }
            break;
        }
    }

    void AnimatorPropertiesPanel::drawStatePropertiesPanel(animator::AnimatorData* animatorData,
                                                            uint32_t selectedStateId,
                                                            bool& isDirty)
    {
        if (!animatorData || selectedStateId == 0)
            return;

        auto* state = animatorData->graph.findStateById(selectedStateId);
        if (!state)
            return;

        char nameBuffer[256];
        std::strncpy(nameBuffer, state->name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
        {
            state->name = nameBuffer;
            isDirty = true;
        }

        ImGui::Text("Animation:");
        if (!state->animationPath.empty())
        {
            fs::path animPath(state->animationPath);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", animPath.filename().string().c_str());
        }

        if (ImGui::Button("Select Animation"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Animation Files (*.vfAnim)", L"*.vfAnim"}});
            if (!path.empty())
            {
                state->animationPath = path;
                isDirty = true;
            }
        }

        if (!state->animationPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear##Animation"))
            {
                state->animationPath.clear();
                isDirty = true;
            }
        }

        if (ImGui::DragFloat("Speed", &state->playbackSpeed, 0.01f, 0.0f, 10.0f))
        {
            isDirty = true;
        }

        if (ImGui::Checkbox("Loop", &state->loop))
        {
            isDirty = true;
        }

        bool isDefault = (state->id == animatorData->graph.defaultStateId);
        if (ImGui::Checkbox("Default State", &isDefault))
        {
            if (isDefault)
            {
                animatorData->graph.defaultStateId = state->id;
            }
            isDirty = true;
        }

        ImGui::Separator();
        drawBlendTreeEditor(state, animatorData, isDirty);
    }

    void AnimatorPropertiesPanel::drawTransitionPropertiesPanel(animator::AnimatorData* animatorData,
                                                                 uint32_t selectedTransitionId,
                                                                 bool& isDirty)
    {
        if (!animatorData || selectedTransitionId == 0)
            return;

        animator::AnimatorTransition* transition = nullptr;
        for (auto& t : animatorData->graph.transitions)
        {
            if (t.id == selectedTransitionId)
            {
                transition = &t;
                break;
            }
        }

        if (!transition)
            return;

        std::string sourceName = transition->sourceStateId == 0 ? "Any State" : "Unknown";
        std::string targetName = "Unknown";

        if (transition->sourceStateId != 0)
        {
            if (auto* s = animatorData->graph.findStateById(transition->sourceStateId))
                sourceName = s->name;
        }
        if (auto* t = animatorData->graph.findStateById(transition->targetStateId))
            targetName = t->name;

        ImGui::Text("%s -> %s", sourceName.c_str(), targetName.c_str());
        ImGui::Separator();

        if (ImGui::DragFloat("Blend Duration", &transition->blendDuration, 0.01f, 0.0f, 5.0f))
        {
            isDirty = true;
        }

        if (ImGui::Checkbox("Has Exit Time", &transition->hasExitTime))
        {
            isDirty = true;
        }

        if (transition->hasExitTime)
        {
            if (ImGui::DragFloat("Exit Time", &transition->exitTime, 0.01f, 0.0f, 1.0f))
            {
                isDirty = true;
            }
        }

        if (ImGui::DragInt("Priority", &transition->priority))
        {
            isDirty = true;
        }

        ImGui::Separator();
        ImGui::Text("Conditions:");

        int conditionToRemove = -1;
        for (size_t i = 0; i < transition->conditions.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            drawConditionEditor(transition->conditions[i], animatorData, isDirty);

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                conditionToRemove = static_cast<int>(i);
            }
            ImGui::PopID();
        }

        if (conditionToRemove >= 0)
        {
            transition->conditions.erase(transition->conditions.begin() + conditionToRemove);
            isDirty = true;
        }

        if (ImGui::Button("Add Condition"))
        {
            animator::TransitionCondition cond;
            if (!animatorData->graph.parameters.empty())
            {
                cond.parameterName = animatorData->graph.parameters[0].name;
                cond.op = animator::ComparisonOperator::Equal;
                cond.value = animatorData->graph.parameters[0].defaultValue;
            }
            transition->conditions.push_back(std::move(cond));
            isDirty = true;
        }
    }

    void AnimatorPropertiesPanel::drawConditionEditor(animator::TransitionCondition& condition,
                                                       animator::AnimatorData* animatorData,
                                                       bool& isDirty)
    {
        if (ImGui::BeginCombo("##param", condition.parameterName.c_str()))
        {
            for (const auto& param : animatorData->graph.parameters)
            {
                bool isSelected = (condition.parameterName == param.name);
                if (ImGui::Selectable(param.name.c_str(), isSelected))
                {
                    condition.parameterName = param.name;
                    condition.value = param.defaultValue;
                    isDirty = true;
                }
            }
            ImGui::EndCombo();
        }

        animator::AnimatorParameterType paramType = animator::AnimatorParameterType::Float;
        for (const auto& param : animatorData->graph.parameters)
        {
            if (param.name == condition.parameterName)
            {
                paramType = param.type;
                break;
            }
        }

        ImGui::SameLine();
        const char* opNames[] = {"==", "!=", ">", "<", ">=", "<="};
        int opIndex = static_cast<int>(condition.op);
        ImGui::SetNextItemWidth(50);
        if (ImGui::Combo("##op", &opIndex, opNames, IM_ARRAYSIZE(opNames)))
        {
            condition.op = static_cast<animator::ComparisonOperator>(opIndex);
            isDirty = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);

        switch (paramType)
        {
        case animator::AnimatorParameterType::Float:
            {
                float val = std::get<float>(condition.value);
                if (ImGui::DragFloat("##val", &val, 0.01f))
                {
                    condition.value = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Int:
            {
                int32_t val = std::get<int32_t>(condition.value);
                if (ImGui::DragInt("##val", &val))
                {
                    condition.value = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Bool:
        case animator::AnimatorParameterType::Trigger:
            {
                bool val = std::get<bool>(condition.value);
                if (ImGui::Checkbox("##val", &val))
                {
                    condition.value = val;
                    isDirty = true;
                }
            }
            break;
        }
    }

    void AnimatorPropertiesPanel::drawBlendTreeEditor(animator::AnimatorState* state,
                                                        animator::AnimatorData* animatorData,
                                                        bool& isDirty)
    {
        if (!state)
            return;

        if (!drawMotionTypeCombo(state, isDirty))
            return;

        auto& bt = state->blendTree.value();

        auto drawParamCombo = [&](const char* label, std::string& paramName)
        {
            if (ImGui::BeginCombo(label, paramName.empty() ? "(none)" : paramName.c_str()))
            {
                for (const auto& param : animatorData->graph.parameters)
                {
                    if (param.type != animator::AnimatorParameterType::Float)
                        continue;
                    bool isSelected = (paramName == param.name);
                    if (ImGui::Selectable(param.name.c_str(), isSelected))
                    {
                        paramName = param.name;
                        isDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        };

        if (bt.type == animator::BlendTreeType::BlendTree1D)
        {
            drawParamCombo("Parameter", bt.parameterName);
        }
        else
        {
            drawParamCombo("Parameter X", bt.parameterName);
            drawParamCombo("Parameter Y", bt.parameterNameY);
        }

        ImGui::Separator();
        ImGui::Text("Entries:");

        int entryToRemove = -1;
        for (size_t i = 0; i < bt.entries.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            drawBlendTreeEntry(bt.entries[i], bt.type, isDirty);

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                entryToRemove = static_cast<int>(i);

            ImGui::Separator();
            ImGui::PopID();
        }

        if (entryToRemove >= 0)
        {
            bt.entries.erase(bt.entries.begin() + entryToRemove);
            isDirty = true;
        }

        if (ImGui::Button("Add Entry"))
        {
            animator::BlendTreeEntry entry;
            entry.threshold = bt.entries.empty() ? 0.0f : bt.entries.back().threshold + 1.0f;
            bt.entries.push_back(std::move(entry));
            isDirty = true;
        }
    }

    bool AnimatorPropertiesPanel::drawMotionTypeCombo(animator::AnimatorState* state, bool& isDirty)
    {
        const char* motionTypes[] = {"Single Clip", "1D Blend Tree", "2D Blend Tree"};
        int currentType = 0;
        if (state->blendTree.has_value())
        {
            currentType = state->blendTree->type == animator::BlendTreeType::BlendTree1D ? 1 : 2;
        }

        if (ImGui::Combo("Motion Type", &currentType, motionTypes, IM_ARRAYSIZE(motionTypes)))
        {
            if (currentType == 0)
            {
                state->blendTree.reset();
            }
            else
            {
                if (!state->blendTree.has_value())
                    state->blendTree = animator::BlendTreeData{};

                state->blendTree->type = (currentType == 1)
                    ? animator::BlendTreeType::BlendTree1D
                    : animator::BlendTreeType::BlendTree2D;
            }
            isDirty = true;
        }

        return state->blendTree.has_value();
    }

    void AnimatorPropertiesPanel::drawBlendTreeEntry(animator::BlendTreeEntry& entry,
                                                       animator::BlendTreeType type,
                                                       bool& isDirty)
    {
        if (!entry.animationPath.empty())
        {
            fs::path animPath(entry.animationPath);
            ImGui::Text("%s", animPath.filename().string().c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "(no animation)");
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Browse"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Animation Files (*.vfAnim)", L"*.vfAnim"}});
            if (!path.empty())
            {
                entry.animationPath = path;
                isDirty = true;
            }
        }

        if (type == animator::BlendTreeType::BlendTree1D)
        {
            if (ImGui::DragFloat("Threshold", &entry.threshold, 0.01f))
                isDirty = true;
        }
        else
        {
            float pos[2] = {entry.position.x, entry.position.y};
            if (ImGui::DragFloat2("Position", pos, 0.01f))
            {
                entry.position.x = pos[0];
                entry.position.y = pos[1];
                isDirty = true;
            }
        }
    }

    void AnimatorPropertiesPanel::drawAddParameterPopup(animator::AnimatorData* animatorData,
                                                         bool& showAddParameterPopup,
                                                         std::string& newParameterName,
                                                         animator::AnimatorParameterType& newParameterType,
                                                         bool& isDirty)
    {
        if (showAddParameterPopup)
        {
            ImGui::OpenPopup("Add Parameter");
            showAddParameterPopup = false;
        }

        if (ImGui::BeginPopup("Add Parameter"))
        {
            ImGui::Text("Add New Parameter");
            ImGui::Separator();

            char buffer[256];
            std::strncpy(buffer, newParameterName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            {
                newParameterName = buffer;
            }

            const char* types[] = {"Float", "Int", "Bool", "Trigger"};
            int typeIndex = static_cast<int>(newParameterType);
            if (ImGui::Combo("Type", &typeIndex, types, IM_ARRAYSIZE(types)))
            {
                newParameterType = static_cast<animator::AnimatorParameterType>(typeIndex);
            }

            if (ImGui::Button("Add") && !newParameterName.empty())
            {
                animator::AnimatorParameter param;
                param.name = newParameterName;
                param.type = newParameterType;

                switch (newParameterType)
                {
                case animator::AnimatorParameterType::Float:
                    param.defaultValue = 0.0f;
                    break;
                case animator::AnimatorParameterType::Int:
                    param.defaultValue = 0;
                    break;
                case animator::AnimatorParameterType::Bool:
                case animator::AnimatorParameterType::Trigger:
                    param.defaultValue = false;
                    break;
                }

                animatorData->graph.parameters.push_back(std::move(param));
                newParameterName.clear();
                isDirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                newParameterName.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
