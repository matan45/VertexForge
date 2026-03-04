#include "EditorCameraWindow.hpp"
#include "../../camera/EditorCamera.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows
{
    void EditorCameraWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Editor Camera Settings", &visible))
        {
            if (!editorCameraRef)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Editor Camera not available");
                ImGui::End();
                return;
            }

            ImGui::Text("Transform");
            ImGui::Separator();

            // Position
            ImGui::Text("Position");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat3("##Position", &editorCameraRef->position.x, 0.1f))
            {
                editorCameraRef->updateViewMatrix();
            }
            ImGui::PopItemWidth();

            // Rotation
            ImGui::Text("Rotation (degrees)");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat3("##Rotation", &editorCameraRef->rotation.x, 0.5f))
            {
                // Clamp pitch
                editorCameraRef->rotation.x = std::clamp(editorCameraRef->rotation.x, -89.0f, 89.0f);
                editorCameraRef->updateViewMatrix();
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Projection");
            ImGui::Separator();

            // Field of View
            ImGui::Text("Field of View");
            ImGui::PushItemWidth(-1);
            if (ImGui::SliderFloat("##FOV", &editorCameraRef->fieldOfView, 30.0f, 120.0f, "%.1f"))
            {
                editorCameraRef->updateProjectionMatrix();
            }
            ImGui::PopItemWidth();

            // Near Plane
            ImGui::Text("Near Plane");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##NearPlane", &editorCameraRef->nearPlane, 0.01f, 0.001f, 10.0f, "%.3f"))
            {
                editorCameraRef->updateProjectionMatrix();
            }
            ImGui::PopItemWidth();

            // Far Plane
            ImGui::Text("Far Plane");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##FarPlane", &editorCameraRef->farPlane, 1.0f, 10.0f, 100000.0f, "%.1f"))
            {
                editorCameraRef->updateProjectionMatrix();
            }
            ImGui::PopItemWidth();

            // Aspect Ratio (read-only, set by viewport)
            ImGui::Text("Aspect Ratio");
            ImGui::PushItemWidth(-1);
            ImGui::BeginDisabled();
            ImGui::InputFloat("##AspectRatio", &editorCameraRef->aspectRatio, 0, 0, "%.3f");
            ImGui::EndDisabled();
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Navigation");
            ImGui::Separator();

            // Move Speed
            ImGui::Text("Move Speed");
            ImGui::PushItemWidth(-1);
            ImGui::SliderFloat("##MoveSpeed", &editorCameraRef->moveSpeed, 0.5f, 50.0f, "%.1f");
            ImGui::PopItemWidth();

            // Mouse Sensitivity
            ImGui::Text("Mouse Sensitivity");
            ImGui::PushItemWidth(-1);
            ImGui::SliderFloat("##MouseSensitivity", &editorCameraRef->mouseSensitivity, 0.01f, 0.5f, "%.3f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            if (ImGui::Button("Reset to Default", ImVec2(-1, 0)))
            {
                editorCameraRef->position = glm::vec3(0.0f, 2.0f, 5.0f);
                editorCameraRef->rotation = glm::vec3(0.0f);
                editorCameraRef->fieldOfView = 60.0f;
                editorCameraRef->nearPlane = 0.1f;
                editorCameraRef->farPlane = 1000.0f;
                editorCameraRef->moveSpeed = 5.0f;
                editorCameraRef->mouseSensitivity = 0.1f;
                editorCameraRef->updateViewMatrix();
                editorCameraRef->updateProjectionMatrix();
            }
        }
        ImGui::End();
    }
}
