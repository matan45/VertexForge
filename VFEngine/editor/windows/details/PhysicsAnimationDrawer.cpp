#include "PhysicsAnimationDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include "types/PhysicsTypes.hpp"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

namespace windows::details
{
    bool PhysicsAnimationDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasPhysicsAnimationComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::scene::GetPhysicsAnimationDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("PhysicsAnimationComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::PhysicsAnimationComponentData data = *dataOpt;
            bool changed = false;

            changed |= drawDefaultMode(data);
            ImGui::Spacing();
            changed |= drawCollisionLayer(data);
            ImGui::Spacing();
            changed |= drawBoneMappings(data);
            ImGui::Spacing();
            changed |= drawJointLimits(data);

            if (changed)
            {
                events::scene::SetPhysicsAnimationDataCommand cmd;
                cmd.entity = handle;
                cmd.data = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::scene::RemovePhysicsAnimationComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool PhysicsAnimationDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##PhysicsAnimationHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Physics Animation");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemovePhysicsAnimation", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool PhysicsAnimationDrawer::drawDefaultMode(services::PhysicsAnimationComponentData& data)
    {
        bool changed = false;

        const char* modeNames[] = {"Animated", "Kinematic", "Ragdoll"};
        int currentMode = static_cast<int>(data.defaultMode);

        if (ImGui::Combo("Default Mode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
        {
            data.defaultMode = static_cast<types::PhysicsAnimationMode>(currentMode);
            changed = true;
        }

        switch (data.defaultMode)
        {
        case types::PhysicsAnimationMode::Animated:
            ImGui::TextDisabled("Animated: Standard skeletal animation, no physics");
            break;
        case types::PhysicsAnimationMode::Kinematic:
            ImGui::TextDisabled("Kinematic: Bones drive physics bodies");
            break;
        case types::PhysicsAnimationMode::Ragdoll:
            ImGui::TextDisabled("Ragdoll: Physics drives bone transforms");
            break;
        }

        return changed;
    }

    bool PhysicsAnimationDrawer::drawCollisionLayer(services::PhysicsAnimationComponentData& data)
    {
        bool changed = false;

        int layer = static_cast<int>(data.collisionLayer);
        if (ImGui::DragInt("Collision Layer", &layer, 1.0f, 0, 15))
        {
            data.collisionLayer = static_cast<uint8_t>(layer);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Physics collision layer for bone bodies (0-15)");
        }

        return changed;
    }

    bool PhysicsAnimationDrawer::drawBoneMappings(services::PhysicsAnimationComponentData& data)
    {
        bool changed = false;

        bool isOpen = ImGui::CollapsingHeader("Bone Body Mappings",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        if (ImGui::SmallButton("+##AddBoneMapping"))
        {
            data.boneBodyMappings.emplace_back();
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Add bone body mapping");
        }

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            int removeIndex = -1;
            for (size_t i = 0; i < data.boneBodyMappings.size(); ++i)
            {
                auto& mapping = data.boneBodyMappings[i];
                ImGui::PushID(static_cast<int>(i));

                std::string label = mapping.boneName.empty()
                    ? "Bone " + std::to_string(i)
                    : mapping.boneName;

                bool boneOpen = ImGui::TreeNodeEx(label.c_str(),
                                                  ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                ImGui::SameLine();
                if (ImGui::SmallButton("x##RemoveBone"))
                {
                    removeIndex = static_cast<int>(i);
                }

                if (boneOpen)
                {
                    // Bone name
                    char nameBuffer[256] = {};
                    strncpy(nameBuffer, mapping.boneName.c_str(), sizeof(nameBuffer) - 1);
                    if (ImGui::InputText("Bone Name", nameBuffer, sizeof(nameBuffer)))
                    {
                        mapping.boneName = nameBuffer;
                        changed = true;
                    }

                    // Shape (only Box, Sphere, Capsule for bone bodies)
                    const char* shapeNames[] = {"Box", "Sphere", "Capsule"};
                    int shapeIndex = 0;
                    switch (mapping.shape)
                    {
                    case types::ColliderShape::Box:     shapeIndex = 0; break;
                    case types::ColliderShape::Sphere:  shapeIndex = 1; break;
                    case types::ColliderShape::Capsule: shapeIndex = 2; break;
                    default:                            shapeIndex = 2; break;
                    }

                    if (ImGui::Combo("Shape", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames)))
                    {
                        switch (shapeIndex)
                        {
                        case 0: mapping.shape = types::ColliderShape::Box; break;
                        case 1: mapping.shape = types::ColliderShape::Sphere; break;
                        case 2: mapping.shape = types::ColliderShape::Capsule; break;
                        }
                        changed = true;
                    }

                    // Size
                    if (ImGui::DragFloat3("Size", &mapping.size.x, 0.01f, 0.001f, 10.0f, "%.3f"))
                    {
                        changed = true;
                    }

                    // Offset
                    if (ImGui::DragFloat3("Offset", &mapping.offset.x, 0.01f, -100.0f, 100.0f, "%.3f"))
                    {
                        changed = true;
                    }

                    // Rotation offset as euler angles for user-friendliness
                    glm::vec3 euler = glm::degrees(glm::eulerAngles(mapping.rotationOffset));
                    if (ImGui::DragFloat3("Rotation Offset", &euler.x, 0.5f, -180.0f, 180.0f, "%.1f deg"))
                    {
                        mapping.rotationOffset = glm::quat(glm::radians(euler));
                        changed = true;
                    }

                    // Physics properties
                    if (ImGui::DragFloat("Mass", &mapping.mass, 0.1f, 0.001f, 1000.0f, "%.3f"))
                    {
                        if (mapping.mass < 0.001f) mapping.mass = 0.001f;
                        changed = true;
                    }

                    if (ImGui::SliderFloat("Friction", &mapping.friction, 0.0f, 1.0f, "%.2f"))
                    {
                        changed = true;
                    }

                    if (ImGui::SliderFloat("Restitution", &mapping.restitution, 0.0f, 1.0f, "%.2f"))
                    {
                        changed = true;
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            if (removeIndex >= 0)
            {
                data.boneBodyMappings.erase(data.boneBodyMappings.begin() + removeIndex);
                changed = true;
            }

            if (data.boneBodyMappings.empty())
            {
                ImGui::TextDisabled("No bone mappings. Click + to add.");
            }

            ImGui::Unindent(10.0f);
        }

        return changed;
    }

    bool PhysicsAnimationDrawer::drawJointLimits(services::PhysicsAnimationComponentData& data)
    {
        bool changed = false;

        bool isOpen = ImGui::CollapsingHeader("Joint Limits",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        if (ImGui::SmallButton("+##AddJointLimit"))
        {
            data.jointLimits.emplace_back();
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Add joint constraint limits");
        }

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            int removeIndex = -1;
            for (size_t i = 0; i < data.jointLimits.size(); ++i)
            {
                auto& joint = data.jointLimits[i];
                ImGui::PushID(static_cast<int>(i));

                std::string label = joint.boneName.empty()
                    ? "Joint " + std::to_string(i)
                    : joint.boneName;

                bool jointOpen = ImGui::TreeNodeEx(label.c_str(),
                                                   ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                ImGui::SameLine();
                if (ImGui::SmallButton("x##RemoveJoint"))
                {
                    removeIndex = static_cast<int>(i);
                }

                if (jointOpen)
                {
                    // Bone name
                    char nameBuffer[256] = {};
                    strncpy(nameBuffer, joint.boneName.c_str(), sizeof(nameBuffer) - 1);
                    if (ImGui::InputText("Bone Name", nameBuffer, sizeof(nameBuffer)))
                    {
                        joint.boneName = nameBuffer;
                        changed = true;
                    }

                    // Display angles in degrees for user convenience
                    float swingNormal = glm::degrees(joint.swingNormalHalfAngle);
                    if (ImGui::DragFloat("Swing Normal Half Angle", &swingNormal, 0.5f, 0.0f, 180.0f, "%.1f deg"))
                    {
                        joint.swingNormalHalfAngle = glm::radians(swingNormal);
                        changed = true;
                    }

                    float swingPlane = glm::degrees(joint.swingPlaneHalfAngle);
                    if (ImGui::DragFloat("Swing Plane Half Angle", &swingPlane, 0.5f, 0.0f, 180.0f, "%.1f deg"))
                    {
                        joint.swingPlaneHalfAngle = glm::radians(swingPlane);
                        changed = true;
                    }

                    float twistMin = glm::degrees(joint.twistMinAngle);
                    if (ImGui::DragFloat("Twist Min Angle", &twistMin, 0.5f, -180.0f, 0.0f, "%.1f deg"))
                    {
                        joint.twistMinAngle = glm::radians(twistMin);
                        changed = true;
                    }

                    float twistMax = glm::degrees(joint.twistMaxAngle);
                    if (ImGui::DragFloat("Twist Max Angle", &twistMax, 0.5f, 0.0f, 180.0f, "%.1f deg"))
                    {
                        joint.twistMaxAngle = glm::radians(twistMax);
                        changed = true;
                    }

                    if (ImGui::DragFloat("Max Friction Torque", &joint.maxFrictionTorque, 0.1f, 0.0f, 1000.0f, "%.1f"))
                    {
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Additional friction torque applied at the joint (0 = free movement)");
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            if (removeIndex >= 0)
            {
                data.jointLimits.erase(data.jointLimits.begin() + removeIndex);
                changed = true;
            }

            if (data.jointLimits.empty())
            {
                ImGui::TextDisabled("No joint limits. Click + to add.");
            }

            ImGui::Unindent(10.0f);
        }

        return changed;
    }
}
