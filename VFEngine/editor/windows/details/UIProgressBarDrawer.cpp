#include "print/Log.hpp"
#include "UIProgressBarDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
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

    static bool drawProgressBarTextureSlot(const char* label, std::string& texturePath, const char* uniqueId)
    {
        bool changed = false;

        ImGui::Text("%s", label);

        if (!texturePath.empty())
        {
            std::string filename = texturePath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", filename.c_str());
        }

        char selectId[64];
        std::snprintf(selectId, sizeof(selectId), "Select##UIPb_%s", uniqueId);
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
                    texturePath = path;
                    changed = true;
                }
                else
                {
                    vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = texturePath.empty();
        if (wasEmpty) ImGui::BeginDisabled();
        char clearId[64];
        std::snprintf(clearId, sizeof(clearId), "Clear##UIPb_%s", uniqueId);
        if (ImGui::Button(clearId))
        {
            texturePath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool UIProgressBarDrawer::drawTrackAppearance(services::UIProgressBarData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Track Appearance##UIProgressBar", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Track Color##UIProgressBar", &data.trackColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawProgressBarTextureSlot("Track", data.trackTexture, "track");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIProgressBarDrawer::drawFillAppearance(services::UIProgressBarData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Fill Appearance##UIProgressBar", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Fill Color##UIProgressBar", &data.fillColor.x))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawProgressBarTextureSlot("Fill", data.fillTexture, "fill");

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
