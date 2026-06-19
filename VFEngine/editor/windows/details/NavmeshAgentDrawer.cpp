#include "NavmeshAgentDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "types/NavmeshTypes.hpp"
#include <imgui.h>

namespace windows::details
{
    bool NavmeshAgentDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::NavmeshAgentComponent>(entity))
        {
            return false;
        }

        auto& agent = registry.get<components::NavmeshAgentComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Navmesh Agent", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Agent Shape");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##NavAgentRadius", &agent.radius, 0.01f, 0.1f, 5.0f, "Radius: %.2f");
            ImGui::DragFloat("##NavAgentHeight", &agent.height, 0.1f, 0.5f, 10.0f, "Height: %.1f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Movement");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##NavAgentSpeed", &agent.maxSpeed, 0.1f, 0.0f, 50.0f, "Max Speed: %.1f");
            ImGui::DragFloat("##NavAgentAccel", &agent.maxAcceleration, 0.1f, 0.0f, 100.0f, "Max Accel: %.1f");
            ImGui::DragFloat("##NavAgentStopDist", &agent.stoppingDistance, 0.01f, 0.0f, 5.0f, "Stop Dist: %.2f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Checkbox("Root Motion Driven##NavAgentRootMotion", &agent.rootMotionDriven);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Animation root motion sets ground speed (requires applyRootMotion=true on the Animator).");

            ImGui::Spacing();
            ImGui::Text("Avoidance");
            ImGui::PushItemWidth(-1);
            int quality = agent.avoidanceQuality;
            if (ImGui::SliderInt("##NavAgentAvoidQuality", &quality, 0, 3, "Quality: %d"))
            {
                agent.avoidanceQuality = static_cast<uint8_t>(quality);
            }
            ImGui::DragFloat("##NavAgentSepWeight", &agent.separationWeight, 0.1f, 0.0f, 10.0f, "Separation: %.1f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Cost Overrides");
            ImGui::Checkbox("Use Custom Costs", &agent.useCustomCosts);
            if (agent.useCustomCosts)
            {
                ImGui::PushItemWidth(-1);
                for (int i = 0; i < types::NAVMESH_NAMED_AREA_COUNT; ++i)
                {
                    char label[64];
                    snprintf(label, sizeof(label), "%s##NavAgentCost%d", types::getNavmeshAreaName(static_cast<uint8_t>(i)), i);
                    ImGui::DragFloat(label, &agent.customAreaCosts[i], 0.1f, 0.0f, 100.0f, "%.1f");
                }
                ImGui::PopItemWidth();
            }

            if (agent.isActive)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active (Agent #%d)", agent.crowdAgentIndex);
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveNavmeshAgentComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
