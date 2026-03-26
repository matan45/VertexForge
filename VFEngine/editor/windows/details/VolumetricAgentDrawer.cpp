#include "VolumetricAgentDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details
{
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
            ImGui::PopItemWidth();

            if (agent.isActive)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active");
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
