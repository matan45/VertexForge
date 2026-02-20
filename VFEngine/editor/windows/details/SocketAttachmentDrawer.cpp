#include "SocketAttachmentDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SocketEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool SocketAttachmentDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        services::events::socket::HasSocketAttachmentComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        services::events::socket::GetSocketAttachmentDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("SocketAttachmentComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            auto data = *dataOpt;

            // Parent entity info
            if (data.parentEntity.isValid())
            {
                if (!data.parentEntityName.empty())
                {
                    ImGui::Text("Parent: %s", data.parentEntityName.c_str());
                }
                else
                {
                    ImGui::Text("Parent: Entity #%llu", static_cast<unsigned long long>(data.parentEntity.id));
                }
            }
            else
            {
                ImGui::TextDisabled("Parent: Not attached");
            }

            // Socket name
            ImGui::Text("Socket: %s", data.socketName.empty() ? "(none)" : data.socketName.c_str());

            // Active toggle
            bool isActive = data.isActive;
            if (ImGui::Checkbox("Active", &isActive))
            {
                services::events::socket::SetSocketActiveCommand cmd;
                cmd.entity = handle;
                cmd.active = isActive;
                dispatcher.execute(cmd);
            }

            // Detach button
            if (data.parentEntity.isValid() && !data.socketName.empty())
            {
                ImGui::Spacing();
                if (ImGui::Button("Detach"))
                {
                    services::events::socket::DetachFromSocketCommand cmd;
                    cmd.childEntity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Remove socket attachment and restore normal transform");
                }
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            services::events::socket::RemoveSocketAttachmentComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool SocketAttachmentDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##SocketAttachmentHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Socket Attachment");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveSocketAttachment", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
