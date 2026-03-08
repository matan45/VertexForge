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

        // VegetationPlacementPanel is for tree/vegetation scattering (VK-712).
        // It will be wired to its own placement mode when implemented.
        // For now, do not auto-show during grass brush mode.

        subscribed = true;
    }

    void VegetationPlacementPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::Begin("Vegetation Placement", &visible);

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

        // Species selector - placeholder
        ImGui::Separator();
        ImGui::Text("Species");
        ImGui::InputInt("Species ID", &selectedSpecies);

        ImGui::End();
    }
}
