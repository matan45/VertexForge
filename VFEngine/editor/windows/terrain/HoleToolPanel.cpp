#include "HoleToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/HoleModeEvents.hpp"
#include "events/terrain/HoleBrushEvents.hpp"
#include <imgui.h>

namespace windows
{
    HoleToolPanel::~HoleToolPanel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (holeModeToken.isValid())
        {
            dispatcher.unsubscribe(holeModeToken);
        }
        if (holeParamsToken.isValid())
        {
            dispatcher.unsubscribe(holeParamsToken);
        }
    }

    void HoleToolPanel::subscribe()
    {
        if (subscribed)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                visible = n.isActive;
                if (n.isActive)
                {
                    auto& d = events::EventDispatcher::instance();
                    auto params = d.query(events::holeBrush::GetHoleBrushParamsQuery{});
                    brushRadius = params.radius;
                    falloffIndex = static_cast<int>(params.falloff);
                    shapeIndex = static_cast<int>(params.shape);
                }
            });

        holeParamsToken = dispatcher.subscribe<events::holeBrush::HoleBrushParamsChangedNotification>(
            [this](const events::holeBrush::HoleBrushParamsChangedNotification& n)
            {
                brushRadius = n.params.radius;
                falloffIndex = static_cast<int>(n.params.falloff);
                shapeIndex = static_cast<int>(n.params.shape);
            });

        subscribed = true;
    }

    void HoleToolPanel::draw()
    {
        subscribe();

        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Hole Tool", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Brush Parameters");
        ImGui::Separator();

        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f, "%.1f"))
        {
            events::holeBrush::SetHoleBrushRadiusCommand cmd;
            cmd.radius = brushRadius;
            dispatcher.execute(cmd);
        }

        const char* falloffLabels[] = {"Constant", "Linear", "Smooth", "Sharp"};
        if (ImGui::Combo("Falloff", &falloffIndex, falloffLabels, 4))
        {
            events::holeBrush::SetHoleBrushFalloffCommand cmd;
            cmd.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
            dispatcher.execute(cmd);
        }

        const char* shapeLabels[] = {"Circle"};
        ImGui::BeginDisabled(true);
        if (ImGui::Combo("Shape", &shapeIndex, shapeLabels, 1))
        {
            events::holeBrush::SetHoleBrushShapeCommand cmd;
            cmd.shape = static_cast<terrain::BrushShape>(shapeIndex);
            dispatcher.execute(cmd);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Left-click: Create hole");
        ImGui::TextDisabled("Shift+click: Fill hole");

        ImGui::End();

        if (!visible)
        {
            events::hole::SetHoleModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
    }
}
