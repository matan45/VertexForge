#include "UIDraggableDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UIDraggableDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIDraggableComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasDraggable = dispatcher.query(hasQuery);

        if (!hasDraggable)
            return false;

        events::ui::GetUIDraggableDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("UIDraggableComponent");

        bool removeDraggable = false;
        bool isOpen = drawHeader(removeDraggable);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIDraggableData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Marks this element as a drag source");
            ImGui::Spacing();

            if (ImGui::SliderFloat("Ghost Opacity##UIDraggable", &data.ghostOpacity, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }

            float ghostOffset[2] = {data.ghostOffset.x, data.ghostOffset.y};
            if (ImGui::DragFloat2("Ghost Offset##UIDraggable", ghostOffset, 0.5f))
            {
                data.ghostOffset.x = ghostOffset[0];
                data.ghostOffset.y = ghostOffset[1];
                changed = true;
            }

            if (ImGui::Checkbox("Constrain to Parent##UIDraggable", &data.constrainToParent))
            {
                changed = true;
            }

            ImGui::Spacing();

            char tagBuffer[256];
            std::strncpy(tagBuffer, data.dragTag.c_str(), sizeof(tagBuffer));
            tagBuffer[sizeof(tagBuffer) - 1] = '\0';
            if (ImGui::InputText("Drag Tag##UIDraggable", tagBuffer, sizeof(tagBuffer)))
            {
                data.dragTag = tagBuffer;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Optional tag for filtering compatible drop targets");

            if (changed)
            {
                events::ui::SetUIDraggableDataCommand cmd;
                cmd.entity = handle;
                cmd.draggableData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeDraggable)
        {
            events::ui::RemoveUIDraggableComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIDraggableDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIDraggableHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Draggable");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIDraggable", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
