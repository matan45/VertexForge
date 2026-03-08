#include "VegetationPlacementPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/vegetation/VegetationEvents.hpp"
#include <imgui.h>

namespace windows
{
    VegetationPlacementPanel::~VegetationPlacementPanel()
    {
        if (subscribed)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.unsubscribe(modeToken);
        }
    }

    void VegetationPlacementPanel::subscribe()
    {
        if (subscribed) return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::vegetationBrush::VegetationPlacementModeChangedNotification>(
            [this](const auto& n)
            {
                visible = n.isActive;
            });

        subscribed = true;
    }

    void VegetationPlacementPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Vegetation Placement", &visible))
        {
            ImGui::End();
            return;
        }

        // Brush type
        const char* brushTypes[] = {"Scatter", "Erase"};
        if (ImGui::Combo("Tool", &selectedBrushType, brushTypes, IM_ARRAYSIZE(brushTypes)))
        {
            events::vegetationBrush::SetPlacementBrushTypeCommand cmd;
            cmd.type = static_cast<vegetation::PlacementBrushType>(selectedBrushType);
            events::EventDispatcher::instance().execute(cmd);
        }

        ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f);
        ImGui::SliderFloat("Density", &density, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Scale");
        ImGui::SliderFloat("Min Scale", &minScale, 0.1f, 3.0f);
        ImGui::SliderFloat("Max Scale", &maxScale, 0.1f, 3.0f);
        ImGui::SliderFloat("Random Rotation", &randomRotation, 0.0f, 1.0f);

        // Species selector
        ImGui::Separator();
        ImGui::Text("Species");
        ImGui::InputInt("Species ID", &selectedSpecies);

        ImGui::End();

        if (!visible)
        {
            events::vegetationBrush::SetVegetationPlacementModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }
}
