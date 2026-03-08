#include "GrassDensityPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/vegetation/GrassEvents.hpp"
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

        ImGui::Separator();
        drawGrassConfigSection();

        ImGui::End();
    }

    void GrassDensityPanel::drawGrassConfigSection()
    {
        if (!configLoaded)
        {
            grassConfig = events::EventDispatcher::instance().query(
                events::vegetation::GetGlobalGrassConfigQuery{});
            configLoaded = true;
        }

        if (!ImGui::CollapsingHeader("Grass Appearance", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        bool changed = false;

        changed |= ImGui::DragFloat("Height Min", &grassConfig.heightMin, 0.01f, 0.01f, 10.0f);
        changed |= ImGui::DragFloat("Height Max", &grassConfig.heightMax, 0.01f, 0.01f, 10.0f);
        changed |= ImGui::DragFloat("Width Min", &grassConfig.widthMin, 0.005f, 0.005f, 2.0f);
        changed |= ImGui::DragFloat("Width Max", &grassConfig.widthMax, 0.005f, 0.005f, 2.0f);

        ImGui::Spacing();
        changed |= ImGui::ColorEdit4("Base Color", &grassConfig.baseColor.x);
        changed |= ImGui::ColorEdit4("Tip Color", &grassConfig.tipColor.x);

        ImGui::Spacing();
        changed |= ImGui::DragFloat("Slope Limit", &grassConfig.slopeLimit, 0.01f, 0.0f, 1.0f,
                                     "%.2f", ImGuiSliderFlags_None);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum surface normal.y for grass placement (0=vertical, 1=flat)");
        changed |= ImGui::DragFloat("Density Multiplier", &grassConfig.densityMultiplier, 0.1f, 0.1f, 10.0f);

        ImGui::Spacing();
        changed |= ImGui::DragFloat("Fade Start", &grassConfig.fadeStartDistance, 1.0f, 1.0f, 500.0f);
        changed |= ImGui::DragFloat("Fade End", &grassConfig.fadeEndDistance, 1.0f, 1.0f, 500.0f);

        if (ImGui::CollapsingHeader("Wind"))
        {
            changed |= ImGui::DragFloat3("Direction", &grassConfig.windDirection.x, 0.01f, -1.0f, 1.0f);
            changed |= ImGui::DragFloat("Speed", &grassConfig.windSpeed, 0.1f, 0.0f, 20.0f);
            changed |= ImGui::DragFloat("Strength", &grassConfig.windStrength, 0.1f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat("Gust Strength", &grassConfig.gustStrength, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Gust Frequency", &grassConfig.gustFrequency, 0.1f, 0.0f, 5.0f);
        }

        if (changed)
        {
            pushGrassConfig();
        }
    }

    void GrassDensityPanel::pushGrassConfig()
    {
        events::vegetation::SetGlobalGrassConfigCommand cmd;
        cmd.config = grassConfig;
        events::EventDispatcher::instance().execute(cmd);
    }
}
