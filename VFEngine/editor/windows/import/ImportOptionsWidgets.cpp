#include "ImportOptionsWidgets.hpp"
#include <imgui.h>
#include <variant>

namespace windows::importui
{
    bool drawImportOptions(const std::vector<import::ImportOptionDesc>& descs,
                           std::map<std::string, importConfig::ImportOptionValue>& values)
    {
        bool changed = false;

        for (const auto& desc : descs)
        {
            // try_emplace seeds the declared default the first time this option is
            // seen and leaves an already-chosen (or sidecar-loaded) value alone.
            auto [stored, inserted] = values.try_emplace(desc.key, desc.defaultValue);

            switch (desc.type)
            {
                case import::ImportOptionDesc::Type::Bool:
                {
                    bool value = std::holds_alternative<bool>(stored->second) &&
                                 std::get<bool>(stored->second);
                    if (ImGui::Checkbox(desc.label.c_str(), &value))
                    {
                        stored->second = value;
                        changed = true;
                    }
                    break;
                }
                case import::ImportOptionDesc::Type::Int:
                {
                    int value = std::holds_alternative<int32_t>(stored->second)
                                ? std::get<int32_t>(stored->second) : 0;
                    if (ImGui::SliderInt(desc.label.c_str(), &value,
                                         static_cast<int>(desc.minValue), static_cast<int>(desc.maxValue)))
                    {
                        stored->second = static_cast<int32_t>(value);
                        changed = true;
                    }
                    break;
                }
                case import::ImportOptionDesc::Type::Float:
                {
                    float value = std::holds_alternative<float>(stored->second)
                                  ? std::get<float>(stored->second) : 0.0f;
                    if (ImGui::SliderFloat(desc.label.c_str(), &value, desc.minValue, desc.maxValue))
                    {
                        stored->second = value;
                        changed = true;
                    }
                    break;
                }
                case import::ImportOptionDesc::Type::Enum:
                {
                    int value = std::holds_alternative<int32_t>(stored->second)
                                ? std::get<int32_t>(stored->second) : 0;
                    std::vector<const char*> names;
                    names.reserve(desc.enumNames.size());
                    for (const auto& name : desc.enumNames)
                        names.push_back(name.c_str());
                    if (ImGui::Combo(desc.label.c_str(), &value, names.data(),
                                     static_cast<int>(names.size())))
                    {
                        stored->second = static_cast<int32_t>(value);
                        changed = true;
                    }
                    break;
                }
            }

            if (!desc.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", desc.tooltip.c_str());
        }

        return changed;
    }
}
