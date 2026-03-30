#include "PreviewInputHandler.hpp"
#include "../../camera/OrbitCamera.hpp"
#include <imgui.h>
#include <glm/glm.hpp>

namespace editor::preview
{
    void PreviewInputHandler::handleInput(OrbitCamera* camera, bool& isDraggingOrbit, bool& isDraggingPan)
    {
        bool isHovered = ImGui::IsWindowHovered();

        // RMB orbit start/stop
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            isDraggingOrbit = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            isDraggingOrbit = false;
        }

        // MMB pan start/stop
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            isDraggingPan = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
        {
            isDraggingPan = false;
        }

        if (!isHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        // Scroll zoom
        if (io.MouseWheel != 0.0f)
        {
            float zoomFactor = 1.0f - io.MouseWheel * camera->zoomSensitivity * 0.1f;
            camera->setDistance(camera->distance * zoomFactor);
            camera->updateMatrices();
        }

        // RMB drag: orbit
        if (isDraggingOrbit && ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            ImVec2 delta = io.MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                camera->yaw += delta.x * camera->orbitSensitivity;
                camera->pitch -= delta.y * camera->orbitSensitivity;
                camera->pitch = glm::clamp(camera->pitch, -89.0f, 89.0f);
                camera->updateMatrices();
            }
        }

        // MMB drag: pan
        if (isDraggingPan && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            ImVec2 delta = io.MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                const glm::mat4& view = camera->getViewMatrix();
                glm::vec3 right = glm::vec3(view[0][0], view[1][0], view[2][0]);
                glm::vec3 up = glm::vec3(view[0][1], view[1][1], view[2][1]);

                float panSpeed = camera->distance * 0.002f;
                camera->target -= right * (delta.x * panSpeed);
                camera->target += up * (delta.y * panSpeed);
                camera->updateMatrices();
            }
        }
    }
}
