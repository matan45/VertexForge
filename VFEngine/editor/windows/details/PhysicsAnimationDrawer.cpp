#include "print/Log.hpp"
#include "PhysicsAnimationDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
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
                    const auto& config = *configOpt;
                    data.physicsAnimationRef = asset::AssetRef::fromPath(path);
                    data.defaultMode = config.defaultMode;
                    data.collisionLayer = config.collisionLayer;
                    data.boneBodyMappings = config.boneBodyMappings;
                    data.jointLimits = config.jointLimits;
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
                data.defaultMode = types::PhysicsAnimationMode::Animated;
                data.collisionLayer = 1;
                data.boneBodyMappings.clear();
                data.jointLimits.clear();
                changed = true;
            }
        }
    }

    void PhysicsAnimationDrawer::drawConfigSummary(const services::PhysicsAnimationComponentData& data)
    {
        if (ImGui::CollapsingHeader("Config Summary", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            const char* modeStr = "Animated";
            switch (data.defaultMode)
            {
            case types::PhysicsAnimationMode::Kinematic: modeStr = "Kinematic"; break;
            case types::PhysicsAnimationMode::Ragdoll: modeStr = "Ragdoll"; break;
            default: break;
            }
            ImGui::Text("Default Mode: %s", modeStr);
            ImGui::Text("Collision Layer: %d", static_cast<int>(data.collisionLayer));
            ImGui::Text("Bone Mappings: %zu", data.boneBodyMappings.size());
            ImGui::Text("Joint Limits: %zu", data.jointLimits.size());

            if (!data.boneBodyMappings.empty() && ImGui::TreeNode("Mapped Bones"))
            {
                for (const auto& mapping : data.boneBodyMappings)
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
