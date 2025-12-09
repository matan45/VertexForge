#include "SceneGraph.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include <imgui.h>

namespace windows
{
    SceneGraph::SceneGraph()
    {
        subscribeToEvents();
    }

    SceneGraph::~SceneGraph()
    {
        events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
    }

    void SceneGraph::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&) {
                onSceneCleared();
            });
    }

    void SceneGraph::onSceneCleared()
    {
        selectedHandle = services::EntityHandle::invalid();
    }

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
                ImGui::PushID("CameraComponent");

                bool removeCamera = false;

                // Component header with remove button (Unity-style)
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));

                bool isOpen = ImGui::CollapsingHeader("##CameraHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                // Component label after the arrow
                ImGui::SameLine();
                ImGui::Text("Camera");

                // Small X button on the right
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

                if (ImGui::Button("x##RemoveCamera", ImVec2(18, 18)))
                {
                    removeCamera = true;
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(6);

                if (isOpen)
                {
                    ImGui::Indent(10.0f);

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

                    ImGui::Unindent(10.0f);
                }

                ImGui::PopID();

                if (removeCamera)
                {
                    events::scene::RemoveCameraComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }
        }

        // IBL component
        events::scene::HasIBLComponentQuery hasIBLQuery;
        hasIBLQuery.entity = handle;
        bool hasIBL = dispatcher.query(hasIBLQuery);

        if (hasIBL)
        {
            events::scene::GetIBLDataQuery iblQuery;
            iblQuery.entity = handle;
            auto iblOpt = dispatcher.query(iblQuery);

            if (iblOpt.has_value())
            {
                ImGui::PushID("IBLComponent");

                bool removeIBL = false;

                // Component header with remove button (Unity-style)
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));

                bool isOpen = ImGui::CollapsingHeader("##IBLHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                // Component label after the arrow
                ImGui::SameLine();
                ImGui::Text("IBL");

                // Small X button on the right
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

                if (ImGui::Button("x##RemoveIBL", ImVec2(18, 18)))
                {
                    removeIBL = true;
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(6);

                if (isOpen)
                {
                    ImGui::Indent(10.0f);

                    // Display IBL file path (read-only)
                    ImGui::Text("File: %s", iblOpt->fileName.c_str());

                    ImGui::Unindent(10.0f);
                }

                ImGui::PopID();

                if (removeIBL)
                {
                    events::scene::RemoveIBLComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);

                    // Also remove from renderer
                    events::render::RemoveIBLCommand removeRenderCmd;
                    dispatcher.execute(removeRenderCmd);
                }
            }
        }

        // Add Component button (Unity-style)
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Center the button and style it
        float buttonWidth = ImGui::GetContentRegionAvail().x * 0.6f;
        float buttonOffset = (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonOffset);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 28)))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Popup menu for adding components
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            ImGui::TextDisabled("Components");
            ImGui::Separator();

            if (!hasCamera)
            {
                if (ImGui::Selectable("  Camera"))
                {
                    events::scene::AddCameraComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }

            // Show message if all components are added
            if (hasCamera)
            {
                ImGui::TextDisabled("All components added");
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
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
