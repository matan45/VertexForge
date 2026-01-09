#include "ColliderDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/PhysicsSettingsEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>

namespace windows::details
{
    bool ColliderDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasColliderComponentQuery hasColliderQuery;
        hasColliderQuery.entity = handle;
        bool hasCollider = dispatcher.query(hasColliderQuery);

        if (!hasCollider)
        {
            return false;
        }

        events::scene::GetColliderDataQuery colliderQuery;
        colliderQuery.entity = handle;
        auto colliderOpt = dispatcher.query(colliderQuery);

        if (!colliderOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("ColliderComponent");

        bool removeCollider = false;
        bool isOpen = drawHeader(removeCollider);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::ColliderComponentData colliderData = *colliderOpt;
            bool changed = false;

            changed |= drawShapeSelection(colliderData);
            ImGui::Spacing();
            changed |= drawShapeParameters(colliderData);
            ImGui::Spacing();
            changed |= drawPhysicsMaterial(colliderData);
            ImGui::Spacing();
            changed |= drawTriggerSettings(colliderData);
            ImGui::Spacing();
            changed |= drawCollisionLayer(colliderData);

            if (changed)
            {
                // Enforce TriangleMesh constraint: must be Static, auto-adjust if needed
                if (colliderData.shape == services::ColliderShapeType::TriangleMesh)
                {
                    events::scene::HasRigidBodyComponentQuery hasRbQuery;
                    hasRbQuery.entity = handle;
                    if (dispatcher.query(hasRbQuery))
                    {
                        events::scene::GetRigidBodyDataQuery rbQuery;
                        rbQuery.entity = handle;
                        auto rbOpt = dispatcher.query(rbQuery);
                        if (rbOpt.has_value() && rbOpt->type == services::RigidBodyTypeData::Dynamic)
                        {
                            // Auto-adjust to Static
                            services::RigidBodyComponentData rbData = *rbOpt;
                            rbData.type = services::RigidBodyTypeData::Static;
                            events::scene::SetRigidBodyDataCommand rbCmd;
                            rbCmd.entity = handle;
                            rbCmd.rigidBodyData = rbData;
                            dispatcher.execute(rbCmd);
                        }
                    }
                }

                events::scene::SetColliderDataCommand cmd;
                cmd.entity = handle;
                cmd.colliderData = colliderData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeCollider)
        {
            events::scene::RemoveColliderComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool ColliderDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##ColliderHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Collider");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveCollider", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool ColliderDrawer::drawShapeSelection(services::ColliderComponentData& colliderData)
    {
        bool changed = false;

        const char* shapeNames[] = {"Box", "Sphere", "Capsule", "Convex Mesh", "Triangle Mesh"};
        int currentShape = static_cast<int>(colliderData.shape);

        if (ImGui::Combo("Shape", &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            colliderData.shape = static_cast<services::ColliderShapeType>(currentShape);
            changed = true;
        }

        return changed;
    }

    bool ColliderDrawer::drawShapeParameters(services::ColliderComponentData& colliderData)
    {
        bool changed = false;

        switch (colliderData.shape)
        {
        case services::ColliderShapeType::Box:
            ImGui::Text("Box Dimensions:");
            if (ImGui::DragFloat3("Size (Half Extents)", &colliderData.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Half-extents of the box collider");
            }
            break;

        case services::ColliderShapeType::Sphere:
            ImGui::Text("Sphere Dimensions:");
            if (ImGui::DragFloat("Radius", &colliderData.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                changed = true;
            }
            break;

        case services::ColliderShapeType::Capsule:
            ImGui::Text("Capsule Dimensions:");
            if (ImGui::DragFloat("Radius", &colliderData.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::DragFloat("Height", &colliderData.height, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Total height of the capsule");
            }
            break;

        case services::ColliderShapeType::ConvexMesh:
        case services::ColliderShapeType::TriangleMesh:
        {
            ImGui::Text("Mesh Collider:");
            if (!colliderData.meshPath.empty())
            {
                std::string filename = colliderData.meshPath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                {
                    filename = filename.substr(lastSlash + 1);
                }
                ImGui::Text("File: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No mesh file selected");
            }

            if (ImGui::Button("Select Mesh##Collider"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Mesh Files (*.vfMesh)", L"*.vfMesh"}});
                if (!path.empty())
                {
                    colliderData.meshPath = path;
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (colliderData.meshPath.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##MeshCollider"))
            {
                colliderData.meshPath = "";
                changed = true;
            }
            if (colliderData.meshPath.empty()) ImGui::EndDisabled();

            if (colliderData.shape == services::ColliderShapeType::TriangleMesh)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Triangle meshes are static only");
            }
            break;
        }
        }

        ImGui::Spacing();
        ImGui::Text("Offset:");
        if (ImGui::DragFloat3("Offset", &colliderData.offset.x, 0.01f, -100.0f, 100.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Local offset from entity center");
        }

        return changed;
    }

    bool ColliderDrawer::drawPhysicsMaterial(services::ColliderComponentData& colliderData)
    {
        bool changed = false;

        ImGui::Text("Physics Material:");
        ImGui::Indent(10.0f);

        if (ImGui::SliderFloat("Friction", &colliderData.friction, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Surface friction (0 = ice, 1 = rubber)");
        }

        if (ImGui::SliderFloat("Restitution", &colliderData.restitution, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Bounciness (0 = no bounce, 1 = perfect bounce)");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ColliderDrawer::drawTriggerSettings(services::ColliderComponentData& colliderData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Is Trigger", &colliderData.isTrigger))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Trigger colliders detect overlaps but don't cause physical collisions");
        }

        return changed;
    }

    bool ColliderDrawer::drawCollisionLayer(services::ColliderComponentData& colliderData)
    {
        bool changed = false;

        auto& dispatcher = events::EventDispatcher::instance();

        // Get available collision layers
        events::physics::GetCollisionLayersQuery layersQuery;
        auto layers = dispatcher.query(layersQuery);

        if (layers.empty())
        {
            ImGui::TextDisabled("No collision layers available");
            return false;
        }

        ImGui::Text("Collision Layer:");

        // Build combo items
        std::vector<std::string> layerNames;
        int currentIndex = 0;

        for (size_t i = 0; i < layers.size(); ++i)
        {
            layerNames.push_back(layers[i].name + " [" + std::to_string(layers[i].index) + "]");
            if (layers[i].index == colliderData.collisionLayer)
            {
                currentIndex = static_cast<int>(i);
            }
        }

        // Create combo
        if (ImGui::BeginCombo("##CollisionLayer", layerNames[currentIndex].c_str()))
        {
            for (size_t i = 0; i < layers.size(); ++i)
            {
                bool isSelected = (layers[i].index == colliderData.collisionLayer);
                if (ImGui::Selectable(layerNames[i].c_str(), isSelected))
                {
                    colliderData.collisionLayer = layers[i].index;
                    changed = true;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Collision layer determines which objects this collider can interact with");
        }

        return changed;
    }
}
