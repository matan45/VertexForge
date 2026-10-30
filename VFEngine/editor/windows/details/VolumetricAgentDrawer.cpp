#include "VolumetricAgentDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>
#include <exception>
#include <optional>

namespace windows::details
{
    namespace
    {
        // VK-1660: the agent's live state (Play). The drawer runs every editor frame on the main thread, so a
        // query nobody answers in this binary reads as "no state" instead of escaping the frame.
        std::optional<services::VolumetricAgentState> queryAgentState(services::EntityHandle handle)
        {
            try
            {
                events::volumetric::GetVolumetricAgentStateQuery query;
                query.entity = handle;
                return events::EventDispatcher::instance().query(query);
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }
    }

    bool VolumetricAgentDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::VolumetricAgentComponent>(entity))
        {
            return false;
        }

        auto& agent = registry.get<components::VolumetricAgentComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Volumetric Agent", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Agent Properties");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##VolAgentRadius", &agent.radius, 0.01f, 0.1f, 5.0f, "Radius: %.2f");
            ImGui::DragFloat("##VolAgentSpeed", &agent.maxSpeed, 0.1f, 0.0f, 50.0f, "Max Speed: %.1f");
            ImGui::DragFloat("##VolAgentAccel", &agent.maxAcceleration, 0.1f, 0.0f, 100.0f, "Max Accel: %.1f");
            ImGui::DragFloat("##VolAgentStopDist", &agent.stoppingDistance, 0.01f, 0.0f, 10.0f, "Stop Dist: %.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The agent arrives within this 3D distance of its destination");
            ImGui::DragFloat("##VolAgentTurnSpeed", &agent.turnSpeed, 1.0f, 0.0f, 1440.0f, "Turn Speed: %.0f deg/s");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Degrees per second toward the travel direction (0 = turn at once)");
            ImGui::PopItemWidth();
            ImGui::Checkbox("Align Pitch##VolAgent", &agent.alignPitch);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Also pitch toward the travel direction (off = yaw only)");

            if (agent.isActive)
            {
                ImGui::Spacing();
                const std::optional<services::VolumetricAgentState> state = queryAgentState(handle);
                if (!state)
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active");
                }
                else
                {
                    switch (state->state)
                    {
                    case services::VolumetricAgentMoveState::Idle:
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Idle");
                        break;
                    case services::VolumetricAgentMoveState::Moving:
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Moving: %.1f m left%s", state->remaining,
                                           state->partial ? " (partial path)" : "");
                        break;
                    case services::VolumetricAgentMoveState::Arrived:
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Arrived");
                        break;
                    case services::VolumetricAgentMoveState::Failed:
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Failed: %s", state->failReason.c_str());
                        break;
                    }
                }
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveVolumetricAgentComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
