#pragma once

#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"

#include <cstring>
#include <imgui.h>

namespace editor::windows::bt
{
    inline behaviortree::BlackboardValue defaultValueForType(behaviortree::BlackboardValueType type)
    {
        switch (type)
        {
        case behaviortree::BlackboardValueType::Float: return 0.0f;
        case behaviortree::BlackboardValueType::Int: return int32_t{0};
        case behaviortree::BlackboardValueType::Bool: return false;
        case behaviortree::BlackboardValueType::String: return std::string{};
        case behaviortree::BlackboardValueType::Vec3: return glm::vec3{0.0f};
        case behaviortree::BlackboardValueType::Entity: return services::EntityHandle::invalid();
        default: return 0.0f;
        }
    }

    inline behaviortree::BlackboardValueType typeOfValue(const behaviortree::BlackboardValue& value)
    {
        return static_cast<behaviortree::BlackboardValueType>(value.index());
    }

    inline behaviortree::BlackboardValueType resolveKeyType(const behaviortree::BTGraph* graph,
                                                            const std::string& keyName)
    {
        if (graph)
        {
            for (const auto& keyDef : graph->blackboardKeys)
            {
                if (keyDef.name == keyName)
                {
                    return keyDef.type;
                }
            }
        }
        return behaviortree::BlackboardValueType::Float;
    }

    inline bool drawBlackboardValueWidget(const char* label,
                                          behaviortree::BlackboardValueType type,
                                          behaviortree::BlackboardValue& value)
    {
        behaviortree::BlackboardValue editValue =
            typeOfValue(value) == type ? value : defaultValueForType(type);

        switch (type)
        {
        case behaviortree::BlackboardValueType::Float:
        {
            float v = std::holds_alternative<float>(editValue) ? std::get<float>(editValue) : 0.0f;
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::DragFloat(label, &v, 0.1f))
            {
                value = v;
                return true;
            }
            return false;
        }
        case behaviortree::BlackboardValueType::Int:
        {
            int v = std::holds_alternative<int32_t>(editValue) ? std::get<int32_t>(editValue) : 0;
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::DragInt(label, &v))
            {
                value = static_cast<int32_t>(v);
                return true;
            }
            return false;
        }
        case behaviortree::BlackboardValueType::Bool:
        {
            bool v = std::holds_alternative<bool>(editValue) && std::get<bool>(editValue);
            if (ImGui::Checkbox(label, &v))
            {
                value = v;
                return true;
            }
            return false;
        }
        case behaviortree::BlackboardValueType::String:
        {
            std::string text = std::holds_alternative<std::string>(editValue)
                                   ? std::get<std::string>(editValue)
                                   : std::string{};
            char buffer[128];
            std::strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText(label, buffer, sizeof(buffer)))
            {
                value = std::string(buffer);
                return true;
            }
            return false;
        }
        case behaviortree::BlackboardValueType::Vec3:
        {
            glm::vec3 v = std::holds_alternative<glm::vec3>(editValue)
                              ? std::get<glm::vec3>(editValue)
                              : glm::vec3{0.0f};
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::DragFloat3(label, &v.x, 0.1f))
            {
                value = v;
                return true;
            }
            return false;
        }
        case behaviortree::BlackboardValueType::Entity:
        {
            const auto handle = std::holds_alternative<services::EntityHandle>(editValue)
                                    ? std::get<services::EntityHandle>(editValue)
                                    : services::EntityHandle::invalid();
            if (label && label[0] == '#' && label[1] == '#')
            {
                ImGui::Text("entity %llu", static_cast<unsigned long long>(handle.id));
            }
            else
            {
                ImGui::Text("%s: entity %llu", label, static_cast<unsigned long long>(handle.id));
            }
            return false;
        }
        default:
            return false;
        }
    }
}
