#include "UIDropTargetDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UIDropTargetDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIDropTargetComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasDropTarget = dispatcher.query(hasQuery);

        if (!hasDropTarget)
            return false;

        events::ui::GetUIDropTargetDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("UIDropTargetComponent");

        bool removeDropTarget = false;
        bool isOpen = drawHeader(removeDropTarget);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIDropTargetData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Marks this element as a drop receiver");
            ImGui::Spacing();

            char tagBuffer[256];
            std::strncpy(tagBuffer, data.acceptTag.c_str(), sizeof(tagBuffer));
            tagBuffer[sizeof(tagBuffer) - 1] = '\0';
            if (ImGui::InputText("Accept Tag##UIDropTarget", tagBuffer, sizeof(tagBuffer)))
            {
                data.acceptTag = tagBuffer;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Empty = accept all draggables. Otherwise must match drag tag");

            ImGui::Spacing();

            float color[4] = {data.highlightColor.r, data.highlightColor.g, data.highlightColor.b, data.highlightColor.a};
            if (ImGui::ColorEdit4("Highlight Color##UIDropTarget", color))
            {
                data.highlightColor = glm::vec4(color[0], color[1], color[2], color[3]);
                changed = true;
            }

            ImGui::Spacing();

            if (ImGui::Checkbox("Interactable##UIDropTarget", &data.interactable))
            {
                changed = true;
            }

            if (changed)
            {
                events::ui::SetUIDropTargetDataCommand cmd;
                cmd.entity = handle;
                cmd.dropTargetData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeDropTarget)
        {
            events::ui::RemoveUIDropTargetComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIDropTargetDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIDropTargetHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Drop Target");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIDropTarget", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
