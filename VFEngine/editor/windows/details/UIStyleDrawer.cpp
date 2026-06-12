#include "UIStyleDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIThemeEvents.hpp"
#include <imgui.h>
#include <cstring>

namespace windows::details
{
    bool UIStyleDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIStyleComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
            return false;

        events::ui::GetUIStyleKeyQuery keyQuery;
        keyQuery.entity = handle;
        auto keyOpt = dispatcher.query(keyQuery);

        ImGui::PushID("UIStyleComponent");

        bool removeStyle = false;
        bool isOpen = drawHeader(removeStyle);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            ImGui::TextDisabled("Theme style applied from the canvas theme");
            ImGui::Spacing();

            char buffer[128] = {};
            if (keyOpt.has_value())
            {
                std::strncpy(buffer, keyOpt->c_str(), sizeof(buffer) - 1);
            }
            if (ImGui::InputText("Style Key##UIStyle", buffer, sizeof(buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                events::ui::SetUIStyleKeyCommand cmd;
                cmd.entity = handle;
                cmd.styleKey = buffer;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Style name in the canvas .vfTheme. Press Enter to apply.");

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeStyle)
        {
            events::ui::RemoveUIStyleComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIStyleDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIStyleHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Style");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIStyle", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
