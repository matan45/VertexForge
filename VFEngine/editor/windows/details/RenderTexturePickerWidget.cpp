#include "RenderTexturePickerWidget.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool RenderTexturePickerWidget::draw(const char* imguiId,
                                          std::string& renderTextureSourceName,
                                          services::EntityHandle& renderTextureSource,
                                          services::ComponentTypeId filter,
                                          const char* comboLabel,
                                          const char* tooltip)
    {
        bool changed = false;

        if (needsRefresh)
        {
            refresh(filter);
            needsRefresh = false;
            syncSelection(renderTextureSourceName);
        }

        const char* preview = selectedIdx >= 0 && selectedIdx < static_cast<int>(candidateNames.size())
            ? candidateNames[selectedIdx].c_str()
            : "None";

        ImGui::PushID(imguiId);

        // Keep the combo + its label + the Refresh button on one line even in a narrow pane
        // (e.g. the UI Layer Builder's inspector): shrink the combo to leave room for the label
        // and button so "Refresh" isn't clipped. Responsive — no change in a wide panel.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float refreshWidth = ImGui::CalcTextSize("Refresh").x + style.FramePadding.x * 2.0f;
        const float labelWidth = (comboLabel && comboLabel[0] != '\0')
            ? ImGui::CalcTextSize(comboLabel, nullptr, true).x + style.ItemInnerSpacing.x
            : 0.0f;
        float comboWidth = ImGui::GetContentRegionAvail().x - labelWidth - style.ItemSpacing.x - refreshWidth;
        if (comboWidth < 80.0f) comboWidth = 80.0f;
        ImGui::SetNextItemWidth(comboWidth);

        if (ImGui::BeginCombo(comboLabel, preview))
        {
            // "None" option
            if (ImGui::Selectable("None##RTTPicker", selectedIdx < 0))
            {
                selectedIdx = -1;
                renderTextureSourceName = "";
                renderTextureSource = services::EntityHandle::invalid();
                changed = true;
            }

            for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
            {
                bool isSelected = (i == selectedIdx);
                if (ImGui::Selectable(candidateNames[i].c_str(), isSelected))
                {
                    selectedIdx = i;
                    renderTextureSourceName = candidateNames[i];
                    renderTextureSource = candidates[i];
                    changed = true;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered() && tooltip)
        {
            ImGui::SetTooltip("%s", tooltip);
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh##RTTPicker"))
        {
            refresh(filter);
            syncSelection(renderTextureSourceName);
        }

        ImGui::PopID();

        return changed;
    }

    void RenderTexturePickerWidget::refresh(services::ComponentTypeId filter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        candidates.clear();
        candidateNames.clear();
        selectedIdx = -1;

        events::scene::GetEntitiesWithComponentQuery compQuery;
        compQuery.componentType = filter;
        candidates = dispatcher.query(compQuery);

        for (const auto& candidate : candidates)
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = candidate;
            auto entityDataOpt = dispatcher.query(entityQuery);

            if (entityDataOpt.has_value() && !entityDataOpt->name.empty())
            {
                candidateNames.push_back(entityDataOpt->name);
            }
            else
            {
                candidateNames.push_back("Entity #" + std::to_string(candidate.id));
            }
        }
    }

    void RenderTexturePickerWidget::syncSelection(const std::string& currentName)
    {
        selectedIdx = -1;
        if (!currentName.empty())
        {
            for (int i = 0; i < static_cast<int>(candidateNames.size()); ++i)
            {
                if (candidateNames[i] == currentName)
                {
                    selectedIdx = i;
                    break;
                }
            }
        }
    }
}
