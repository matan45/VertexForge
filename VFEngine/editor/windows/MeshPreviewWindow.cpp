#include "MeshPreviewWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include "../../graphics/controllers/MeshPreviewController.hpp"
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

namespace windows
{
    MeshPreviewWindow::MeshPreviewWindow(const std::string& meshFilePath)
        : meshPath(meshFilePath)
        , camera(std::make_unique<editor::OrbitCamera>())
        , controller(std::make_unique<controllers::MeshPreviewController>())
    {
        // Extract filename for window title
        std::filesystem::path path(meshFilePath);
        windowTitle = "Mesh Preview: " + path.filename().string();
    }

    MeshPreviewWindow::~MeshPreviewWindow()
    {
        if (controller)
        {
            controller->cleanUp();
        }
    }

    void MeshPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        // Initialize on first draw
        if (needsInit)
        {
            initRenderer();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            // Don't render content if window is being closed this frame
            // (isOpen was just set to false by clicking X)
            if (isOpen)
            {
                // Split layout: left panel for submesh list, right for 3D viewport
                float panelWidth = 200.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Submesh panel on the left
                ImGui::BeginChild("SubMeshPanel", ImVec2(panelWidth, contentSize.y), true);
                drawSubMeshPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // 3D viewport on the right
                float viewportWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("ViewportPanel", ImVec2(viewportWidth, contentSize.y), true,
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                drawViewport(viewportSize.x, viewportSize.y);

                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void MeshPreviewWindow::initRenderer()
    {
        controller->init();

        math::AABB bounds;
        if (controller->loadMesh(meshPath, bounds))
        {
            camera->fitToBounds(bounds);
            subMeshes = controller->getSubMeshInfo();
        }
    }

    void MeshPreviewWindow::drawViewport(float width, float height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        // Update camera aspect ratio
        camera->setAspectRatio(width / height);

        // Build model matrix from scale and rotation
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::scale(model, glm::vec3(meshScale));
        model = glm::rotate(model, glm::radians(meshRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(meshRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(meshRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        controller->setModelMatrix(model);

        // Update camera matrices in the controller
        controller->updateCamera(
            camera->getViewMatrix(),
            camera->getProjectionMatrix(),
            camera->getPosition()
        );

        // Render and display
        void* texture = controller->render();
        if (texture)
        {
            ImGui::Image(texture, ImVec2(width, height));

            // Check if mouse is over the image for camera input
            bool imageHovered = ImGui::IsItemHovered();

            // Track if drag started on the image
            if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                isDragging = true;
            }

            // Stop dragging when mouse released
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                isDragging = false;
            }

            // Handle camera input only when appropriate
            if (imageHovered || isDragging)
            {
                handleCameraInput(imageHovered);
            }
        }
        else
        {
            ImGui::Text("Loading mesh...");
        }
    }

    void MeshPreviewWindow::drawSubMeshPanel()
    {
        ImGui::Text("Submeshes");
        ImGui::Separator();

        if (subMeshes.empty())
        {
            ImGui::TextDisabled("No submeshes");
            return;
        }

        // "All" option
        bool allSelected = (selectedSubMesh == -1);
        if (ImGui::Selectable("All Submeshes", allSelected))
        {
            selectedSubMesh = -1;
            controller->setHighlightedSubMesh(-1);
        }

        ImGui::Separator();

        // Individual submeshes
        for (size_t i = 0; i < subMeshes.size(); ++i)
        {
            const auto& info = subMeshes[i];
            bool isSelected = (selectedSubMesh == static_cast<int>(i));

            if (ImGui::Selectable(info.name.c_str(), isSelected))
            {
                selectedSubMesh = static_cast<int>(i);
                controller->setHighlightedSubMesh(selectedSubMesh);
            }

            // Show tooltip with details
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Vertices: %u", info.vertexCount);
                ImGui::Text("Indices: %u", info.indexCount);
                ImGui::Text("Triangles: %u", info.indexCount / 3);
                ImGui::EndTooltip();
            }
        }

        ImGui::Separator();

        // Summary
        uint32_t totalVerts = 0;
        uint32_t totalIndices = 0;
        for (const auto& info : subMeshes)
        {
            totalVerts += info.vertexCount;
            totalIndices += info.indexCount;
        }

        ImGui::TextDisabled("Total:");
        ImGui::Text("  Submeshes: %zu", subMeshes.size());
        ImGui::Text("  Vertices: %u", totalVerts);
        ImGui::Text("  Triangles: %u", totalIndices / 3);

        ImGui::Separator();
        ImGui::Spacing();

        // Transform controls
        ImGui::Text("Transform");
        ImGui::Separator();

        // Scale slider
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("Scale", &meshScale, 0.01f, 0.001f, 100.0f, "%.3f"))
        {
            meshScale = glm::clamp(meshScale, 0.001f, 100.0f);
        }

        // Rotation sliders
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("Rot X", &meshRotation.x, 1.0f, -180.0f, 180.0f, "%.1f");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("Rot Y", &meshRotation.y, 1.0f, -180.0f, 180.0f, "%.1f");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("Rot Z", &meshRotation.z, 1.0f, -180.0f, 180.0f, "%.1f");

        // Reset button
        if (ImGui::Button("Reset Transform", ImVec2(-1, 0)))
        {
            meshScale = 1.0f;
            meshRotation = glm::vec3(0.0f);
        }

        // Fit camera button
        if (ImGui::Button("Fit Camera", ImVec2(-1, 0)))
        {
            math::AABB bounds = controller->getMeshBounds();
            camera->fitToBounds(bounds);
        }
    }

    void MeshPreviewWindow::handleCameraInput(bool imageHovered)
    {
        // Left mouse drag to orbit - only when drag started on the image
        if (isDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            camera->processMouseDrag(delta.x, -delta.y);  // Invert Y for natural feel
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        // Scroll to zoom (only when image is hovered)
        if (imageHovered)
        {
            float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f)
            {
                camera->processScroll(scroll);
            }

            // Middle mouse to reset view
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            {
                math::AABB bounds = controller->getMeshBounds();
                camera->fitToBounds(bounds);
            }
        }
    }
}
