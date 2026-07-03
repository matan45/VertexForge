#include "VehicleDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace windows::details
{
    namespace
    {
        constexpr float kRadToDeg = 57.2957795f;
        constexpr float kDegToRad = 0.0174532925f;

        const char* controllerName(types::VehicleControllerType type)
        {
            switch (type)
            {
            case types::VehicleControllerType::Tracked: return "Tracked";
            case types::VehicleControllerType::Motorcycle: return "Motorcycle";
            case types::VehicleControllerType::Wheeled:
            default: return "Wheeled";
            }
        }
    }

    bool VehicleDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasVehicleComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasVehicle = dispatcher.query(hasQuery);
        if (!hasVehicle)
            return false;

        events::scene::GetVehicleDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);
        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("VehicleComponent");

        bool removeVehicle = false;
        bool isOpen = drawHeader(removeVehicle);
        if (isOpen)
        {
            ImGui::Indent(10.0f);
            services::VehicleComponentData data = *dataOpt;
            bool changed = false;

            changed |= drawPresetControls(data);
            ImGui::Spacing();
            changed |= drawEngineControls(data.config);
            ImGui::Spacing();
            changed |= drawCollisionControls(data.config);
            ImGui::Spacing();
            changed |= drawWheelControls(data.config);
            ImGui::Spacing();
            changed |= drawDifferentialControls(data.config);

            if (changed)
            {
                events::scene::SetVehicleDataCommand cmd;
                cmd.entity = handle;
                cmd.vehicleData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeVehicle)
        {
            events::scene::RemoveVehicleComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool VehicleDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##VehicleHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Vehicle");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveVehicle", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool VehicleDrawer::drawPresetControls(services::VehicleComponentData& data)
    {
        bool changed = false;

        const char* presets[] = {"Four Wheel Car", "Tank", "Motorcycle"};
        int preset = 0;
        if (data.config.controllerType == types::VehicleControllerType::Tracked)
            preset = 1;
        else if (data.config.controllerType == types::VehicleControllerType::Motorcycle)
            preset = 2;

        if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets)))
        {
            switch (preset)
            {
            case 1:
                data.config = types::VehicleConfig::createTank();
                break;
            case 2:
                data.config = types::VehicleConfig::createMotorcycle();
                break;
            case 0:
            default:
                data.config = types::VehicleConfig::createFourWheelCar();
                break;
            }
            changed = true;
        }

        ImGui::TextDisabled("%s controller, %zu wheels", controllerName(data.config.controllerType), data.config.wheels.size());
        return changed;
    }

    bool VehicleDrawer::drawEngineControls(types::VehicleConfig& config)
    {
        bool changed = false;
        if (ImGui::TreeNodeEx("Engine", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ImGui::DragFloat("Max Torque", &config.engineMaxTorque, 10.0f, 1.0f, 100000.0f, "%.0f");
            changed |= ImGui::DragFloat("Min RPM", &config.engineMinRPM, 50.0f, 0.0f, 50000.0f, "%.0f");
            changed |= ImGui::DragFloat("Max RPM", &config.engineMaxRPM, 50.0f, 1.0f, 100000.0f, "%.0f");

            float maxPitchRollDeg = config.maxPitchRollAngle * kRadToDeg;
            if (ImGui::SliderFloat("Max Pitch/Roll", &maxPitchRollDeg, 0.0f, 180.0f, "%.1f deg"))
            {
                config.maxPitchRollAngle = maxPitchRollDeg * kDegToRad;
                changed = true;
            }

            if (config.controllerType == types::VehicleControllerType::Motorcycle)
            {
                float leanDeg = config.maxLeanAngle * kRadToDeg;
                if (ImGui::SliderFloat("Max Lean", &leanDeg, 0.0f, 89.0f, "%.1f deg"))
                {
                    config.maxLeanAngle = leanDeg * kDegToRad;
                    changed = true;
                }
                changed |= ImGui::DragFloat("Lean Spring", &config.leanSpringConstant, 10.0f, 0.0f, 50000.0f, "%.1f");
                changed |= ImGui::DragFloat("Lean Damping", &config.leanSpringDamping, 10.0f, 0.0f, 50000.0f, "%.1f");
            }

            if (config.engineMaxRPM <= config.engineMinRPM)
            {
                config.engineMaxRPM = config.engineMinRPM + 1.0f;
                changed = true;
            }
            if (config.engineMaxTorque < 1.0f)
            {
                config.engineMaxTorque = 1.0f;
                changed = true;
            }

            ImGui::TreePop();
        }
        return changed;
    }

    bool VehicleDrawer::drawCollisionControls(types::VehicleConfig& config)
    {
        bool changed = false;
        if (ImGui::TreeNodeEx("Wheel Collision", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* testerNames[] = {"Ray", "Cast Sphere", "Cast Cylinder"};
            int tester = static_cast<int>(config.collisionTester);
            if (ImGui::Combo("Tester", &tester, testerNames, IM_ARRAYSIZE(testerNames)))
            {
                config.collisionTester = static_cast<types::VehicleCollisionTesterType>(tester);
                changed = true;
            }

            int layer = static_cast<int>(config.wheelCollisionLayer);
            if (ImGui::SliderInt("Layer", &layer, 0, 15))
            {
                config.wheelCollisionLayer = static_cast<uint8_t>(layer);
                changed = true;
            }

            float slopeDeg = config.maxSlopeAngle * kRadToDeg;
            if (ImGui::SliderFloat("Max Slope", &slopeDeg, 0.0f, 89.0f, "%.1f deg"))
            {
                config.maxSlopeAngle = slopeDeg * kDegToRad;
                changed = true;
            }

            ImGui::TreePop();
        }
        return changed;
    }

    bool VehicleDrawer::drawWheelControls(types::VehicleConfig& config)
    {
        bool changed = false;
        if (ImGui::TreeNodeEx("Wheels", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < config.wheels.size(); ++i)
            {
                auto& wheel = config.wheels[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("wheel", "Wheel %d", static_cast<int>(i)))
                {
                    changed |= ImGui::DragFloat3("Position", &wheel.position.x, 0.01f, -100.0f, 100.0f, "%.3f");
                    changed |= ImGui::DragFloat3("Suspension Direction", &wheel.suspensionDirection.x, 0.01f, -1.0f, 1.0f, "%.3f");
                    changed |= ImGui::DragFloat("Radius", &wheel.radius, 0.01f, 0.01f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Width", &wheel.width, 0.01f, 0.01f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Suspension Min", &wheel.suspensionMinLength, 0.01f, 0.0f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Suspension Max", &wheel.suspensionMaxLength, 0.01f, 0.0f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Spring Frequency", &wheel.suspensionFrequency, 0.1f, 0.01f, 20.0f, "%.2f");
                    changed |= ImGui::DragFloat("Spring Damping", &wheel.suspensionDamping, 0.1f, 0.0f, 20.0f, "%.2f");

                    float steerDeg = wheel.maxSteerAngle * kRadToDeg;
                    if (ImGui::SliderFloat("Max Steer", &steerDeg, 0.0f, 89.0f, "%.1f deg"))
                    {
                        wheel.maxSteerAngle = steerDeg * kDegToRad;
                        changed = true;
                    }

                    changed |= ImGui::DragFloat("Brake Torque", &wheel.maxBrakeTorque, 25.0f, 0.0f, 100000.0f, "%.0f");
                    changed |= ImGui::DragFloat("Handbrake Torque", &wheel.maxHandBrakeTorque, 25.0f, 0.0f, 100000.0f, "%.0f");
                    changed |= ImGui::Checkbox("Driven", &wheel.driven);
                    if (config.controllerType == types::VehicleControllerType::Tracked)
                    {
                        changed |= ImGui::SliderInt("Track", &wheel.trackIndex, -1, 1);
                    }
                    changed |= ImGui::DragFloat("Longitudinal Friction", &wheel.longitudinalFriction, 0.05f, 0.0f, 10.0f, "%.2f");
                    changed |= ImGui::DragFloat("Lateral Friction", &wheel.lateralFriction, 0.05f, 0.0f, 10.0f, "%.2f");

                    if (wheel.suspensionMaxLength < wheel.suspensionMinLength)
                    {
                        wheel.suspensionMaxLength = wheel.suspensionMinLength;
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        return changed;
    }

    bool VehicleDrawer::drawDifferentialControls(types::VehicleConfig& config)
    {
        if (config.controllerType == types::VehicleControllerType::Tracked)
            return false;

        bool changed = false;
        if (ImGui::TreeNodeEx("Differentials", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < config.differentials.size(); ++i)
            {
                auto& diff = config.differentials[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("diff", "Differential %d", static_cast<int>(i)))
                {
                    changed |= ImGui::InputInt("Left Wheel", &diff.leftWheel);
                    changed |= ImGui::InputInt("Right Wheel", &diff.rightWheel);
                    changed |= ImGui::DragFloat("Ratio", &diff.differentialRatio, 0.01f, 0.01f, 100.0f, "%.3f");
                    changed |= ImGui::SliderFloat("Left/Right Split", &diff.leftRightSplit, 0.0f, 1.0f, "%.2f");
                    changed |= ImGui::SliderFloat("Engine Torque Ratio", &diff.engineTorqueRatio, 0.0f, 1.0f, "%.2f");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (ImGui::Button("Add Differential"))
            {
                config.differentials.push_back({});
                changed = true;
            }
            ImGui::SameLine();
            if (!config.differentials.empty() && ImGui::Button("Remove Last"))
            {
                config.differentials.pop_back();
                changed = true;
            }

            ImGui::TreePop();
        }
        return changed;
    }
}
