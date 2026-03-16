#include "print/Log.hpp"
#include "UISliderDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
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

    static bool drawSliderTextureSlot(const char* label, asset::AssetRef& textureRef, const char* uniqueId)
    {
        bool changed = false;

        ImGui::Text("%s", label);

        if (textureRef.isValid())
        {
            std::string filename = textureRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", filename.c_str());
        }

        char selectId[64];
        std::snprintf(selectId, sizeof(selectId), "Select##UISld_%s", uniqueId);
        if (ImGui::Button(selectId))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    textureRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
                else
                {
                    vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = !textureRef.isValid();
        if (wasEmpty) ImGui::BeginDisabled();
        char clearId[64];
        std::snprintf(clearId, sizeof(clearId), "Clear##UISld_%s", uniqueId);
        if (ImGui::Button(clearId))
        {
            textureRef = asset::AssetRef::invalid();
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

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

            if (ImGui::ColorEdit4("Handle Normal##UISlider", &data.handleNormalColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Handle Hovered##UISlider", &data.handleHoveredColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Handle Pressed##UISlider", &data.handlePressedColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Handle Disabled##UISlider", &data.handleDisabledColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Handle Textures");
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawSliderTextureSlot("Normal", data.handleNormalTextureRef, "handleNormal");
            changed |= drawSliderTextureSlot("Hovered", data.handleHoveredTextureRef, "handleHovered");
            changed |= drawSliderTextureSlot("Pressed", data.handlePressedTextureRef, "handlePressed");
            changed |= drawSliderTextureSlot("Disabled", data.handleDisabledTextureRef, "handleDisabled");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UISliderDrawer::drawFillAppearance(services::UISliderData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Fill Appearance##UISlider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Fill Color##UISlider", &data.fillColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawSliderTextureSlot("Fill", data.fillTextureRef, "fill");

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
