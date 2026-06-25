#include "print/Log.hpp"
#include "UISliderDrawer.hpp"
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
    bool UISliderDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUISliderComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasSlider = dispatcher.query(hasQuery);

        if (!hasSlider)
        {
            return false;
        }

        events::ui::GetUISliderDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UISliderComponent");

        bool removeSlider = false;
        bool isOpen = drawHeader(removeSlider);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UISliderData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Draggable slider for numeric value input");
            ImGui::Spacing();

            changed |= drawValueConfig(data);
            ImGui::Spacing();

            changed |= drawOrientation(data);
            ImGui::Spacing();

            changed |= drawHandleAppearance(data);
            ImGui::Spacing();

            changed |= drawFillAppearance(data);
            ImGui::Spacing();

            changed |= drawConfig(data);
            ImGui::Spacing();

            drawCurrentState(data);

            if (changed)
            {
                events::ui::SetUISliderDataCommand cmd;
                cmd.entity = handle;
                cmd.sliderData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeSlider)
        {
            events::ui::RemoveUISliderComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UISliderDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UISliderHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Slider");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUISlider", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UISliderDrawer::drawValueConfig(services::UISliderData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Value Config##UISlider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::DragFloat("Min Value##UISlider", &data.minValue))
            {
                changed = true;
            }

            if (ImGui::DragFloat("Max Value##UISlider", &data.maxValue))
            {
                changed = true;
            }

            if (ImGui::DragFloat("Value##UISlider", &data.value, 0.01f, data.minValue, data.maxValue))
            {
                changed = true;
            }

            if (ImGui::DragFloat("Step Size##UISlider", &data.stepSize, 0.01f, 0.0f, data.maxValue - data.minValue))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("0 = continuous. Non-zero = snap to increments");
            }

            if (ImGui::Checkbox("Click Track To Set##UISlider", &data.clickTrackToSet))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("When enabled, clicking on the track sets the value directly");
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UISliderDrawer::drawOrientation(services::UISliderData& data)
    {
        bool changed = false;

        const char* orientations[] = {"Horizontal", "Vertical"};
        int currentOrientation = static_cast<int>(data.orientation);

        if (ImGui::Combo("Orientation##UISlider", &currentOrientation, orientations, IM_ARRAYSIZE(orientations)))
        {
            data.orientation = static_cast<uint8_t>(currentOrientation);
            changed = true;
        }

        return changed;
    }

    bool UISliderDrawer::drawHandleAppearance(services::UISliderData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Handle Appearance##UISlider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::DragFloat("Handle Size Ratio##UISlider", &data.handleSizeRatio, 0.01f, 0.01f, 0.5f))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Handle size as a ratio of the track length");
            }

            ImGui::Spacing();

            if (ColorEditRow("Handle Normal##UISlider", &data.handleNormalColor.x))
            {
                changed = true;
            }

            if (ColorEditRow("Handle Hovered##UISlider", &data.handleHoveredColor.x))
            {
                changed = true;
            }

            if (ColorEditRow("Handle Pressed##UISlider", &data.handlePressedColor.x))
            {
                changed = true;
            }

            if (ColorEditRow("Handle Disabled##UISlider", &data.handleDisabledColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Handle Textures");
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawUITextureSlot("Normal", data.handleNormalTextureRef, "UISld_handleNormal");
            changed |= drawUITextureSlot("Hovered", data.handleHoveredTextureRef, "UISld_handleHovered");
            changed |= drawUITextureSlot("Pressed", data.handlePressedTextureRef, "UISld_handlePressed");
            changed |= drawUITextureSlot("Disabled", data.handleDisabledTextureRef, "UISld_handleDisabled");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UISliderDrawer::drawFillAppearance(services::UISliderData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Fill Appearance##UISlider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ColorEditRow("Fill Color##UISlider", &data.fillColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawUITextureSlot("Fill", data.fillTextureRef, "UISld_fill");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UISliderDrawer::drawConfig(services::UISliderData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Transition Duration##UISlider", &data.colorTransitionDuration, 0.01f, 0.0f, 2.0f, "%.2f s"))
        {
            changed = true;
        }

        if (ImGui::Checkbox("Interactable##UISlider", &data.interactable))
        {
            changed = true;
        }

        return changed;
    }

    void UISliderDrawer::drawCurrentState(const services::UISliderData& data)
    {
        const char* stateNames[] = {"Normal", "Hovered", "Pressed", "Disabled"};
        int stateIndex = static_cast<int>(data.currentState);
        const char* stateName = (stateIndex >= 0 && stateIndex <= 3) ? stateNames[stateIndex] : "Unknown";

        ImGui::Text("Current State:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stateName);

        ImGui::Text("Dragging:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", data.isDragging ? "Yes" : "No");

        ImGui::Text("Current Value:");
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f", data.value);
    }
}
