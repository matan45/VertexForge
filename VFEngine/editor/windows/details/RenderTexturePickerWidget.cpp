#include "RenderTexturePickerWidget.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool RenderTexturePickerWidget::draw(const char* imguiId,
                                          std::string& renderTextureSourceName,
                                          services::EntityHandle& renderTextureSource)
    {
        bool changed = false;

        if (needsRefresh)
        {
            refresh();
            needsRefresh = false;
            syncSelection(renderTextureSourceName);
        }

        const char* preview = selectedIdx >= 0 && selectedIdx < static_cast<int>(candidateNames.size())
            ? candidateNames[selectedIdx].c_str()
            : "None";

        ImGui::PushID(imguiId);

        if (ImGui::BeginCombo("RTT Source", preview))
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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Entity with RenderTextureComponent + CameraComponent.\n"
                              "Displays camera feed during play mode.");
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh##RTTPicker"))
        {
            refresh();
            syncSelection(renderTextureSourceName);
        }

        ImGui::PopID();

        return changed;
    }

    void RenderTexturePickerWidget::refresh()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        candidates.clear();
        candidateNames.clear();
        selectedIdx = -1;

        events::scene::GetEntitiesWithComponentQuery compQuery;
        compQuery.componentType = services::ComponentTypeId::RenderTexture;
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
