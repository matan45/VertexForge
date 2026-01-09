#include "RigidBodyDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool RigidBodyDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasRigidBodyComponentQuery hasRigidBodyQuery;
        hasRigidBodyQuery.entity = handle;
        bool hasRigidBody = dispatcher.query(hasRigidBodyQuery);

        if (!hasRigidBody)
        {
            return false;
        }

        events::scene::GetRigidBodyDataQuery rigidBodyQuery;
        rigidBodyQuery.entity = handle;
        auto rigidBodyOpt = dispatcher.query(rigidBodyQuery);

        if (!rigidBodyOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("RigidBodyComponent");

        bool removeRigidBody = false;
        bool isOpen = drawHeader(removeRigidBody);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::RigidBodyComponentData rigidBodyData = *rigidBodyOpt;
            bool changed = false;

            changed |= drawBodyType(rigidBodyData);
            ImGui::Spacing();
            changed |= drawMassSettings(rigidBodyData);
            ImGui::Spacing();
            changed |= drawDampingSettings(rigidBodyData);
            ImGui::Spacing();
            changed |= drawConstraints(rigidBodyData);

            if (changed)
            {
                // Enforce constraint: Dynamic body type not allowed with TriangleMesh collider
                if (rigidBodyData.type == services::RigidBodyTypeData::Dynamic)
                {
                    events::scene::HasColliderComponentQuery hasColQuery;
                    hasColQuery.entity = handle;
                    if (dispatcher.query(hasColQuery))
                    {
                        events::scene::GetColliderDataQuery colQuery;
                        colQuery.entity = handle;
                        auto colOpt = dispatcher.query(colQuery);
                        if (colOpt.has_value() && colOpt->shape == services::ColliderShapeType::TriangleMesh)
                        {
                            // Revert to Static - TriangleMesh cannot be Dynamic
                            rigidBodyData.type = services::RigidBodyTypeData::Static;
                        }
                    }
                }

                events::scene::SetRigidBodyDataCommand cmd;
                cmd.entity = handle;
                cmd.rigidBodyData = rigidBodyData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeRigidBody)
        {
            events::scene::RemoveRigidBodyComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool RigidBodyDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##RigidBodyHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Rigid Body");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveRigidBody", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool RigidBodyDrawer::drawBodyType(services::RigidBodyComponentData& rigidBodyData)
    {
        bool changed = false;

        const char* typeNames[] = {"Static", "Dynamic", "Kinematic"};
        int currentType = static_cast<int>(rigidBodyData.type);

        if (ImGui::Combo("Body Type", &currentType, typeNames, IM_ARRAYSIZE(typeNames)))
        {
            rigidBodyData.type = static_cast<services::RigidBodyTypeData>(currentType);
            changed = true;
        }

        // Show description based on type
        switch (rigidBodyData.type)
        {
        case services::RigidBodyTypeData::Static:
            ImGui::TextDisabled("Static: Never moves, optimized for world geometry");
            break;
        case services::RigidBodyTypeData::Dynamic:
            ImGui::TextDisabled("Dynamic: Fully simulated with physics");
            break;
        case services::RigidBodyTypeData::Kinematic:
            ImGui::TextDisabled("Kinematic: Script-controlled, affects dynamic bodies");
            break;
        }

        return changed;
    }

    bool RigidBodyDrawer::drawMassSettings(services::RigidBodyComponentData& rigidBodyData)
    {
        bool changed = false;

        // Mass is only relevant for dynamic bodies
        bool isStatic = rigidBodyData.type == services::RigidBodyTypeData::Static;
        if (isStatic) ImGui::BeginDisabled();

        ImGui::Text("Mass Settings:");
        ImGui::Indent(10.0f);

        if (ImGui::DragFloat("Mass (kg)", &rigidBodyData.mass, 0.1f, 0.001f, 10000.0f, "%.3f"))
        {
            if (rigidBodyData.mass < 0.001f) rigidBodyData.mass = 0.001f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Mass of the rigid body in kilograms");
        }

        ImGui::Unindent(10.0f);

        if (isStatic) ImGui::EndDisabled();

        return changed;
    }

    bool RigidBodyDrawer::drawDampingSettings(services::RigidBodyComponentData& rigidBodyData)
    {
        bool changed = false;

        bool isStatic = rigidBodyData.type == services::RigidBodyTypeData::Static;
        if (isStatic) ImGui::BeginDisabled();

        ImGui::Text("Damping:");
        ImGui::Indent(10.0f);

        if (ImGui::SliderFloat("Linear Damping", &rigidBodyData.linearDamping, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Reduces linear velocity over time (air resistance)");
        }

        if (ImGui::SliderFloat("Angular Damping", &rigidBodyData.angularDamping, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Reduces angular velocity over time (rotational friction)");
        }

        ImGui::Unindent(10.0f);

        if (isStatic) ImGui::EndDisabled();

        return changed;
    }

    bool RigidBodyDrawer::drawConstraints(services::RigidBodyComponentData& rigidBodyData)
    {
        bool changed = false;

        bool isStatic = rigidBodyData.type == services::RigidBodyTypeData::Static;
        if (isStatic) ImGui::BeginDisabled();

        ImGui::Text("Constraints:");
        ImGui::Indent(10.0f);

        ImGui::Text("Freeze Position:");
        ImGui::SameLine();
        if (ImGui::Checkbox("X##FreezePos", &rigidBodyData.freezePositionX)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Y##FreezePos", &rigidBodyData.freezePositionY)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Z##FreezePos", &rigidBodyData.freezePositionZ)) changed = true;

        ImGui::Text("Freeze Rotation:");
        ImGui::SameLine();
        if (ImGui::Checkbox("X##FreezeRot", &rigidBodyData.freezeRotationX)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Y##FreezeRot", &rigidBodyData.freezeRotationY)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Z##FreezeRot", &rigidBodyData.freezeRotationZ)) changed = true;

        ImGui::Unindent(10.0f);

        if (isStatic) ImGui::EndDisabled();

        return changed;
    }
}
