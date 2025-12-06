#include "SceneGraph.hpp"
#include "ServiceLocator.hpp"
#include <imgui.h>
#include <cstring>

namespace windows
{
	void SceneGraph::draw()
	{
		auto sceneService = TRY_RESOLVE_SERVICE(services::ISceneService);
		if (!sceneService) {
			return;
		}

		if (ImGui::Begin("SceneGraph"))
		{
			// Get root entity through service
			auto rootHandle = sceneService->getRootEntity();
			if (rootHandle.isValid()) {
				drawEntityNode(rootHandle, *sceneService);
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
				drawDetails(selectedHandle, *sceneService);

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

	void SceneGraph::drawEntityNode(services::EntityHandle handle, services::ISceneService& sceneService)
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
				drawEntityNode(child, sceneService);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneGraph::drawDetails(services::EntityHandle handle, services::ISceneService& sceneService)
	{
		// Get entity data
		auto entityData = sceneService.getEntityData(handle);
		if (!entityData.has_value()) {
			return;
		}

		// Display name
		char buffer[256];
		std::strncpy(buffer, entityData->name.c_str(), sizeof(buffer));
		buffer[sizeof(buffer) - 1] = '\0';
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

		// IBL component - display only
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
