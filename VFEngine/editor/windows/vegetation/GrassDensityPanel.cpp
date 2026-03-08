#include "GrassDensityPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include <imgui.h>

namespace windows
{
    GrassDensityPanel::~GrassDensityPanel()
    {
        if (subscribed)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.unsubscribe(modeToken);
            dispatcher.unsubscribe(brushTypeToken);
            dispatcher.unsubscribe(brushParamsToken);
        }
    }

    void GrassDensityPanel::subscribe()
    {
        if (subscribed) return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const auto& n)
            {
                visible = n.isActive;
            });

        subscribed = true;
    }

    void GrassDensityPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::Begin("Grass Density Brush", &visible);

        // Brush type
        const char* brushTypes[] = {"Paint", "Erase", "Smooth", "Fill"};
        if (ImGui::Combo("Brush Type", &selectedBrushType, brushTypes, IM_ARRAYSIZE(brushTypes)))
        {
            events::vegetationBrush::SetDensityBrushTypeCommand cmd;
            cmd.type = static_cast<vegetation::DensityBrushType>(selectedBrushType);
            events::EventDispatcher::instance().execute(cmd);
        }

        // Radius
        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f))
        {
            vegetation::DensityBrushParams params;
            params.radius = brushRadius;
            params.strength = brushStrength;
            params.opacity = brushOpacity;
            params.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
            params.shape = static_cast<terrain::BrushShape>(shapeIndex);

            events::vegetationBrush::SetDensityBrushParamsCommand cmd;
            cmd.params = params;
            events::EventDispatcher::instance().execute(cmd);
        }

        // Strength
        ImGui::SliderFloat("Strength", &brushStrength, 0.0f, 100.0f);

        // Opacity
        ImGui::SliderFloat("Opacity", &brushOpacity, 0.0f, 1.0f);

        // Falloff
        const char* falloffTypes[] = {"Constant", "Linear", "Smooth", "Sharp"};
        ImGui::Combo("Falloff", &falloffIndex, falloffTypes, IM_ARRAYSIZE(falloffTypes));

        ImGui::End();
    }
}
