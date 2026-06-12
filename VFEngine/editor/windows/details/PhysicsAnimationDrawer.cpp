#include "print/Log.hpp"
#include "PhysicsAnimationDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/physics/PhysicsAnimationEvents.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "physics/PhysicsAnimationAsset.hpp"
#include <imgui.h>

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

            drawFilePicker(handle, data, changed);

            if (data.physicsAnimationRef.isValid())
            {
                ImGui::Spacing();
                drawConfigSummary(data);
                ImGui::Spacing();
                drawRuntimeControls(handle, data);
            }

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

    void PhysicsAnimationDrawer::drawFilePicker(services::EntityHandle handle,
                                                 services::PhysicsAnimationComponentData& data,
                                                 bool& changed)
    {
        if (data.physicsAnimationRef.isValid())
        {
            std::string filename = data.physicsAnimationRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("Config: %s", filename.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", data.physicsAnimationRef.resolve().c_str());
            }
        }
        else
        {
            ImGui::TextDisabled("No physics animation config selected");
        }

        if (ImGui::Button("Select Config"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Physics Animation (*.vfPhysAnim)", L"*.vfPhysAnim"}});

            if (!path.empty())
            {
                auto configOpt = physics::PhysicsAnimationAsset::load(path);
                if (configOpt.has_value())
                {
                    data.physicsAnimationRef = asset::AssetRef::fromPath(path);
                    data.config = *configOpt;
                    changed = true;
                }
                else
                {
                    vfLogError("Failed to load physics animation config: {}", path);
                }
            }
        }

        if (data.physicsAnimationRef.isValid())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                data.physicsAnimationRef = asset::AssetRef::invalid();
                data.config = types::PhysicsAnimationConfig{};
                changed = true;
            }
        }
    }

    void PhysicsAnimationDrawer::drawRuntimeControls(services::EntityHandle handle,
                                                      const services::PhysicsAnimationComponentData& data)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Ragdoll instances only exist during play mode
        events::physicsAnimation::HasPhysicsAnimationQuery activeQuery;
        activeQuery.entity = handle;
        if (!dispatcher.query(activeQuery)) return;

        if (ImGui::CollapsingHeader("Runtime (Play Mode)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            events::physicsAnimation::GetPhysicsAnimationModeQuery modeQuery;
            modeQuery.entity = handle;
            int currentMode = static_cast<int>(dispatcher.query(modeQuery));

            const char* modeNames[] = {"Animated", "Kinematic", "Ragdoll", "Powered Ragdoll"};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##RuntimeMode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
            {
                events::physicsAnimation::SetPhysicsAnimationModeCommand cmd;
                cmd.entity = handle;
                cmd.mode = static_cast<types::PhysicsAnimationMode>(currentMode);
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch physics animation mode live");

            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##GlobalStrength", &globalStrength, 0.0f, 1.0f, "Global Strength: %.2f"))
            {
                events::physicsAnimation::SetGlobalMotorStrengthCommand cmd;
                cmd.entity = handle;
                cmd.strength = globalStrength;
                dispatcher.execute(cmd);
            }

            events::physicsAnimation::IsRagdollSettledQuery settledQuery;
            settledQuery.entity = handle;
            bool settled = dispatcher.query(settledQuery);
            ImGui::Text("Settled: %s", settled ? "yes" : "no");

            if (!data.config.boneBodyMappings.empty())
            {
                ImGui::Separator();

                if (selectedHitBone >= static_cast<int>(data.config.boneBodyMappings.size()))
                    selectedHitBone = 0;

                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##HitBone",
                                      data.config.boneBodyMappings[selectedHitBone].boneName.c_str()))
                {
                    for (size_t i = 0; i < data.config.boneBodyMappings.size(); ++i)
                    {
                        bool isSelected = static_cast<int>(i) == selectedHitBone;
                        if (ImGui::Selectable(data.config.boneBodyMappings[i].boneName.c_str(), isSelected))
                            selectedHitBone = static_cast<int>(i);
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bone for the test hit reaction");

                if (ImGui::Button("Test Hit Reaction", ImVec2(-1, 0)))
                {
                    events::physicsAnimation::HitReactionCommand cmd;
                    cmd.entity = handle;
                    cmd.boneName = data.config.boneBodyMappings[selectedHitBone].boneName;
                    cmd.impulse = glm::vec3(60.0f, 30.0f, 0.0f);
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Impulse + temporary motor dip on the selected bone's chain (powered ragdoll)");
            }

            ImGui::Unindent(10.0f);
        }
    }

    void PhysicsAnimationDrawer::drawConfigSummary(const services::PhysicsAnimationComponentData& data)
    {
        if (ImGui::CollapsingHeader("Config Summary", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            const char* modeStr = "Animated";
            switch (data.config.defaultMode)
            {
            case types::PhysicsAnimationMode::Kinematic: modeStr = "Kinematic"; break;
            case types::PhysicsAnimationMode::Ragdoll: modeStr = "Ragdoll"; break;
            case types::PhysicsAnimationMode::PoweredRagdoll: modeStr = "Powered Ragdoll"; break;
            default: break;
            }
            ImGui::Text("Default Mode: %s", modeStr);
            ImGui::Text("Collision Layer: %d", static_cast<int>(data.config.collisionLayer));
            ImGui::Text("Bone Mappings: %zu", data.config.boneBodyMappings.size());
            ImGui::Text("Joint Limits: %zu", data.config.jointLimits.size());
            ImGui::Text("Motor Overrides: %zu", data.config.boneMotors.size());

            if (!data.config.boneBodyMappings.empty() && ImGui::TreeNode("Mapped Bones"))
            {
                for (const auto& mapping : data.config.boneBodyMappings)
                {
                    const char* shapeStr = "Box";
                    switch (mapping.shape)
                    {
                    case types::ColliderShape::Sphere: shapeStr = "Sphere"; break;
                    case types::ColliderShape::Capsule: shapeStr = "Capsule"; break;
                    default: break;
                    }
                    ImGui::BulletText("%s (%s)", mapping.boneName.c_str(), shapeStr);
                }
                ImGui::TreePop();
            }

            ImGui::Unindent(10.0f);
        }
    }
}
