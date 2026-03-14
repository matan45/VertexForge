#include "UIMaskDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UIMaskDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIMaskComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasMask = dispatcher.query(hasQuery);

        if (!hasMask)
            return false;

        events::ui::GetUIMaskDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("UIMaskComponent");

        bool removeMask = false;
        bool isOpen = drawHeader(removeMask);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIMaskData data = *dataOpt;
            bool changed = false;

            // Mask mode combo
            const char* maskModeNames[] = {"Rectangle", "Alpha Texture"};
            int currentMode = static_cast<int>(data.maskMode);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Mask Mode", &currentMode, maskModeNames, 2))
            {
                data.maskMode = static_cast<uint8_t>(currentMode);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rectangle: clips to rect bounds\nAlpha Texture: clips using texture alpha channel");

            // Alpha threshold (only for AlphaTexture mode)
            if (data.maskMode == 1) // AlphaTexture
            {
                ImGui::Spacing();

                // Mask texture path
                char texBuf[256];
                std::strncpy(texBuf, data.maskTexturePath.c_str(), sizeof(texBuf));
                texBuf[sizeof(texBuf) - 1] = '\0';
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
                if (ImGui::InputText("Mask Texture##UIMask", texBuf, sizeof(texBuf)))
                {
                    data.maskTexturePath = texBuf;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Path to .vfImage texture used as alpha mask");

                if (ImGui::SliderFloat("Alpha Threshold##UIMask", &data.alphaThreshold, 0.0f, 1.0f, "%.2f"))
                {
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pixels with alpha below this value will not be masked");
            }

            ImGui::Spacing();

            // Show mask graphic checkbox
            if (ImGui::Checkbox("Show Mask Graphic##UIMask", &data.showMaskGraphic))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("When enabled, the mask shape itself is visible");

            if (changed)
            {
                events::ui::SetUIMaskDataCommand cmd;
                cmd.entity = handle;
                cmd.maskData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeMask)
        {
            events::ui::RemoveUIMaskComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIMaskDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIMaskHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Mask");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIMask", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
