#include "SceneGraph.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

namespace windows
{
    void SceneGraph::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (ImGui::Begin("SceneGraph"))
        {
            // Query root entity through event system
            events::scene::GetSceneHierarchyQuery hierarchyQuery;
            auto hierarchy = dispatcher.query(hierarchyQuery);

            if (!hierarchy.entities.empty())
            {
                // Root is first entity
                auto rootHandle = hierarchy.entities[0].handle;
                drawEntityNode(rootHandle);
            }

            // Right-click context menu for adding/removing entities
            if (ImGui::BeginPopupContextWindow())
            {
                if (ImGui::MenuItem("Add New Entity"))
                {
                    // Get root for default parent
                    events::scene::GetSceneHierarchyQuery rootQuery;
                    auto rootHierarchy = dispatcher.query(rootQuery);
                    services::EntityHandle parentHandle = selectedHandle.isValid()
                                                              ? selectedHandle
                                                              : (rootHierarchy.entities.empty()
                                                                     ? services::EntityHandle::invalid()
                                                                     : rootHierarchy.entities[0].handle);

                    events::scene::CreateEntityCommand cmd;
                    cmd.name = "New Entity";
                    cmd.parent = parentHandle.isValid() ? std::optional{parentHandle} : std::nullopt;
                    dispatcher.execute(cmd);
                }

                // Prevent deleting the root entity
                if (selectedHandle.isValid())
                {
                    events::scene::GetSceneHierarchyQuery rootCheckQuery;
                    auto rootCheckHierarchy = dispatcher.query(rootCheckQuery);
                    bool isRoot = !rootCheckHierarchy.entities.empty() &&
                        rootCheckHierarchy.entities[0].handle.id == selectedHandle.id;

                    if (!isRoot && ImGui::MenuItem("Remove Selected Entity"))
                    {
                        events::scene::DeleteEntityCommand cmd;
                        cmd.entity = selectedHandle;
                        dispatcher.execute(cmd);
                        selectedHandle = services::EntityHandle::invalid();
                    }
                }

                ImGui::EndPopup();
            }
        }
        ImGui::End();

        // Show the selected entity's components in the "Details" window
        if (ImGui::Begin("Details"))
        {
            if (selectedHandle.isValid())
            {
                drawDetails(selectedHandle);

                if (ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::MenuItem("Add Camera Component"))
                    {
                        events::scene::AddCameraComponentCommand cmd;
                        cmd.entity = selectedHandle;
                        dispatcher.execute(cmd);
                    }
                    if (ImGui::MenuItem("Remove Camera Component"))
                    {
                        events::scene::RemoveCameraComponentCommand cmd;
                        cmd.entity = selectedHandle;
                        dispatcher.execute(cmd);
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::End();
    }

    void SceneGraph::drawEntityNode(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::PushID(static_cast<int>(handle.id));

        // Query entity data
        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        std::string entityName = entityDataOpt.has_value() ? entityDataOpt->name : "Unknown";

        ImGuiTreeNodeFlags flags = (selectedHandle.id == handle.id) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;

        bool nodeOpen = ImGui::TreeNodeEx((void*)handle.id, flags, "%s", entityName.c_str());

        // Select the entity when clicked
        if (ImGui::IsItemClicked())
        {
            selectedHandle = handle;

            // Publish selection through command
            events::scene::SelectEntityCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("DND_ENTITY_HANDLE", &handle, sizeof(services::EntityHandle));
            ImGui::Text("Move %s", entityName.c_str());
            ImGui::EndDragDropSource();
        }

        dragDropEntity(handle);

        // If the entity has children, recursively draw them
        if (nodeOpen)
        {
            if (entityDataOpt.has_value())
            {
                for (const auto& child : entityDataOpt->children)
                {
                    drawEntityNode(child);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneGraph::drawDetails(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Query entity data
        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        if (!entityDataOpt.has_value())
        {
            return;
        }

        // Display name
        char buffer[256];
        std::strncpy(buffer, entityDataOpt->name.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
        {
            events::scene::SetEntityNameCommand cmd;
            cmd.entity = handle;
            cmd.newName = buffer;
            dispatcher.execute(cmd);
        }

        ImGui::Separator();

        // Transform component
        events::scene::GetTransformQuery transformQuery;
        transformQuery.entity = handle;
        auto transformOpt = dispatcher.query(transformQuery);

        if (transformOpt.has_value())
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                services::TransformData transform = *transformOpt;
                bool changed = false;

                changed |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
                changed |= ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.1f);
                changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);

                if (changed)
                {
                    events::scene::SetTransformCommand cmd;
                    cmd.entity = handle;
                    cmd.transform = transform;
                    dispatcher.execute(cmd);
                }
            }
        }

        // Camera component
        events::scene::HasCameraComponentQuery hasCameraQuery;
        hasCameraQuery.entity = handle;
        bool hasCamera = dispatcher.query(hasCameraQuery);

        if (hasCamera)
        {
            events::scene::GetCameraDataQuery cameraQuery;
            cameraQuery.entity = handle;
            auto cameraOpt = dispatcher.query(cameraQuery);

            if (cameraOpt.has_value())
            {
                if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    services::CameraData camera = *cameraOpt;
                    bool changed = false;

                    changed |= ImGui::DragFloat("Field of View", &camera.fieldOfView, 1.0f, 1.0, 180.0f);
                    changed |= ImGui::DragFloat("Near Plane", &camera.nearPlane,  0.01f, 0.01f, camera.farPlane - 0.1f);
                    changed |= ImGui::DragFloat("Far Plane", &camera.farPlane, 0.1f, camera.nearPlane + 0.1f, 10000.0f);
                    changed |= ImGui::DragFloat("Aspect Ratio", &camera.aspectRatio, 0.01f, 0.1f, 10.0f);
                    changed |= ImGui::Checkbox("Perspective", &camera.isPerspective);

                    if (!camera.isPerspective)
                    {
                        changed |= ImGui::DragFloat("Orthographic Size", &camera.orthoSize, 0.1f, 0.1f, 1000.0f);
                    }

                    if (changed)
                    {
                        events::scene::SetCameraDataCommand cmd;
                        cmd.entity = handle;
                        cmd.cameraData = camera;
                        dispatcher.execute(cmd);
                    }
                }
            }
        }
    }

    void SceneGraph::dragDropEntity(services::EntityHandle handle)
    {
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_HANDLE"))
            {
                services::EntityHandle draggedHandle = *(services::EntityHandle*)payload->Data;

                if (draggedHandle.id != handle.id)
                {
                    auto& dispatcher = events::EventDispatcher::instance();

                    events::scene::ReparentEntityCommand cmd;
                    cmd.entity = draggedHandle;
                    cmd.newParent = handle;
                    dispatcher.execute(cmd);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
}
