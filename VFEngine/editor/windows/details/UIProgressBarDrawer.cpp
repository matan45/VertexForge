#include "print/Log.hpp"
#include "UIProgressBarDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "DrawerHelpers.hpp"
#include "UIDrawerCommon.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool UIProgressBarDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIProgressBarComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasProgressBar = dispatcher.query(hasQuery);

        if (!hasProgressBar)
        {
            return false;
        }

        events::ui::GetUIProgressBarDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIProgressBarComponent");

        bool removeProgressBar = false;
        bool isOpen = drawHeader(removeProgressBar);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIProgressBarData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Non-interactive bar displaying progress");
            ImGui::Spacing();

            changed |= drawValueConfig(data);
            ImGui::Spacing();

            changed |= drawOrientation(data);
            ImGui::Spacing();

            changed |= drawTrackAppearance(data);
            ImGui::Spacing();

            changed |= drawFillAppearance(data);
            ImGui::Spacing();

            changed |= drawInterpolation(data);
            ImGui::Spacing();

            drawCurrentState(data);

            if (changed)
            {
                events::ui::SetUIProgressBarDataCommand cmd;
                cmd.entity = handle;
                cmd.progressBarData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeProgressBar)
        {
            events::ui::RemoveUIProgressBarComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIProgressBarDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIProgressBarHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Progress Bar");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIProgressBar", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIProgressBarDrawer::drawValueConfig(services::UIProgressBarData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Value Config##UIProgressBar", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::DragFloat("Min Value##UIProgressBar", &data.minValue))
            {
                changed = true;
            }

            if (ImGui::DragFloat("Max Value##UIProgressBar", &data.maxValue))
            {
                changed = true;
            }

            if (ImGui::DragFloat("Value##UIProgressBar", &data.value, 0.01f, data.minValue, data.maxValue))
            {
                changed = true;
            }

            if (ImGui::Checkbox("Invert Direction##UIProgressBar", &data.invertDirection))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("When enabled, the fill direction is reversed");
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIProgressBarDrawer::drawOrientation(services::UIProgressBarData& data)
    {
        bool changed = false;

        const char* orientations[] = {"Horizontal", "Vertical"};
        int currentOrientation = static_cast<int>(data.orientation);

        if (ImGui::Combo("Orientation##UIProgressBar", &currentOrientation, orientations, IM_ARRAYSIZE(orientations)))
        {
            data.orientation = static_cast<uint8_t>(currentOrientation);
            changed = true;
        }

        return changed;
    }

    bool UIProgressBarDrawer::drawTrackAppearance(services::UIProgressBarData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Track Appearance##UIProgressBar", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ColorEditRow("Track Color##UIProgressBar", &data.trackColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawUITextureSlot("Track", data.trackTextureRef, "UIPb_track");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIProgressBarDrawer::drawFillAppearance(services::UIProgressBarData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Fill Appearance##UIProgressBar", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ColorEditRow("Fill Color##UIProgressBar", &data.fillColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawUITextureSlot("Fill", data.fillTextureRef, "UIPb_fill");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIProgressBarDrawer::drawInterpolation(services::UIProgressBarData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Interpolation##UIProgressBar", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("Smooth Interpolation##UIProgressBar", &data.smoothInterpolation))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("When enabled, value changes are smoothly animated");
            }

            if (data.smoothInterpolation)
            {
                if (ImGui::DragFloat("Interpolation Speed##UIProgressBar", &data.interpolationSpeed, 0.1f, 0.1f, 50.0f))
                {
                    changed = true;
                }
            }

            ImGui::TreePop();
        }

        return changed;
    }

    void UIProgressBarDrawer::drawCurrentState(const services::UIProgressBarData& data)
    {
        ImGui::Text("Display Value:");
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f", data.displayValue);
    }
}
