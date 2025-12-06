#include "SceneGraph.hpp"
#include "components/Components.hpp"
#include "ServiceLocator.hpp"
#include <imgui.h>

namespace windows
{
    SceneGraph::SceneGraph(std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem):
    sceneGraphSystem{sceneGraphSystem}
    {
    }

    void SceneGraph::draw()
    {
        if (useServices) {
            drawWithServices();
            return;
        }

        //add some styling here
        if (ImGui::Begin("SceneGraph"))
        {
            // Start with the root entity (this assumes you have a SceneGraph object with a root)
            if (scene::EntityRegistry::getRegistry().valid(sceneGraphSystem->GetRoot().getHandle()))
            {
                drawEntityNode(sceneGraphSystem->GetRoot());
            }

            // Right-click context menu for adding/removing entities
            if (ImGui::BeginPopupContextWindow())
            {
                // Menu item to add a new entity
                if (ImGui::MenuItem("Add New Entity"))
                {
                    scene::Entity newEntity("New Entity");

                    // Add as a child to the selected entity if there is one, else add to root
                    if (selected != entt::null)
                    {
                        scene::Entity parent(selected);
                        sceneGraphSystem->addChild(parent, newEntity);
                    }
                    else
                    {
                        scene::Entity root = sceneGraphSystem->GetRoot();
                        sceneGraphSystem->addChild(root, newEntity);
                    }
                }

                // Prevent deleting the root entity
                if (selected != entt::null && selected != sceneGraphSystem->GetRoot().getHandle())
                {
                    if (ImGui::MenuItem("Remove Selected Entity"))
                    {
                        // Remove the selected entity from the scene graph
                        scene::Entity selectedToRemove(selected);
                        sceneGraphSystem->removeEntity(selectedToRemove);
                        selected = entt::null; // Clear the selection after removal
                    }
                }

                ImGui::EndPopup();
            }
        }
        ImGui::End();

        // Show the selected entity's components in the "Details" window
        if (ImGui::Begin("Details"))
        {
            if (selected != entt::null)
            {
                drawDetails(selected);
                if (ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::MenuItem("Add Camera Component"))
                    {
                        auto entityObject = scene::Entity(selected);
                        // Ensure entity has TransformComponent (required for camera updates)
                        if (!entityObject.hasComponent<components::TransformComponent>()) {
                            entityObject.addComponent<components::TransformComponent>();
                        }
                        entityObject.addComponent<components::CameraComponent>();
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::End();
    }

    void SceneGraph::drawEntityNode(scene::Entity entity)
    {
        ImGui::PushID(static_cast<int>(entity.getHandle()));

        std::string entityName = entity.getName(); // Get entity name

        ImGuiTreeNodeFlags flags = (selected == entity.getHandle()) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;


        bool nodeOpen = ImGui::TreeNodeEx((void*)(uint64_t)entity.getHandle(), flags, "%s", entityName.c_str());

        // Select the entity when clicked
        if (ImGui::IsItemClicked())
        {
            selected = entity.getHandle();
        }

        // Drag source: Start dragging the entity
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("DND_ENTITY", &entity, sizeof(scene::Entity)); // Tag it with "DND_ENTITY"
            ImGui::Text("Move %s", entityName.c_str()); // Show name of entity being dragged
            ImGui::EndDragDropSource();
        }

        // Drag target: Drop onto this entity (to make it a parent)
        drawDragDropTarget(entity);


        // If the entity has children, recursively draw them
        if (nodeOpen)
        {
            if (entity.hasComponent<components::ChildrenComponent>())
            {
                for (auto child : entity.getChildren())
                {
                    drawEntityNode(child); // Recursively draw child nodes
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneGraph::drawDragDropTarget(scene::Entity entity) const
    {
        //root node should not move and the root name need to be quince
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY"))
            {
                // Retrieve the dropped entity handle
                scene::Entity droppedEntity = *(scene::Entity*)payload->Data;

                // Set the parent-child relationship
                if (droppedEntity.getHandle() != entity.getHandle())
                {
                    sceneGraphSystem->moveEntity(droppedEntity, entity);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneGraph::drawDetails(entt::entity entity) const
    {
        auto entityObject = scene::Entity(entity);

        //maybe need pushID here
        // Display the name component first if it exists
        if (entityObject.hasComponent<components::NameComponent>())
        {
            auto const& nameComponent = entityObject.getComponent<components::NameComponent>();
            char buffer[256];
            strcpy(buffer, nameComponent.name.c_str());
            if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            {
                entityObject.setName(buffer); // Update the name
            }
        }

        ImGui::Separator(); // Separate components

        // Dynamically iterate over all attached components
        drawDynamicComponent(entityObject);
    }

    void SceneGraph::drawDynamicComponent(scene::Entity entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        // Handle known component types
        if (registry.all_of<components::TransformComponent>(entity.getHandle()))
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& transform = registry.get<components::TransformComponent>(entity.getHandle());
                ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.1f);
                ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
                transform.isDirty = true;
            }
        }
        // Handle Camera component
        if (registry.all_of<components::CameraComponent>(entity.getHandle()))
        {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& camera = registry.get<components::CameraComponent>(entity.getHandle());

                // Toggle between Perspective and Orthographic
                bool isPerspective = camera.isPerspective;
                if (ImGui::Checkbox("Perspective", &isPerspective))
                {
                    camera.isPerspective = isPerspective;
                    camera.updateProjectionMatrix();
                }

                // Perspective settings
                if (camera.isPerspective)
                {
                    if (ImGui::DragFloat("Field of View", &camera.fieldOfView, 0.1f, 1.0f, 179.0f))
                    {
                        camera.updateProjectionMatrix();
                    }
                }
                // Orthographic settings
                else
                {
                    if (ImGui::DragFloat("Orthographic Size", &camera.orthoSize, 0.1f, 0.1f, 1000.0f))
                    {
                        camera.updateProjectionMatrix();
                    }
                }

                // Near and Far Plane settings
                if (ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.01f, camera.farPlane - 0.1f))
                {
                    camera.updateProjectionMatrix();
                }
                if (ImGui::DragFloat("Far Plane", &camera.farPlane, 0.1f, camera.nearPlane + 0.1f, 10000.0f))
                {
                    camera.updateProjectionMatrix();
                }

                // Aspect Ratio
                if (ImGui::DragFloat("Aspect Ratio", &camera.aspectRatio, 0.01f, 0.1f, 10.0f))
                {
                    camera.updateProjectionMatrix();
                }

                // Remove Camera component button
                if (ImGui::Button("Remove##Camera"))
                {
                    entity.removeComponent<components::CameraComponent>();
                }
            }
        }

        // Handle Ibl component
		if (registry.all_of<components::IBLComponent>(entity.getHandle()))
		{
			if (ImGui::CollapsingHeader("IBL", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto const& ibl = registry.get<components::IBLComponent>(entity.getHandle());
                ImGui::BulletText("IBL image Path");
                ImGui::Text(ibl.fileName.c_str());
			}
		}
    }

    // ========== Service-based methods ==========

    void SceneGraph::drawWithServices()
    {
        auto sceneService = services::ServiceLocator::instance().tryGet<services::ISceneService>();
        if (!sceneService) {
            return;
        }

        if (ImGui::Begin("SceneGraph"))
        {
            // Get root entity through service
            auto rootHandle = sceneService->getRootEntity();
            if (rootHandle.isValid()) {
                drawEntityNodeWithServices(rootHandle, *sceneService);
            }

            // Right-click context menu for adding/removing entities
            if (ImGui::BeginPopupContextWindow())
            {
                if (ImGui::MenuItem("Add New Entity"))
                {
                    // Add as child to selected entity, or root if none selected
                    if (selectedHandle.isValid()) {
                        sceneService->createEntity("New Entity", selectedHandle);
                    } else {
                        sceneService->createEntity("New Entity", rootHandle);
                    }
                }

                // Prevent deleting the root entity
                if (selectedHandle.isValid() && selectedHandle.id != rootHandle.id)
                {
                    if (ImGui::MenuItem("Remove Selected Entity"))
                    {
                        sceneService->deleteEntity(selectedHandle, true);
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
                drawDetailsWithServices(selectedHandle, *sceneService);

                if (ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::MenuItem("Add Camera Component"))
                    {
                        sceneService->addCameraComponent(selectedHandle);
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::End();
    }

    void SceneGraph::drawEntityNodeWithServices(services::EntityHandle handle, services::ISceneService& sceneService)
    {
        ImGui::PushID(static_cast<int>(handle.id));

        // Get entity data through service
        auto entityData = sceneService.getEntityData(handle);
        std::string entityName = entityData.has_value() ? entityData->name : "Unknown";

        ImGuiTreeNodeFlags flags = (selectedHandle.id == handle.id) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;

        bool nodeOpen = ImGui::TreeNodeEx((void*)(uint64_t)handle.id, flags, "%s", entityName.c_str());

        // Select the entity when clicked
        if (ImGui::IsItemClicked())
        {
            selectedHandle = handle;
        }

        // Drag source: Start dragging the entity
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("DND_ENTITY_HANDLE", &handle, sizeof(services::EntityHandle));
            ImGui::Text("Move %s", entityName.c_str());
            ImGui::EndDragDropSource();
        }

        // Drag target: Drop onto this entity (to make it a parent)
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_HANDLE"))
            {
                services::EntityHandle droppedHandle = *(services::EntityHandle*)payload->Data;
                if (droppedHandle.id != handle.id)
                {
                    sceneService.reparentEntity(droppedHandle, handle);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // If the entity has children, recursively draw them
        if (nodeOpen)
        {
            auto children = sceneService.getChildren(handle);
            for (const auto& child : children)
            {
                drawEntityNodeWithServices(child, sceneService);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneGraph::drawDetailsWithServices(services::EntityHandle handle, services::ISceneService& sceneService)
    {
        // Get entity data
        auto entityData = sceneService.getEntityData(handle);
        if (!entityData.has_value()) {
            return;
        }

        // Display name
        char buffer[256];
        strcpy(buffer, entityData->name.c_str());
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
        {
            sceneService.setEntityName(handle, buffer);
        }

        ImGui::Separator();

        // Transform component
        auto transformOpt = sceneService.getTransform(handle);
        if (transformOpt.has_value())
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                services::TransformData transform = *transformOpt;
                bool changed = false;

                changed |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
                changed |= ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.1f);
                changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);

                if (changed) {
                    sceneService.setTransform(handle, transform);
                }
            }
        }

        // Camera component
        auto cameraOpt = sceneService.getCameraData(handle);
        if (cameraOpt.has_value())
        {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                services::CameraData camera = *cameraOpt;
                bool changed = false;

                if (ImGui::Checkbox("Perspective", &camera.isPerspective))
                {
                    changed = true;
                }

                if (camera.isPerspective)
                {
                    changed |= ImGui::DragFloat("Field of View", &camera.fieldOfView, 0.1f, 1.0f, 179.0f);
                }
                else
                {
                    changed |= ImGui::DragFloat("Orthographic Size", &camera.orthoSize, 0.1f, 0.1f, 1000.0f);
                }

                changed |= ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.01f, camera.farPlane - 0.1f);
                changed |= ImGui::DragFloat("Far Plane", &camera.farPlane, 0.1f, camera.nearPlane + 0.1f, 10000.0f);
                changed |= ImGui::DragFloat("Aspect Ratio", &camera.aspectRatio, 0.01f, 0.1f, 10.0f);

                if (changed) {
                    sceneService.setCameraData(handle, camera);
                }

                if (ImGui::Button("Remove##Camera"))
                {
                    sceneService.removeCameraComponent(handle);
                }
            }
        }

        // IBL component - display only (read-only for now)
        auto iblOpt = sceneService.getIBLData(handle);
        if (iblOpt.has_value())
        {
            if (ImGui::CollapsingHeader("IBL", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BulletText("IBL image Path");
                ImGui::Text("%s", iblOpt->fileName.c_str());
            }
        }
    }
}
