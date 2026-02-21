#include "SocketAttachmentDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SocketEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool SocketAttachmentDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::socket::HasSocketAttachmentComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::socket::GetSocketAttachmentDataQuery dataQuery;
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
            bool isAttached = data.parentEntity.isValid() && !data.socketName.empty();

            if (isAttached)
            {
                drawAttachedState(handle, data);
            }
            else
            {
                drawUnattachedState(handle);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::socket::RemoveSocketAttachmentComponentCommand cmd;
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

    void SocketAttachmentDrawer::drawAttachedState(services::EntityHandle handle,
                                                     const events::socket::SocketAttachmentData& data)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (!data.parentEntityName.empty())
        {
            ImGui::Text("Parent: %s", data.parentEntityName.c_str());
        }
        else
        {
            ImGui::Text("Parent: Entity #%llu", static_cast<unsigned long long>(data.parentEntity.id));
        }

        ImGui::Text("Socket: %s", data.socketName.c_str());

        bool isActive = data.isActive;
        if (ImGui::Checkbox("Active", &isActive))
        {
            events::socket::SetSocketActiveCommand cmd;
            cmd.entity = handle;
            cmd.active = isActive;
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        if (ImGui::Button("Detach"))
        {
            events::socket::DetachFromSocketCommand cmd;
            cmd.childEntity = handle;
            dispatcher.execute(cmd);
            needsRefresh = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Remove socket attachment and restore normal transform");
        }
    }

    void SocketAttachmentDrawer::drawUnattachedState(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (needsRefresh)
        {
            refreshCandidateParents();
            needsRefresh = false;
        }

        if (ImGui::Button("Refresh"))
        {
            refreshCandidateParents();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu entities with skeleton)", candidateParents.size());

        const char* parentPreview = selectedParentIdx >= 0 && selectedParentIdx < static_cast<int>(candidateNames.size())
            ? candidateNames[selectedParentIdx].c_str()
            : "Select Parent Entity...";

        if (ImGui::BeginCombo("Parent##SocketParent", parentPreview))
        {
            for (int i = 0; i < static_cast<int>(candidateParents.size()); ++i)
            {
                bool isSelected = (i == selectedParentIdx);
                if (ImGui::Selectable(candidateNames[i].c_str(), isSelected))
                {
                    selectedParentIdx = i;
                    selectedParent = candidateParents[i];
                    selectedSocketIdx = -1;
                    refreshSocketNames(selectedParent);
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (selectedParentIdx >= 0 && !socketNames.empty())
        {
            const char* socketPreview = selectedSocketIdx >= 0 && selectedSocketIdx < static_cast<int>(socketNames.size())
                ? socketNames[selectedSocketIdx].c_str()
                : "Select Socket...";

            if (ImGui::BeginCombo("Socket##SocketName", socketPreview))
            {
                for (int i = 0; i < static_cast<int>(socketNames.size()); ++i)
                {
                    bool isSelected = (i == selectedSocketIdx);
                    if (ImGui::Selectable(socketNames[i].c_str(), isSelected))
                    {
                        selectedSocketIdx = i;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else if (selectedParentIdx >= 0 && socketNames.empty())
        {
            ImGui::TextDisabled("No sockets found on parent");
        }

        bool canAttach = selectedParentIdx >= 0 && selectedSocketIdx >= 0;
        if (!canAttach) ImGui::BeginDisabled();

        ImGui::Spacing();
        if (ImGui::Button("Attach"))
        {
            events::socket::AttachToSocketCommand cmd;
            cmd.childEntity = handle;
            cmd.parentEntity = selectedParent;
            cmd.socketName = socketNames[selectedSocketIdx];
            dispatcher.execute(cmd);

            selectedParentIdx = -1;
            selectedSocketIdx = -1;
            needsRefresh = true;
        }

        if (!canAttach) ImGui::EndDisabled();
    }

    void SocketAttachmentDrawer::refreshCandidateParents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        candidateParents.clear();
        candidateNames.clear();
        selectedParentIdx = -1;
        selectedSocketIdx = -1;
        socketNames.clear();

        // Get all entities with Animator component (these have skeletons with sockets)
        events::scene::GetEntitiesWithComponentQuery compQuery;
        compQuery.componentType = services::ComponentTypeId::Animator;
        candidateParents = dispatcher.query(compQuery);

        // Get names for each candidate
        for (const auto& parent : candidateParents)
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = parent;
            auto entityDataOpt = dispatcher.query(entityQuery);

            if (entityDataOpt.has_value() && !entityDataOpt->name.empty())
            {
                candidateNames.push_back(entityDataOpt->name);
            }
            else
            {
                candidateNames.push_back("Entity #" + std::to_string(parent.id));
            }
        }
    }

    void SocketAttachmentDrawer::refreshSocketNames(services::EntityHandle parentEntity)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        socketNames.clear();
        selectedSocketIdx = -1;

        events::socket::GetSocketNamesQuery query;
        query.entity = parentEntity;
        socketNames = dispatcher.query(query);
    }
}
