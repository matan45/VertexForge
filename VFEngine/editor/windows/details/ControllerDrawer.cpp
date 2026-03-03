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

            // Movement
            ImGui::DragFloat("##CtrlMoveSpeed", &controller.moveSpeed, 0.1f, 0.0f, 50.0f, "Move Speed: %.1f");
            ImGui::DragFloat("##CtrlSprintMult", &controller.sprintMultiplier, 0.05f, 1.0f, 5.0f, "Sprint Mult: %.2f");
            ImGui::DragFloat("##CtrlJumpForce", &controller.jumpForce, 0.1f, 0.0f, 50.0f, "Jump Force: %.1f");
            ImGui::DragFloat("##CtrlArrivalDist", &controller.arrivalDistance, 0.05f, 0.1f, 10.0f, "Arrival Dist: %.2f");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Locomotion");

            ImGui::DragFloat("##CtrlAccel", &controller.acceleration, 0.5f, 0.0f, 100.0f, "Acceleration: %.1f");
            ImGui::DragFloat("##CtrlDecel", &controller.deceleration, 0.5f, 0.0f, 100.0f, "Deceleration: %.1f");
            ImGui::DragFloat("##CtrlRotSpeed", &controller.rotationSpeed, 5.0f, 0.0f, 1440.0f, "Rotation Speed: %.0f");
            ImGui::DragFloat("##CtrlAirCtrl", &controller.airControlFactor, 0.01f, 0.0f, 1.0f, "Air Control: %.2f");
            ImGui::DragFloat("##CtrlWalkThresh", &controller.walkSpeedThreshold, 0.1f, 0.0f, 50.0f, "Walk Threshold: %.1f");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Character Controller");

            ImGui::DragFloat("##CtrlStepHeight", &controller.stepHeight, 0.01f, 0.0f, 2.0f, "Step Height: %.2f");
            ImGui::DragFloat("##CtrlMaxSlope", &controller.maxSlopeAngle, 1.0f, 0.0f, 89.0f, "Max Slope: %.0f");

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
