#include "AnimationPhysicsPanel.hpp"
#include "physics/PhysicsAnimationAsset.hpp"
#include "nfd/FileDialog.hpp"
#include "events/EventDispatcher.hpp"
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>

namespace windows::animation
{
    bool AnimationPhysicsPanel::draw(types::PhysicsAnimationConfig& config,
                                      int& selectedChannel,
                                      const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                      const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                      bool& showColliderOverlay,
                                      std::string& configPath)
    {
        bool changed = false;

        ImGui::Text("Physics Animation");
        ImGui::Separator();

        drawFileBar(config, configPath);
        ImGui::Spacing();

        changed |= drawGlobalConfig(config, showColliderOverlay);
        ImGui::Spacing();

        changed |= drawMotorConfig(config);
        ImGui::Spacing();

        changed |= drawHitReactionConfig(config);
        ImGui::Spacing();

        if (selectedChannel >= 0 && selectedChannel < static_cast<int>(evaluatedBones.size()))
        {
            changed |= drawSelectedBoneMapping(config, selectedChannel, evaluatedBones);
            ImGui::Spacing();
            changed |= drawSelectedBoneJointLimits(config, selectedChannel, evaluatedBones);
            ImGui::Spacing();
            changed |= drawSelectedBoneMotor(config, selectedChannel, evaluatedBones);
            ImGui::Spacing();
        }
        else
        {
            ImGui::TextDisabled("Select a bone in the skeleton panel");
        }

        drawAllMappingsSummary(config, selectedChannel, boneNameToIndex);

        return changed;
    }

    void AnimationPhysicsPanel::drawFileBar(types::PhysicsAnimationConfig& config, std::string& configPath)
    {
        float buttonWidth = 50.0f;

        if (ImGui::Button("Load", ImVec2(buttonWidth, 0)))
        {
            nfd::FileDialog fileDialog;
            std::string selectedPath = fileDialog.openFileDialog(
                {{L"VF Physics Anim (*.vfPhysAnim)", L"*.vfPhysAnim"}});

            if (!selectedPath.empty())
            {
                auto loaded = physics::PhysicsAnimationAsset::load(selectedPath);
                if (loaded.has_value())
                {
                    config = *loaded;
                    configPath = selectedPath;
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Save", ImVec2(buttonWidth, 0)))
        {
            if (configPath.empty())
            {
                nfd::FileDialog fileDialog;
                std::string selectedPath = fileDialog.saveFileDialog(
                    {{L"VF Physics Anim (*.vfPhysAnim)", L"*.vfPhysAnim"}},
                    L"vfPhysAnim");

                if (!selectedPath.empty())
                {
                    configPath = selectedPath;
                }
            }

            if (!configPath.empty())
            {
                physics::PhysicsAnimationAsset::save(configPath, config);
                events::resource::AssetSavedNotification notif;
                notif.filePath = configPath;
                events::EventDispatcher::instance().publish(notif);
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("As..", ImVec2(35.0f, 0)))
        {
            nfd::FileDialog fileDialog;
            std::string selectedPath = fileDialog.saveFileDialog(
                {{L"VF Physics Anim (*.vfPhysAnim)", L"*.vfPhysAnim"}},
                L"vfPhysAnim");

            if (!selectedPath.empty())
            {
                configPath = selectedPath;
                physics::PhysicsAnimationAsset::save(configPath, config);
                events::resource::AssetSavedNotification notif;
                notif.filePath = configPath;
                events::EventDispatcher::instance().publish(notif);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save As...");

        if (!configPath.empty())
        {
            std::filesystem::path p(configPath);
            ImGui::TextDisabled("%s", p.filename().string().c_str());
        }
    }

    bool AnimationPhysicsPanel::drawGlobalConfig(types::PhysicsAnimationConfig& config, bool& showColliderOverlay)
    {
        bool changed = false;

        if (ImGui::CollapsingHeader("Global Config", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(5.0f);

            const char* modeNames[] = {"Animated", "Kinematic", "Ragdoll", "Powered Ragdoll"};
            int currentMode = static_cast<int>(config.defaultMode);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Mode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
            {
                config.defaultMode = static_cast<types::PhysicsAnimationMode>(currentMode);
                changed = true;
            }

            changed |= drawLayerCombo("##GlobalLayer", config.collisionLayer);

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##BlendTime", &config.kinematicToRagdollBlendTime, 0.01f, 0.0f, 2.0f, "Blend: %.2fs"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kinematic to Ragdoll blend time");

            ImGui::Checkbox("Show Colliders", &showColliderOverlay);

            ImGui::Unindent(5.0f);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawMotorConfig(types::PhysicsAnimationConfig& config)
    {
        bool changed = false;

        if (ImGui::CollapsingHeader("Motors (Powered Ragdoll)"))
        {
            ImGui::Indent(5.0f);

            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##DefStrength", &config.defaultMotorStrength, 0.0f, 1.0f, "Strength: %.2f"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Default motor strength for bones without an override");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##DefFreq", &config.defaultMotorFrequency, 0.5f, 1.0f, 120.0f, "Frequency: %.1f Hz"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Motor spring frequency (higher = snappier pose tracking)");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##DefDamp", &config.defaultMotorDamping, 0.05f, 0.0f, 5.0f, "Damping: %.2f"))
                changed = true;

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##DefTorque", &config.defaultMotorMaxTorque, 1.0f, 0.0f, 10000.0f, "Max Torque: %.0f N*m"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Torque available at strength 1.0 (heavy bones like hips/spine need more)");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##RootStrength", &config.rootMotorStrength, 0.0f, 1.0f, "Root: %.2f"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How strongly the root body tracks the entity transform (0 = free)");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##BlendIn", &config.poweredBlendInTime, 0.01f, 0.0f, 2.0f, "Blend In: %.2fs"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visual blend when entering powered ragdoll");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##BlendOut", &config.ragdollToAnimatedBlendTime, 0.01f, 0.0f, 2.0f, "Blend Out: %.2fs"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Crossfade from the captured ragdoll pose back to animation");

            ImGui::Unindent(5.0f);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawHitReactionConfig(types::PhysicsAnimationConfig& config)
    {
        bool changed = false;

        if (ImGui::CollapsingHeader("Hit Reaction / Settle"))
        {
            ImGui::Indent(5.0f);

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##RecoverTime", &config.hitReaction.defaultRecoverTime, 0.01f, 0.0f, 5.0f, "Recover: %.2fs"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time for motor strength to recover after a hit");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##StrengthDip", &config.hitReaction.strengthDip, 0.0f, 1.0f, "Dip To: %.2f"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strength the affected chain drops to on impact (0 = limp)");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragInt("##ChainDepth", &config.hitReaction.chainDepth, 0.1f, -1, 16, "Chain Depth: %d"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many levels of child bones a hit affects (-1 = all descendants)");

            ImGui::Separator();

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##SettleLin", &config.settleLinearVelocityThreshold, 0.005f, 0.0f, 2.0f, "Settle Lin: %.3f m/s"))
                changed = true;

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##SettleAng", &config.settleAngularVelocityThreshold, 0.01f, 0.0f, 5.0f, "Settle Ang: %.2f rad/s"))
                changed = true;

            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragInt("##SettleFrames", &config.settleFrameCount, 1.0f, 1, 600, "Settle Frames: %d"))
                changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Consecutive near-still frames before the ragdoll counts as settled");

            ImGui::Unindent(5.0f);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawSelectedBoneMotor(types::PhysicsAnimationConfig& config,
                                                       int selectedChannel,
                                                       const std::vector<services::EvaluatedBoneInfo>& evaluatedBones)
    {
        bool changed = false;
        const std::string& boneName = evaluatedBones[selectedChannel].name;

        if (ImGui::CollapsingHeader("Bone Motor"))
        {
            ImGui::Indent(5.0f);

            types::BoneMotorSettings* motor = nullptr;
            int motorIndex = -1;
            for (size_t i = 0; i < config.boneMotors.size(); ++i)
            {
                if (config.boneMotors[i].boneName == boneName)
                {
                    motor = &config.boneMotors[i];
                    motorIndex = static_cast<int>(i);
                    break;
                }
            }

            if (motor)
            {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##MotStrength", &motor->strength, 0.0f, 1.0f, "Strength: %.2f"))
                    changed = true;

                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##MotFreq", &motor->frequency, 0.5f, 1.0f, 120.0f, "Frequency: %.1f Hz"))
                    changed = true;

                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##MotDamp", &motor->damping, 0.05f, 0.0f, 5.0f, "Damping: %.2f"))
                    changed = true;

                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##MotTorque", &motor->maxTorque, 1.0f, 0.0f, 10000.0f, "Max Torque: %.0f N*m"))
                    changed = true;

                ImGui::Spacing();
                if (ImGui::Button("Remove Motor Override", ImVec2(-1, 0)))
                {
                    config.boneMotors.erase(config.boneMotors.begin() + motorIndex);
                    changed = true;
                }
            }
            else
            {
                ImGui::TextDisabled("Using motor defaults");
                if (ImGui::Button("Add Motor Override", ImVec2(-1, 0)))
                {
                    types::BoneMotorSettings newMotor;
                    newMotor.boneName = boneName;
                    newMotor.strength = config.defaultMotorStrength;
                    newMotor.frequency = config.defaultMotorFrequency;
                    newMotor.damping = config.defaultMotorDamping;
                    newMotor.maxTorque = config.defaultMotorMaxTorque;
                    config.boneMotors.push_back(newMotor);
                    changed = true;
                }
            }

            ImGui::Unindent(5.0f);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawSelectedBoneMapping(types::PhysicsAnimationConfig& config,
                                                         int selectedChannel,
                                                         const std::vector<services::EvaluatedBoneInfo>& evaluatedBones)
    {
        bool changed = false;
        const std::string& boneName = evaluatedBones[selectedChannel].name;

        if (ImGui::CollapsingHeader("Body Mapping", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(5.0f);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", boneName.c_str());

            types::BoneBodyMapping* mapping = nullptr;
            int mappingIndex = -1;
            for (size_t i = 0; i < config.boneBodyMappings.size(); ++i)
            {
                if (config.boneBodyMappings[i].boneName == boneName)
                {
                    mapping = &config.boneBodyMappings[i];
                    mappingIndex = static_cast<int>(i);
                    break;
                }
            }

            if (mapping)
            {
                changed |= drawMappingFields(*mapping);

                ImGui::Spacing();
                if (ImGui::Button("Remove Mapping", ImVec2(-1, 0)))
                {
                    config.boneBodyMappings.erase(config.boneBodyMappings.begin() + mappingIndex);
                    changed = true;
                }
            }
            else
            {
                if (ImGui::Button("Add Body Mapping", ImVec2(-1, 0)))
                {
                    types::BoneBodyMapping newMapping;
                    newMapping.boneName = boneName;
                    config.boneBodyMappings.push_back(newMapping);
                    changed = true;
                }
            }

            ImGui::Unindent(5.0f);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawSelectedBoneJointLimits(types::PhysicsAnimationConfig& config,
                                                             int selectedChannel,
                                                             const std::vector<services::EvaluatedBoneInfo>& evaluatedBones)
    {
        bool changed = false;
        const std::string& boneName = evaluatedBones[selectedChannel].name;

        if (ImGui::CollapsingHeader("Joint Limits"))
        {
            ImGui::Indent(5.0f);

            types::JointConstraintLimits* limits = nullptr;
            int limitsIndex = -1;
            for (size_t i = 0; i < config.jointLimits.size(); ++i)
            {
                if (config.jointLimits[i].boneName == boneName)
                {
                    limits = &config.jointLimits[i];
                    limitsIndex = static_cast<int>(i);
                    break;
                }
            }

            if (limits)
            {
                changed |= drawJointLimitFields(*limits);

                ImGui::Spacing();
                if (ImGui::Button("Remove Limits", ImVec2(-1, 0)))
                {
                    config.jointLimits.erase(config.jointLimits.begin() + limitsIndex);
                    changed = true;
                }
            }
            else
            {
                if (ImGui::Button("Add Joint Limits", ImVec2(-1, 0)))
                {
                    types::JointConstraintLimits newLimits;
                    newLimits.boneName = boneName;
                    config.jointLimits.push_back(newLimits);
                    changed = true;
                }
            }

            ImGui::Unindent(5.0f);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawMappingFields(types::BoneBodyMapping& mapping)
    {
        bool changed = false;

        const char* shapeNames[] = {"Box", "Sphere", "Capsule"};
        int shapeIndex = 0;
        switch (mapping.shape)
        {
        case types::ColliderShape::Box:     shapeIndex = 0; break;
        case types::ColliderShape::Sphere:  shapeIndex = 1; break;
        case types::ColliderShape::Capsule: shapeIndex = 2; break;
        default:                            shapeIndex = 2; break;
        }

        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##Shape", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            switch (shapeIndex)
            {
            case 0: mapping.shape = types::ColliderShape::Box; break;
            case 1: mapping.shape = types::ColliderShape::Sphere; break;
            case 2: mapping.shape = types::ColliderShape::Capsule; break;
            }
            changed = true;
        }

        ImGui::Text("Size");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##Size", &mapping.size.x, 0.01f, 0.001f, 10.0f, "%.3f"))
            changed = true;

        ImGui::Text("Offset");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##Offset", &mapping.offset.x, 0.01f, -100.0f, 100.0f, "%.3f"))
            changed = true;

        ImGui::Text("Rotation");
        glm::vec3 euler = glm::degrees(glm::eulerAngles(mapping.rotationOffset));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##RotOff", &euler.x, 0.5f, -180.0f, 180.0f, "%.1f"))
        {
            mapping.rotationOffset = glm::quat(glm::radians(euler));
            changed = true;
        }

        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##Mass", &mapping.mass, 0.1f, 0.001f, 1000.0f, "Mass: %.3f"))
        {
            if (mapping.mass < 0.001f) mapping.mass = 0.001f;
            changed = true;
        }

        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##Friction", &mapping.friction, 0.0f, 1.0f, "Fric: %.2f"))
            changed = true;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##Restit", &mapping.restitution, 0.0f, 1.0f, "Rest: %.2f"))
            changed = true;

        bool useGlobalLayer = (mapping.collisionLayer == 255);
        if (ImGui::Checkbox("##UseGlobalLayer", &useGlobalLayer))
        {
            mapping.collisionLayer = useGlobalLayer ? 255 : 1;
            changed = true;
        }
        ImGui::SameLine();
        if (useGlobalLayer)
        {
            ImGui::TextDisabled("Layer: Global");
        }
        else
        {
            changed |= drawLayerCombo("##BoneLayer", mapping.collisionLayer);
        }

        return changed;
    }

    bool AnimationPhysicsPanel::drawJointLimitFields(types::JointConstraintLimits& limits)
    {
        bool changed = false;

        float swingNormal = glm::degrees(limits.swingNormalHalfAngle);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##SwingN", &swingNormal, 0.5f, 0.0f, 180.0f, "SwN: %.1f"))
        {
            limits.swingNormalHalfAngle = glm::radians(swingNormal);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Swing Normal Half Angle");

        float swingPlane = glm::degrees(limits.swingPlaneHalfAngle);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##SwingP", &swingPlane, 0.5f, 0.0f, 180.0f, "SwP: %.1f"))
        {
            limits.swingPlaneHalfAngle = glm::radians(swingPlane);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Swing Plane Half Angle");

        float twistMin = glm::degrees(limits.twistMinAngle);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##TwMin", &twistMin, 0.5f, -180.0f, 0.0f, "TwMin: %.1f"))
        {
            limits.twistMinAngle = glm::radians(twistMin);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Twist Min Angle");

        float twistMax = glm::degrees(limits.twistMaxAngle);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##TwMax", &twistMax, 0.5f, 0.0f, 180.0f, "TwMax: %.1f"))
        {
            limits.twistMaxAngle = glm::radians(twistMax);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Twist Max Angle");

        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##FricTq", &limits.maxFrictionTorque, 0.1f, 0.0f, 1000.0f, "FricTq: %.1f"))
            changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Max Friction Torque");

        return changed;
    }

    void AnimationPhysicsPanel::drawAllMappingsSummary(const types::PhysicsAnimationConfig& config,
                                                        int& selectedChannel,
                                                        const std::unordered_map<std::string, size_t>& boneNameToIndex)
    {
        if (config.boneBodyMappings.empty()) return;

        if (ImGui::CollapsingHeader("All Mappings"))
        {
            ImGui::Indent(5.0f);

            for (size_t i = 0; i < config.boneBodyMappings.size(); ++i)
            {
                const auto& mapping = config.boneBodyMappings[i];

                ImGui::PushID(static_cast<int>(i));

                auto it = boneNameToIndex.find(mapping.boneName);
                bool isSelected = (it != boneNameToIndex.end() && static_cast<int>(it->second) == selectedChannel);

                if (isSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));

                if (ImGui::Selectable(mapping.boneName.c_str(), isSelected))
                {
                    if (it != boneNameToIndex.end())
                    {
                        selectedChannel = static_cast<int>(it->second);
                    }
                }

                if (isSelected) ImGui::PopStyleColor();

                ImGui::PopID();
            }

            ImGui::Unindent(5.0f);
        }
    }

    bool AnimationPhysicsPanel::drawLayerCombo(const char* id, uint8_t& layerValue)
    {
        std::vector<types::CollisionLayer> layers;
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::physics::GetCollisionLayersQuery layersQuery;
            layers = dispatcher.query(layersQuery);
        }
        catch (...) {}

        if (layers.empty())
        {
            layers = types::PhysicsSettings::createDefault().layers;
        }

        if (layers.empty()) return false;

        std::vector<std::string> layerNames;
        int currentIndex = 0;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            layerNames.push_back(layers[i].name + " [" + std::to_string(layers[i].index) + "]");
            if (layers[i].index == layerValue)
            {
                currentIndex = static_cast<int>(i);
            }
        }

        bool changed = false;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(id, layerNames[currentIndex].c_str()))
        {
            for (size_t i = 0; i < layers.size(); ++i)
            {
                bool isSelected = (layers[i].index == layerValue);
                if (ImGui::Selectable(layerNames[i].c_str(), isSelected))
                {
                    layerValue = layers[i].index;
                    changed = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collision Layer");

        return changed;
    }
}
