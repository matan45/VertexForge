#include "PaintToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/PaintModeEvents.hpp"
#include "events/PaintBrushEvents.hpp"
#include <imgui.h>

namespace windows
{
    PaintToolPanel::~PaintToolPanel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (paintModeToken.isValid())
        {
            dispatcher.unsubscribe(paintModeToken);
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

    void PaintToolPanel::subscribe()
    {
        if (subscribed)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                visible = n.isActive;
                if (n.isActive)
                {
                    // Sync with current state
                    auto& d = events::EventDispatcher::instance();
                    auto params = d.query(events::paintBrush::GetPaintBrushParamsQuery{});
                    brushRadius = params.radius;
                    brushStrength = params.strength;
                    brushOpacity = params.opacity;
                    activeLayer = static_cast<int>(params.activeLayer);
                    falloffIndex = static_cast<int>(params.falloff);
                    shapeIndex = static_cast<int>(params.shape);

                    auto type = d.query(events::paintBrush::GetPaintBrushTypeQuery{});
                    selectedBrushType = static_cast<int>(type);
                }
            });

        brushTypeToken = dispatcher.subscribe<events::paintBrush::PaintBrushTypeChangedNotification>(
            [this](const events::paintBrush::PaintBrushTypeChangedNotification& n)
            {
                selectedBrushType = static_cast<int>(n.type);
            });

        brushParamsToken = dispatcher.subscribe<events::paintBrush::PaintBrushParamsChangedNotification>(
            [this](const events::paintBrush::PaintBrushParamsChangedNotification& n)
            {
                brushRadius = n.params.radius;
                brushStrength = n.params.strength;
                brushOpacity = n.params.opacity;
                activeLayer = static_cast<int>(n.params.activeLayer);
                falloffIndex = static_cast<int>(n.params.falloff);
                shapeIndex = static_cast<int>(n.params.shape);
            });

        subscribed = true;
    }

    void PaintToolPanel::draw()
    {
        subscribe();

        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Paint Tools", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Brush Type");
        ImGui::Separator();

        const char* brushLabels[] = {"Paint", "Erase", "Smooth", "Fill"};
        bool typeChanged = false;

        for (int i = 0; i < 4; ++i)
        {
            if (ImGui::RadioButton(brushLabels[i], &selectedBrushType, i))
            {
                typeChanged = true;
            }
            if (i < 3)
            {
                ImGui::SameLine();
            }
        }

        if (typeChanged)
        {
            events::paintBrush::SetPaintBrushTypeCommand cmd;
            cmd.type = static_cast<terrain::PaintBrushType>(selectedBrushType);
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        ImGui::Text("Layer Selection");
        ImGui::Separator();

        if (ImGui::SliderInt("Layer", &activeLayer, 0, 3))
        {
            events::paintBrush::SetPaintActiveLayerCommand cmd;
            cmd.layer = static_cast<uint32_t>(activeLayer);
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        ImGui::Text("Brush Parameters");
        ImGui::Separator();

        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f, "%.1f"))
        {
            events::paintBrush::SetPaintBrushRadiusCommand cmd;
            cmd.radius = brushRadius;
            dispatcher.execute(cmd);
        }

        if (ImGui::SliderFloat("Strength", &brushStrength, 0.0f, 100.0f, "%.2f"))
        {
            events::paintBrush::SetPaintBrushStrengthCommand cmd;
            cmd.strength = brushStrength;
            dispatcher.execute(cmd);
        }

        if (ImGui::SliderFloat("Opacity", &brushOpacity, 0.0f, 1.0f, "%.2f"))
        {
            events::paintBrush::SetPaintBrushOpacityCommand cmd;
            cmd.opacity = brushOpacity;
            dispatcher.execute(cmd);
        }

        const char* falloffLabels[] = {"Constant", "Linear", "Smooth", "Sharp"};
        if (ImGui::Combo("Falloff", &falloffIndex, falloffLabels, 4))
        {
            events::paintBrush::SetPaintBrushFalloffCommand cmd;
            cmd.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
            dispatcher.execute(cmd);
        }

        const char* shapeLabels[] = {"Circle", "Square"};
        if (ImGui::Combo("Shape", &shapeIndex, shapeLabels, 2))
        {
            events::paintBrush::SetPaintBrushShapeCommand cmd;
            cmd.shape = static_cast<terrain::BrushShape>(shapeIndex);
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Left-click to paint");
        ImGui::TextDisabled("Hold Shift to erase");

        ImGui::End();

        // If user closed the panel, deactivate paint mode
        if (!visible)
        {
            events::paint::SetPaintModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
    }
}
