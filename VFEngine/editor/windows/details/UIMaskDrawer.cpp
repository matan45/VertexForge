#include "UIMaskDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
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

            ImGui::TextDisabled("Clips children using texture alpha channel");
            ImGui::Spacing();

            // Display current texture
            if (data.maskTextureRef.isValid())
            {
                std::string filename = data.maskTextureRef.resolve();
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("Mask: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No mask texture selected");
            }

            if (ImGui::Button("Select Mask Texture##UIMask"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
                if (!path.empty())
                {
                    data.maskTextureRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
            }

            ImGui::SameLine();
            bool noTex = !data.maskTextureRef.isValid();
            if (noTex) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##UIMaskTex"))
            {
                data.maskTextureRef = asset::AssetRef::invalid();
                changed = true;
            }
            if (noTex) ImGui::EndDisabled();

            ImGui::Spacing();

            if (ImGui::SliderFloat("Alpha Threshold##UIMask", &data.alphaThreshold, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Pixels with alpha below this value will not write to stencil");

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
