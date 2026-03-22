#include "CaveToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/CaveModeEvents.hpp"
#include "events/terrain/CaveBrushEvents.hpp"
#include <imgui.h>

namespace windows
{
    CaveToolPanel::~CaveToolPanel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (caveModeToken.isValid())
        {
            dispatcher.unsubscribe(caveModeToken);
        }
        if (brushTypeToken.isValid())
        {
            dispatcher.unsubscribe(brushTypeToken);
        }
        if (brushParamsToken.isValid())
        {
            dispatcher.unsubscribe(brushParamsToken);
        }
    }

    void CaveToolPanel::subscribe()
    {
        if (subscribed)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        caveModeToken = dispatcher.subscribe<events::cave::CaveModeChangedNotification>(
            [this](const events::cave::CaveModeChangedNotification& n)
            {
                visible = n.isActive;
                if (n.isActive)
                {
                    // Sync with current state
                    auto& d = events::EventDispatcher::instance();
                    auto params = d.query(events::caveBrush::GetCaveBrushParamsQuery{});
                    brushRadius = params.radius;
                    brushStrength = params.strength;
                    falloffIndex = static_cast<int>(params.falloff);
                    shapeIndex = static_cast<int>(params.shape);

                    auto type = d.query(events::caveBrush::GetCaveBrushTypeQuery{});
                    selectedBrushType = static_cast<int>(type);
                }
            });

        brushTypeToken = dispatcher.subscribe<events::caveBrush::CaveBrushTypeChangedNotification>(
            [this](const events::caveBrush::CaveBrushTypeChangedNotification& n)
            {
                selectedBrushType = static_cast<int>(n.type);
            });

        brushParamsToken = dispatcher.subscribe<events::caveBrush::CaveBrushParamsChangedNotification>(
            [this](const events::caveBrush::CaveBrushParamsChangedNotification& n)
            {
                brushRadius = n.params.radius;
                brushStrength = n.params.strength;
                falloffIndex = static_cast<int>(n.params.falloff);
                shapeIndex = static_cast<int>(n.params.shape);
            });

        subscribed = true;
    }

    void CaveToolPanel::draw()
    {
        subscribe();

        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Cave Tools", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        drawBrushTypeSelector(dispatcher);
        drawBrushParams(dispatcher);

        ImGui::End();

        // If user closed the panel, deactivate cave mode
        if (!visible)
        {
            events::cave::SetCaveModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
    }

    void CaveToolPanel::drawBrushTypeSelector(events::EventDispatcher& dispatcher)
    {
        ImGui::Text("Brush Type");
        ImGui::Separator();

        const char* brushLabels[] = {"Carve", "Fill", "Smooth"};
        bool typeChanged = false;

        for (int i = 0; i < 3; ++i)
        {
            if (ImGui::RadioButton(brushLabels[i], &selectedBrushType, i))
            {
                typeChanged = true;
            }
            if (i < 2)
            {
                ImGui::SameLine();
            }
        }

        if (typeChanged)
        {
            events::caveBrush::SetCaveBrushTypeCommand cmd;
            cmd.type = static_cast<terrain::CaveBrushType>(selectedBrushType);
            dispatcher.execute(cmd);
        }
    }

    void CaveToolPanel::drawBrushParams(events::EventDispatcher& dispatcher)
    {
        ImGui::Spacing();
        ImGui::Text("Brush Parameters");
        ImGui::Separator();

        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f, "%.1f"))
        {
            events::caveBrush::SetCaveBrushRadiusCommand cmd;
            cmd.radius = brushRadius;
            dispatcher.execute(cmd);
        }

        if (ImGui::SliderFloat("Strength", &brushStrength, 0.0f, 100.0f, "%.2f"))
        {
            events::caveBrush::SetCaveBrushStrengthCommand cmd;
            cmd.strength = brushStrength;
            dispatcher.execute(cmd);
        }

        const char* falloffLabels[] = {"Constant", "Linear", "Smooth", "Sharp"};
        if (ImGui::Combo("Falloff", &falloffIndex, falloffLabels, 4))
        {
            events::caveBrush::SetCaveBrushFalloffCommand cmd;
            cmd.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
            dispatcher.execute(cmd);
        }

        const char* shapeLabels[] = {"Circle", "Square"};
        if (ImGui::Combo("Shape", &shapeIndex, shapeLabels, 2))
        {
            events::caveBrush::SetCaveBrushShapeCommand cmd;
            cmd.shape = static_cast<terrain::BrushShape>(shapeIndex);
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Left-click to carve/fill");
        ImGui::TextDisabled("Hold Shift to invert");
    }
}
