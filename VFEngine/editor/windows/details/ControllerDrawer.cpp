#include "ControllerDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details
{
    bool ControllerDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
        {
            return false;
        }

        auto& controller = registry.get<components::ControllerComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Controller", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##CtrlMoveSpeed", &controller.moveSpeed, 0.1f, 0.0f, 50.0f, "Move Speed: %.1f");
            ImGui::DragFloat("##CtrlSprintMult", &controller.sprintMultiplier, 0.05f, 1.0f, 5.0f, "Sprint Mult: %.2f");
            ImGui::DragFloat("##CtrlJumpForce", &controller.jumpForce, 0.1f, 0.0f, 50.0f, "Jump Force: %.1f");
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveControllerComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
