#include "MeshPreviewWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/PreviewEvents.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

namespace windows
{
    MeshPreviewWindow::MeshPreviewWindow(const std::string& meshFilePath)
        : meshPath(meshFilePath)
        , camera(std::make_unique<editor::OrbitCamera>())
    {
        // Extract filename for window title
        std::filesystem::path path(meshFilePath);
        windowTitle = "Mesh Preview: " + path.filename().string();
    }

    MeshPreviewWindow::~MeshPreviewWindow()
    {
        // Unload mesh and clean up via PreviewService
        events::EventDispatcher::instance().execute(
            services::events::preview::UnloadPreviewMeshCommand{});
        events::EventDispatcher::instance().execute(
            services::events::preview::CleanUpMeshPreviewCommand{});
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
        // Initialize mesh preview via PreviewService
        events::EventDispatcher::instance().execute(
            services::events::preview::InitMeshPreviewCommand{});

        // Load mesh via PreviewService
        services::events::preview::LoadPreviewMeshCommand loadCmd;
        loadCmd.meshPath = meshPath;
        auto result = events::EventDispatcher::instance().execute(loadCmd);

        if (result.success)
        {
            meshBounds = result.bounds;
            camera->fitToBounds(meshBounds);

            // Get submesh info via PreviewService
            subMeshes = events::EventDispatcher::instance().query(
                services::events::preview::GetPreviewMeshSubMeshInfoQuery{});
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

        // Build model matrix from position, rotation, and scale (TRS order)
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, meshPosition);
        model = glm::rotate(model, glm::radians(meshRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(meshRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(meshRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(meshScale));

        // Set mesh preview params via PreviewService
        services::MeshPreviewParams meshParams;
        meshParams.modelMatrix = model;
        meshParams.highlightedSubMesh = selectedSubMesh;
        services::events::preview::SetMeshPreviewParamsCommand meshCmd;
        meshCmd.params = meshParams;
        events::EventDispatcher::instance().execute(meshCmd);

        // Update camera via PreviewService
        services::events::preview::UpdateMeshCameraCommand cameraCmd;
        cameraCmd.view = camera->getViewMatrix();
        cameraCmd.projection = camera->getProjectionMatrix();
        cameraCmd.cameraPos = camera->getPosition();
        events::EventDispatcher::instance().execute(cameraCmd);

        // Render via PreviewService
        auto textureHandle = events::EventDispatcher::instance().query(
            services::events::preview::RenderMeshPreviewQuery{});

        if (textureHandle.imguiDescriptorSet)
        {
            ImGui::Image(textureHandle.imguiDescriptorSet, ImVec2(width, height));
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
            // Highlight is updated via SetMeshPreviewParamsCommand in drawViewport
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
                // Highlight is updated via SetMeshPreviewParamsCommand in drawViewport
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
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            // Position
            ImGui::Text("Pos X");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::DragFloat("##PosX", &meshPosition.x, 0.1f, -1000.0f, 1000.0f, "%.2f");

            ImGui::Text("Pos Y");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::DragFloat("##PosY", &meshPosition.y, 0.1f, -1000.0f, 1000.0f, "%.2f");

            ImGui::Text("Pos Z");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::DragFloat("##PosZ", &meshPosition.z, 0.1f, -1000.0f, 1000.0f, "%.2f");

            ImGui::Spacing();

            // Rotation
            ImGui::Text("Rot X");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##RotX", &meshRotation.x, -180.0f, 180.0f, "%.0f");

            ImGui::Text("Rot Y");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##RotY", &meshRotation.y, -180.0f, 180.0f, "%.0f");

            ImGui::Text("Rot Z");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##RotZ", &meshRotation.z, -180.0f, 180.0f, "%.0f");

            ImGui::Spacing();

            // Scale
            ImGui::Text("Scale");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderFloat("##Scale", &meshScale, 0.01f, 10.0f, "%.2f"))
            {
                meshScale = glm::clamp(meshScale, 0.001f, 100.0f);
            }

            ImGui::Spacing();
            
            if (ImGui::Button("Reset", ImVec2(-1, 0)))
            {
                meshPosition = glm::vec3(0.0f);
                meshRotation = glm::vec3(0.0f);
                meshScale = 1.0f;
            }

            // Fit camera button
            if (ImGui::Button("Fit Camera", ImVec2(-1, 0)))
            {
                // Use cached bounds from when mesh was loaded
                camera->fitToBounds(meshBounds);
            }
        }
        
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;
            
            float dist = camera->getDistance();
            float minDist = camera->getMinDistance();
            float maxDist = camera->getMaxDistance();

            ImGui::Text("Zoom");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderFloat("##Zoom", &dist, minDist, maxDist, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
                camera->setDistance(dist);
            }

            // Zoom buttons
            if (ImGui::Button("+", ImVec2(itemWidth / 2 - 2, 0)))
            {
                camera->setDistance(dist * 0.9f);
            }
            ImGui::SameLine();
            if (ImGui::Button("-", ImVec2(itemWidth / 2 - 2, 0)))
            {
                camera->setDistance(dist * 1.1f);
            }
        }
    }

}
