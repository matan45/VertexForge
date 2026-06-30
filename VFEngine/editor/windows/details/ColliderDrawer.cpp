#include "ColliderDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/lifecycle/AssetLifecycleEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "types/PhysicsTypes.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <exception>

namespace windows::details
{
    ColliderDrawer::~ColliderDrawer()
    {
        cancelConvexRegen.store(true);
        if (convexRegenFuture.valid())
            convexRegenFuture.wait();
    }

    bool ColliderDrawer::draw(services::EntityHandle handle)
    {
        pollRegenerationResult(handle, activeConvexRegenMeshPath);

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
            changed |= drawShapeParameters(handle, colliderData);
            ImGui::Spacing();
            changed |= drawPhysicsMaterial(colliderData);
            ImGui::Spacing();
            changed |= drawTriggerSettings(colliderData);
            ImGui::Spacing();
            changed |= drawCollisionLayer(colliderData);

            if (changed)
            {
                // Enforce TriangleMesh constraint: must be Static, auto-adjust if needed
                if (colliderData.shape == types::ColliderShape::TriangleMesh)
                {
                    events::scene::HasRigidBodyComponentQuery hasRbQuery;
                    hasRbQuery.entity = handle;
                    if (dispatcher.query(hasRbQuery))
                    {
                        events::scene::GetRigidBodyDataQuery rbQuery;
                        rbQuery.entity = handle;
                        auto rbOpt = dispatcher.query(rbQuery);
                        if (rbOpt.has_value() && rbOpt->type == types::RigidBodyType::Dynamic)
                        {
                            // Auto-adjust to Static
                            services::RigidBodyComponentData rbData = *rbOpt;
                            rbData.type = types::RigidBodyType::Static;
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
            colliderData.shape = static_cast<types::ColliderShape>(currentShape);
            changed = true;
        }

        return changed;
    }

    bool ColliderDrawer::drawShapeParameters(services::EntityHandle handle,
                                             services::ColliderComponentData& colliderData)
    {
        bool changed = false;

        switch (colliderData.shape)
        {
        case types::ColliderShape::Box:
            ImGui::Text("Box Dimensions:");
            if (ImGui::DragFloat3("Size", &colliderData.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Full size of the box collider (half-extents = size / 2)");
            }
            break;

        case types::ColliderShape::Sphere:
            ImGui::Text("Sphere Dimensions:");
            if (ImGui::DragFloat("Radius", &colliderData.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
            {
                changed = true;
            }
            break;

        case types::ColliderShape::Capsule:
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

        case types::ColliderShape::ConvexMesh:
        case types::ColliderShape::TriangleMesh:
            {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Uses mesh from Mesh Component");

                int submeshIdx = colliderData.submeshIndex;
                if (ImGui::InputInt("Submesh Index##Collider", &submeshIdx))
                {
                    if (submeshIdx < -1)
                        submeshIdx = -1;
                    colliderData.submeshIndex = submeshIdx;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("-1 = use all submeshes\n>= 0 = use specific submesh");
                }

                if (colliderData.shape == types::ColliderShape::TriangleMesh)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Triangle meshes are static only");
                }

                if (colliderData.shape == types::ColliderShape::ConvexMesh)
                {
                    ImGui::Spacing();
                    drawConvexRegenerationControls(handle, colliderData);
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

    bool ColliderDrawer::drawConvexRegenerationControls(
        services::EntityHandle handle,
        const services::ColliderComponentData& colliderData)
    {
        bool started = false;
        const std::string meshPath = resolveMeshPath(handle, colliderData);

        ImGui::Separator();
        ImGui::Text("Convex Decomposition");
        drawConvexRegenerationSettings();

        pollRegenerationResult(handle, meshPath);

        const bool running = convexRegenFuture.valid()
            && convexRegenFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;

        if (running)
        {
            ImGui::ProgressBar(std::clamp(convexRegenProgress.load(), 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
            if (ImGui::Button("Cancel Regeneration"))
                cancelConvexRegen.store(true);
        }
        else
        {
            if (meshPath.empty())
                ImGui::BeginDisabled();

            if (ImGui::Button("Regenerate Convex Decomposition"))
            {
                cancelConvexRegen.store(false);
                convexRegenProgress.store(0.0f);
                convexRegenStatus.clear();
                activeConvexRegenMeshPath = meshPath;
                activeConvexRegenEntity = handle;

                const int32_t submeshIndex = colliderData.submeshIndex;
                const auto config = convexRegenConfig;
                convexRegenFuture = std::async(std::launch::async,
                    [this, meshPath, submeshIndex, config]()
                    {
                        return types::ConvexDecompositionRegenerator::regenerate(
                            meshPath,
                            submeshIndex,
                            config,
                            [this](float progress, std::string_view)
                            {
                                convexRegenProgress.store(progress);
                            },
                            &cancelConvexRegen);
                    });
                started = true;
            }

            if (meshPath.empty())
            {
                ImGui::EndDisabled();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "No mesh path resolved");
            }
        }

        if (!convexRegenStatus.empty())
        {
            ImGui::TextWrapped("%s", convexRegenStatus.c_str());
        }

        return started;
    }

    bool ColliderDrawer::drawConvexRegenerationSettings()
    {
        bool changed = false;

        const char* presetNames[] = {"Fast", "Balanced", "Quality", "Custom"};
        int presetIndex = static_cast<int>(convexRegenConfig.vhacdPreset);
        if (ImGui::Combo("Quality Preset##ConvexRegen", &presetIndex, presetNames, IM_ARRAYSIZE(presetNames)))
        {
            convexRegenConfig.vhacdPreset = static_cast<importConfig::VHACDPreset>(presetIndex);
            changed = true;
        }

        const bool isCustom = convexRegenConfig.vhacdPreset == importConfig::VHACDPreset::Custom;
        if (!isCustom)
            ImGui::BeginDisabled();

        int maxHulls = static_cast<int>(convexRegenConfig.maxConvexHulls);
        if (ImGui::SliderInt("Max Hulls##ConvexRegen", &maxHulls, 1, 64))
        {
            convexRegenConfig.maxConvexHulls = static_cast<uint32_t>(maxHulls);
            changed = true;
        }

        int maxVerts = static_cast<int>(convexRegenConfig.maxVerticesPerHull);
        if (ImGui::SliderInt("Max Vertices Per Hull##ConvexRegen", &maxVerts, 8, 256))
        {
            convexRegenConfig.maxVerticesPerHull = static_cast<uint32_t>(maxVerts);
            changed = true;
        }

        if (ImGui::TreeNode("Advanced Parameters##ConvexRegen"))
        {
            int resolution = static_cast<int>(convexRegenConfig.vhacdResolution);
            if (ImGui::SliderInt("Resolution##ConvexRegen", &resolution, 10000, 500000))
            {
                convexRegenConfig.vhacdResolution = static_cast<uint32_t>(resolution);
                changed = true;
            }

            float minVolumeError = convexRegenConfig.minVolumePercentError;
            if (ImGui::SliderFloat("Min Volume Error %%##ConvexRegen", &minVolumeError, 0.1f, 10.0f, "%.1f"))
            {
                convexRegenConfig.minVolumePercentError = minVolumeError;
                changed = true;
            }

            int recursionDepth = static_cast<int>(convexRegenConfig.maxRecursionDepth);
            if (ImGui::SliderInt("Max Recursion Depth##ConvexRegen", &recursionDepth, 4, 16))
            {
                convexRegenConfig.maxRecursionDepth = static_cast<uint32_t>(recursionDepth);
                changed = true;
            }

            if (ImGui::Checkbox("Shrink Wrap##ConvexRegen", &convexRegenConfig.shrinkWrap))
                changed = true;

            ImGui::TreePop();
        }

        if (!isCustom)
            ImGui::EndDisabled();

        return changed;
    }

    std::string ColliderDrawer::resolveMeshPath(
        services::EntityHandle handle,
        const services::ColliderComponentData& colliderData) const
    {
        if (colliderData.meshRef.isValid())
            return colliderData.meshRef.resolve();

        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::GetMeshDataQuery meshQuery;
        meshQuery.entity = handle;
        try
        {
            auto meshData = dispatcher.query(meshQuery);
            if (meshData.has_value() && meshData->meshRef.isValid())
                return meshData->meshRef.resolve();
        }
        catch (const std::exception&)
        {
        }

        return {};
    }

    void ColliderDrawer::pollRegenerationResult(services::EntityHandle handle, const std::string& meshPath)
    {
        if (!convexRegenFuture.valid())
            return;

        if (convexRegenFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return;

        types::ConvexRegenerationResult result;
        try
        {
            result = convexRegenFuture.get();
        }
        catch (const std::exception& e)
        {
            convexRegenStatus = std::string("Convex decomposition failed: ") + e.what();
            return;
        }
        if (!result.success)
        {
            convexRegenStatus = "Convex decomposition failed: " + result.message;
            return;
        }

        convexRegenProgress.store(1.0f);
        convexRegenStatus = result.message + " (" + std::to_string(result.hullCount) + " hulls)";

        auto& dispatcher = events::EventDispatcher::instance();
        events::lifecycle::AssetReleaseReadyNotification releaseNotification;
        releaseNotification.path = activeConvexRegenMeshPath.empty()
            ? meshPath
            : activeConvexRegenMeshPath;
        releaseNotification.type = resource::AssetType::Mesh;
        dispatcher.publish(releaseNotification);

        const services::EntityHandle rebuildEntity = activeConvexRegenEntity.isValid()
            ? activeConvexRegenEntity
            : handle;
        try
        {
            events::physics::HasRigidBodyQuery hasBodyQuery;
            hasBodyQuery.entity = rebuildEntity;
            if (dispatcher.query(hasBodyQuery))
            {
                events::physics::CreatePhysicsBodyCommand rebuildCmd;
                rebuildCmd.entity = rebuildEntity;
                rebuildCmd.rebuild = true;
                dispatcher.execute(rebuildCmd);
            }
        }
        catch (const std::exception&)
        {
            // Physics service may not be registered in all editor contexts; cache eviction above is enough
            // for the next body creation to pick up the regenerated collider.
        }
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

        std::vector<types::CollisionLayer> layers;
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::physics::GetCollisionLayersQuery layersQuery;
            layers = dispatcher.query(layersQuery);
        }
        catch (...)
        {
            // Query failed, use default layers
        }

        if (layers.empty())
        {
            // Use default layer names if query failed
            layers = types::PhysicsSettings::createDefault().layers;
        }

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
