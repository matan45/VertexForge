#include "UITooltipDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UITooltipEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <cstring>

namespace windows::details
{
    bool UITooltipDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUITooltipComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
            return false;

        events::ui::GetUITooltipDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);
        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("UITooltipComponent");

        bool removeTooltip = false;
        bool isOpen = drawHeader(removeTooltip);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UITooltipData data = *dataOpt;
            bool changed = false;

            const char* modes[] = {"Text", "Child Panel"};
            int mode = static_cast<int>(data.mode);
            if (ImGui::Combo("Mode##UITooltip", &mode, modes, 2))
            {
                data.mode = static_cast<uint8_t>(mode);
                changed = true;
            }

            if (ImGui::Checkbox("Enabled##UITooltip", &data.enabled))
            {
                changed = true;
            }

            if (ImGui::DragFloat("Show Delay##UITooltip", &data.showDelay, 0.05f, 0.0f, 5.0f, "%.2fs"))
            {
                changed = true;
            }

            if (data.mode == 0)
            {
                char buffer[512] = {};
                std::strncpy(buffer, data.text.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputTextMultiline("Text##UITooltip", buffer, sizeof(buffer),
                                              ImVec2(-1, 60)))
                {
                    data.text = buffer;
                    changed = true;
                }

                if (ImGui::Checkbox("Follow Cursor##UITooltip", &data.followCursor))
                {
                    changed = true;
                }
                if (ImGui::DragFloat2("Offset##UITooltip", &data.offset.x, 1.0f, -200.0f, 200.0f, "%.0f"))
                {
                    changed = true;
                }
                if (ImGui::DragFloat("Max Width##UITooltip", &data.maxWidth, 1.0f, 40.0f, 1200.0f, "%.0f"))
                {
                    changed = true;
                }
                if (ImGui::ColorEdit4("Background##UITooltip", &data.backgroundColor.x))
                {
                    changed = true;
                }
                if (ImGui::ColorEdit4("Text Color##UITooltip", &data.textColor.x))
                {
                    changed = true;
                }
                if (ImGui::DragFloat("Font Size##UITooltip", &data.fontSize, 0.5f, 6.0f, 96.0f, "%.0f"))
                {
                    changed = true;
                }
                if (ImGui::DragFloat4("Padding##UITooltip", &data.padding.x, 0.5f, 0.0f, 64.0f, "%.0f"))
                {
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Left, right, top, bottom");

                // Font selection
                if (data.fontRef.isValid())
                {
                    std::string filename = data.fontRef.resolve();
                    auto lastSlash = filename.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        filename = filename.substr(lastSlash + 1);
                    ImGui::Text("Font: %s", filename.c_str());
                }
                else
                {
                    ImGui::TextDisabled("No font selected (tooltip text needs one)");
                }
                if (ImGui::Button("Select Font##UITooltip"))
                {
                    nfd::FileDialog fileDialog;
                    std::string path = fileDialog.openFileDialog(
                        {{L"VF Font Files (*.vfFont)", L"*.vfFont"}});
                    if (!path.empty())
                    {
                        data.fontRef = asset::AssetRef::fromPath(path);
                        changed = true;
                    }
                }
            }
            else
            {
                char buffer[128] = {};
                std::strncpy(buffer, data.panelChildName.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText("Panel Child##UITooltip", buffer, sizeof(buffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    data.panelChildName = buffer;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Child entity to show on hover (empty = first inactive child panel)");
            }

            if (changed)
            {
                events::ui::SetUITooltipDataCommand cmd;
                cmd.entity = handle;
                cmd.tooltipData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeTooltip)
        {
            events::ui::RemoveUITooltipComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UITooltipDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UITooltipHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Tooltip");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUITooltip", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
