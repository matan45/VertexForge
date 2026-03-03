#include "ControllerDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>
#include <cstring>

namespace windows::details
{
    static const char* paramSourceLabel(components::LocomotionParamSource source)
    {
        switch (source)
        {
        case components::LocomotionParamSource::Speed: return "Speed";
        case components::LocomotionParamSource::Grounded: return "Grounded";
        case components::LocomotionParamSource::VerticalVelocity: return "Vert. Velocity";
        case components::LocomotionParamSource::DirectionX: return "Direction X";
        case components::LocomotionParamSource::DirectionY: return "Direction Y";
        default: return "Unknown";
        }
    }

    static void editString(const char* label, const char* id, std::string& str)
    {
        ImGui::Text("%s", label);
        ImGui::SameLine(100.0f);

        ImGui::PushItemWidth(-1.0f);
        char buf[128];
        std::strncpy(buf, str.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText(id, buf, sizeof(buf)))
            str = buf;
        ImGui::PopItemWidth();
    }

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

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Animator Sync");

            auto& config = controller.locomotionConfig;
            ImGui::Checkbox("Sync to Animator##CtrlSync", &config.syncToAnimator);

            ImGui::PopItemWidth();

            if (config.syncToAnimator)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("State Names");

                editString("Idle", "##StateIdle", config.idleState);
                editString("Walk", "##StateWalk", config.walkState);
                editString("Run", "##StateRun", config.runState);
                editString("Jump", "##StateJump", config.jumpState);
                editString("Fall", "##StateFall", config.fallState);

                ImGui::Spacing();
                ImGui::TextDisabled("Parameter Mappings");

                int paramRemoveIdx = -1;
                for (size_t i = 0; i < config.paramMappings.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    auto& mapping = config.paramMappings[i];

                    ImGui::Text("%s", paramSourceLabel(mapping.source));
                    ImGui::SameLine(100.0f);

                    ImGui::PushItemWidth(-26.0f);
                    char buf[128];
                    std::strncpy(buf, mapping.paramName.c_str(), sizeof(buf));
                    buf[sizeof(buf) - 1] = '\0';
                    if (ImGui::InputText("##ParamName", buf, sizeof(buf)))
                        mapping.paramName = buf;
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    if (ImGui::SmallButton("x"))
                        paramRemoveIdx = static_cast<int>(i);

                    ImGui::PopID();
                }

                if (paramRemoveIdx >= 0)
                    config.paramMappings.erase(config.paramMappings.begin() + paramRemoveIdx);

                constexpr components::LocomotionParamSource allSources[] = {
                    components::LocomotionParamSource::Speed,
                    components::LocomotionParamSource::Grounded,
                    components::LocomotionParamSource::VerticalVelocity,
                    components::LocomotionParamSource::DirectionX,
                    components::LocomotionParamSource::DirectionY
                };

                bool hasUnmappedParam = false;
                for (auto s : allSources)
                {
                    bool found = false;
                    for (const auto& m : config.paramMappings)
                        if (m.source == s) { found = true; break; }
                    if (!found) { hasUnmappedParam = true; break; }
                }

                if (hasUnmappedParam && ImGui::Button("+ Add Parameter##CtrlAddParam"))
                    ImGui::OpenPopup("AddParamPopup");

                if (ImGui::BeginPopup("AddParamPopup"))
                {
                    for (auto s : allSources)
                    {
                        bool found = false;
                        for (const auto& m : config.paramMappings)
                            if (m.source == s) { found = true; break; }
                        if (found) continue;

                        const char* label = paramSourceLabel(s);
                        if (ImGui::Selectable(label))
                        {
                            const char* defaultNames[] = {"speed", "grounded", "verticalVelocity", "directionX", "directionY"};
                            config.paramMappings.push_back({s, defaultNames[static_cast<int>(s)]});
                        }
                    }
                    ImGui::EndPopup();
                }
            }

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
